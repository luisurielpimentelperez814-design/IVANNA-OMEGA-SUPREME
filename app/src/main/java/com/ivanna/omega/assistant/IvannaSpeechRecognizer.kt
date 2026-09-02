package com.ivanna.omega.assistant

import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.Bundle
import android.speech.RecognitionListener
import android.speech.RecognizerIntent
import android.speech.SpeechRecognizer
import android.util.Log
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

interface SpeechInputProvider {
    val state: StateFlow<SpeechState>
    fun isAvailable(): Boolean
    fun startListening(onResult: (String) -> Unit, onError: (String) -> Unit)
    fun stopListening()
    fun release()
}

enum class SpeechState {
    IDLE, LISTENING, PROCESSING, UNAVAILABLE, PERMISSION_DENIED, ERROR
}

/**
 * IvannaSpeechRecognizer — backend SpeechRecognizer del sistema Android.
 *
 * FIX MICRÓFONO: el recognizer se destruye y recrea en cada ciclo de escucha.
 * En algunas ROMs (MediaTek, Exynos) reutilizar la instancia deja el servicio
 * en estado zombie: onReadyForSpeech nunca dispara aunque el micrófono esté
 * físicamente disponible. Destruir y recrear es el patrón recomendado por
 * Android docs para reconocimiento continuo y elimina el zombie state.
 *
 * Reintento automático ante ERROR_CLIENT (1 intento): es el error más común
 * en ROMs donde el servicio STT del fabricante termina abruptamente.
 *
 * IvannaVoiceRecorder: cada utterance exitosa se registra (append-only, RAM)
 * para que el pipeline conversacional acceda al texto original sin mutaciones.
 */
class IvannaSpeechRecognizer(
    private val context: Context
) : SpeechInputProvider {

    companion object {
        private const val TAG = "IvannaSpeechRecognizer"
        private const val MAX_RETRIES = 1
    }

    private val _state = MutableStateFlow(SpeechState.IDLE)
    override val state: StateFlow<SpeechState> = _state.asStateFlow()

    private var onResultCb: ((String) -> Unit)? = null
    private var onErrorCb:  ((String) -> Unit)? = null
    private var recognizer: SpeechRecognizer?   = null
    private var retryCount  = 0

    override fun isAvailable(): Boolean = try {
        SpeechRecognizer.isRecognitionAvailable(context)
    } catch (t: Throwable) {
        Log.w(TAG, "isRecognitionAvailable falló: ${t.message}")
        false
    }

    override fun startListening(onResult: (String) -> Unit, onError: (String) -> Unit) {
        if (!isAvailable()) {
            _state.value = SpeechState.UNAVAILABLE
            onError("Este dispositivo no tiene servicio de reconocimiento de voz instalado.")
            return
        }
        onResultCb = onResult
        onErrorCb  = onError
        retryCount = 0
        startRecognizer()
    }

    private fun startRecognizer() {
        destroyRecognizer()   // previene zombie state en ROMs problemáticas
        try {
            recognizer = createRecognizer().also { r ->
                r.setRecognitionListener(listener)
                r.startListening(buildIntent())
            }
            _state.value = SpeechState.LISTENING
            Log.d(TAG, "Reconocedor iniciado (intento ${retryCount + 1})")
        } catch (t: Throwable) {
            Log.e(TAG, "No se pudo iniciar recognizer: ${t.message}", t)
            _state.value = SpeechState.ERROR
            onErrorCb?.invoke("No se pudo iniciar el reconocedor: ${t.message}")
        }
    }

    override fun stopListening() {
        runCatching { recognizer?.stopListening() }
        if (_state.value == SpeechState.LISTENING || _state.value == SpeechState.PROCESSING) {
            _state.value = SpeechState.IDLE
        }
    }

    override fun release() {
        destroyRecognizer()
        _state.value = SpeechState.IDLE
        onResultCb = null
        onErrorCb  = null
    }

    private fun destroyRecognizer() {
        runCatching { recognizer?.stopListening() }
        runCatching { recognizer?.destroy() }
        recognizer = null
    }

    private fun createRecognizer(): SpeechRecognizer =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S &&
            SpeechRecognizer.isOnDeviceRecognitionAvailable(context))
            SpeechRecognizer.createOnDeviceSpeechRecognizer(context)
        else
            SpeechRecognizer.createSpeechRecognizer(context)

    private fun buildIntent(): Intent = Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH).apply {
        putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL, RecognizerIntent.LANGUAGE_MODEL_FREE_FORM)
        putExtra(RecognizerIntent.EXTRA_PARTIAL_RESULTS, true)
        putExtra(RecognizerIntent.EXTRA_MAX_RESULTS, 1)
        putExtra(RecognizerIntent.EXTRA_CALLING_PACKAGE, context.packageName)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            putExtra(RecognizerIntent.EXTRA_PREFER_OFFLINE, true)
        }
        putExtra(RecognizerIntent.EXTRA_LANGUAGE, "es-MX")
        putExtra(RecognizerIntent.EXTRA_LANGUAGE_PREFERENCE, "es-MX")
        putExtra("android.speech.extra.EXTRA_ADDITIONAL_LANGUAGES", arrayOf("es-ES", "es-US"))
    }

    private val listener = object : RecognitionListener {
        override fun onReadyForSpeech(params: Bundle?) {
            _state.value = SpeechState.LISTENING
            Log.d(TAG, "Micrófono listo")
        }
        override fun onBeginningOfSpeech() { Log.d(TAG, "Inicio de habla") }
        override fun onRmsChanged(rmsdB: Float) {}
        override fun onBufferReceived(buffer: ByteArray?) {}
        override fun onEndOfSpeech() { _state.value = SpeechState.PROCESSING; Log.d(TAG, "Fin de habla") }
        override fun onEvent(eventType: Int, params: Bundle?) {}

        override fun onError(error: Int) {
            val msg = errorMessage(error)
            Log.w(TAG, "Error STT $error: $msg")
            if (error == SpeechRecognizer.ERROR_CLIENT && retryCount < MAX_RETRIES) {
                retryCount++
                Log.i(TAG, "Reintentando ante ERROR_CLIENT ($retryCount)")
                startRecognizer()
                return
            }
            _state.value = when (error) {
                SpeechRecognizer.ERROR_INSUFFICIENT_PERMISSIONS -> SpeechState.PERMISSION_DENIED
                SpeechRecognizer.ERROR_NO_MATCH,
                SpeechRecognizer.ERROR_SPEECH_TIMEOUT          -> SpeechState.IDLE
                else                                           -> SpeechState.ERROR
            }
            onErrorCb?.invoke(msg)
        }

        override fun onResults(results: Bundle?) {
            val text = results
                ?.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION)
                ?.firstOrNull()?.trim().orEmpty()
            _state.value = SpeechState.IDLE
            retryCount   = 0
            if (text.isNotEmpty()) {
                IvannaVoiceRecorder.record(text)
                Log.d(TAG, "Utterance: \"$text\"")
                onResultCb?.invoke(text)
            } else {
                onErrorCb?.invoke("No se entendió nada — intenta de nuevo.")
            }
        }

        override fun onPartialResults(partialResults: Bundle?) {
            val partial = partialResults
                ?.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION)
                ?.firstOrNull() ?: return
            Log.v(TAG, "Parcial: \"$partial\"")
        }
    }

    private fun errorMessage(error: Int): String = when (error) {
        SpeechRecognizer.ERROR_INSUFFICIENT_PERMISSIONS ->
            "Permiso de micrófono denegado. Ve a Ajustes → Permisos → Micrófono."
        SpeechRecognizer.ERROR_NO_MATCH,
        SpeechRecognizer.ERROR_SPEECH_TIMEOUT -> "No se entendió nada — intenta de nuevo."
        SpeechRecognizer.ERROR_RECOGNIZER_BUSY -> "El servicio de voz está ocupado; espera un momento."
        SpeechRecognizer.ERROR_CLIENT          -> "Error interno del reconocedor. Reintentando..."
        SpeechRecognizer.ERROR_NETWORK,
        SpeechRecognizer.ERROR_NETWORK_TIMEOUT -> "Sin red y sin reconocimiento offline disponible."
        SpeechRecognizer.ERROR_SERVER          -> "El servicio de voz del fabricante falló."
        else -> "Error de reconocimiento ($error)."
    }
}

/**
 * IvannaVoiceRecorder — registro inmutable de utterances del usuario.
 *
 * Append-only, RAM únicamente (no persiste entre sesiones).
 * Se limpia con IvannaAssistant.clearMemory().
 */
object IvannaVoiceRecorder {
    private val utterances = mutableListOf<String>()
    @Synchronized fun record(text: String) { utterances.add(text) }
    @Synchronized fun getAll(): List<String> = utterances.toList()
    @Synchronized fun getLast(): String? = utterances.lastOrNull()
    @Synchronized fun clear() { utterances.clear() }
}
