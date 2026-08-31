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

/**
 * SpeechInputProvider — interfaz intercambiable de entrada de voz.
 *
 * IVANNA nunca habla con un backend STT concreto: habla con esta interfaz.
 * El backend por defecto es el SpeechRecognizer del sistema (FASE 2, opción A)
 * con gestión explícita de "sin servicio / sin permiso / error del fabricante".
 * Si más adelante se integra un motor offline (Whisper TFLite / ONNX), basta
 * con añadir otra implementación de SpeechInputProvider y registrarla en
 * IvannaAssistant — ninguna otra capa cambia.
 */
interface SpeechInputProvider {
    /** Flujo de estado del reconocedor (para la UI: LED de escucha). */
    val state: StateFlow<SpeechState>

    /** true si el backend puede funcionar en este dispositivo ahora mismo. */
    fun isAvailable(): Boolean

    /** Empieza a escuchar. Resultados por [onResult]. */
    fun startListening(onResult: (String) -> Unit, onError: (String) -> Unit)

    /** Detiene la escucha (idempotente, seguro en cualquier estado). */
    fun stopListening()

    /** Libera recursos. Tras release() hay que crear otro provider. */
    fun release()
}

enum class SpeechState {
    IDLE,            // en reposo
    LISTENING,       // micrófono abierto, capturando
    PROCESSING,      // audio capturado, reconociendo
    UNAVAILABLE,     // sin servicio STT en el dispositivo
    PERMISSION_DENIED,
    ERROR
}

/**
 * IvannaSpeechRecognizer — backend SpeechRecognizer del sistema Android.
 *
 * Decisiones de robustez (ROMs problemáticas):
 *  - Si SpeechRecognizer.isRecognitionAvailable() es false → UNAVAILABLE y
 *    no se intenta crear nada (evita el crash clásico de dispositivos sin
 *    Google app / sin servicio STT del fabricante).
 *  - API 31+: se prefiere createOnDeviceSpeechRecognizer() (on-device, sin
 *    red, más privado y más estable); si el sistema no lo soporta, cae al
 *    recognizer estándar con preferOffline.
 *  - Todos los callbacks vienen en el hilo principal; el provider NO hace
 *    trabajo pesado en ellos — solo emite el texto reconocido y cambia de
 *    estado. Nunca toca el hilo de audio DSP (corre en su propio mundo).
 *  - Errores del servicio (ERROR_CLIENT, ERROR_RECOGNIZER_BUSY, etc.) se
 *    traducen a mensajes accionables, nunca a excepciones.
 */
class IvannaSpeechRecognizer(
    private val context: Context
) : SpeechInputProvider {

    companion object { private const val TAG = "IvannaSpeechRecognizer" }

    private val _state = MutableStateFlow(SpeechState.IDLE)
    override val state: StateFlow<SpeechState> = _state.asStateFlow()

    private var recognizer: SpeechRecognizer? = null
    private var onResultCb: ((String) -> Unit)? = null
    private var onErrorCb: ((String) -> Unit)? = null

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
        onErrorCb = onError
        stopListeningInternal()
        try {
            recognizer = createRecognizer().also { r ->
                r.setRecognitionListener(listener)
                r.startListening(buildIntent())
            }
            _state.value = SpeechState.LISTENING
        } catch (t: Throwable) {
            Log.e(TAG, "No se pudo iniciar el recognizer: ${t.message}", t)
            _state.value = SpeechState.ERROR
            onError("No se pudo iniciar el reconocedor de voz: ${t.message}")
        }
    }

    override fun stopListening() {
        stopListeningInternal()
        if (_state.value == SpeechState.LISTENING || _state.value == SpeechState.PROCESSING) {
            _state.value = SpeechState.IDLE
        }
    }

    override fun release() {
        stopListeningInternal()
        runCatching { recognizer?.destroy() }
        recognizer = null
        _state.value = SpeechState.IDLE
    }

    private fun stopListeningInternal() {
        runCatching { recognizer?.stopListening() }
    }

    private fun createRecognizer(): SpeechRecognizer {
        // API 31+: on-device si el sistema lo soporta (privacidad + estabilidad).
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S &&
                   SpeechRecognizer.isOnDeviceRecognitionAvailable(context)) {
            SpeechRecognizer.createOnDeviceSpeechRecognizer(context)
        } else {
            SpeechRecognizer.createSpeechRecognizer(context)
        }
    }

    private fun buildIntent(): Intent = Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH).apply {
        putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL, RecognizerIntent.LANGUAGE_MODEL_FREE_FORM)
        putExtra(RecognizerIntent.EXTRA_PARTIAL_RESULTS, true)
        putExtra(RecognizerIntent.EXTRA_MAX_RESULTS, 1)
        putExtra(RecognizerIntent.EXTRA_CALLING_PACKAGE, context.packageName)
        // Preferir offline cuando sea posible: menos latencia, menos fuga de datos.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            putExtra(RecognizerIntent.EXTRA_PREFER_OFFLINE, true)
        }
        // Español por defecto — la app es ES-first. El recognizer on-device
        // moderno hace auto-detección razonable si el usuario habla otro idioma.
        putExtra(RecognizerIntent.EXTRA_LANGUAGE, "es-ES")
    }

    private val listener = object : RecognitionListener {
        override fun onReadyForSpeech(params: Bundle?) { _state.value = SpeechState.LISTENING }
        override fun onBeginningOfSpeech() {}
        override fun onRmsChanged(rmsdB: Float) {}
        override fun onBufferReceived(buffer: ByteArray?) {}
        override fun onEndOfSpeech() { _state.value = SpeechState.PROCESSING }
        override fun onEvent(eventType: Int, params: Bundle?) {}

        override fun onError(error: Int) {
            val msg = when (error) {
                SpeechRecognizer.ERROR_INSUFFICIENT_PERMISSIONS ->
                    "Permiso de micrófono denegado."
                SpeechRecognizer.ERROR_NO_MATCH,
                SpeechRecognizer.ERROR_SPEECH_TIMEOUT ->
                    "No se entendió nada — intenta de nuevo."
                SpeechRecognizer.ERROR_RECOGNIZER_BUSY ->
                    "El servicio de voz está ocupado; espera un momento."
                SpeechRecognizer.ERROR_CLIENT ->
                    "Error del servicio de voz del dispositivo."
                SpeechRecognizer.ERROR_NETWORK,
                SpeechRecognizer.ERROR_NETWORK_TIMEOUT ->
                    "Sin red y sin reconocimiento offline disponible."
                SpeechRecognizer.ERROR_SERVER ->
                    "El servicio de voz del fabricante falló."
                else -> "Error de reconocimiento ($error)."
            }
            _state.value = when (error) {
                SpeechRecognizer.ERROR_INSUFFICIENT_PERMISSIONS -> SpeechState.PERMISSION_DENIED
                SpeechRecognizer.ERROR_NO_MATCH,
                SpeechRecognizer.ERROR_SPEECH_TIMEOUT -> SpeechState.IDLE
                else -> SpeechState.ERROR
            }
            onErrorCb?.invoke(msg)
        }

        override fun onResults(results: Bundle?) {
            val text = results
                ?.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION)
                ?.firstOrNull()
                ?.trim()
                .orEmpty()
            _state.value = SpeechState.IDLE
            if (text.isNotEmpty()) onResultCb?.invoke(text)
            else onErrorCb?.invoke("No se entendió nada — intenta de nuevo.")
        }

        override fun onPartialResults(partialResults: Bundle?) {
            // Los parciales se ignoran a propósito: actuar sobre texto a medias
            // dispara intenciones equivocadas ("bajar" → "bajar volumen" cuando el
            // usuario aún estaba hablando). Solo se actúa sobre el resultado final.
        }
    }
}
