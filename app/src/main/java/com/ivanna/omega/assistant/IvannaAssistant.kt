package com.ivanna.omega.assistant

import android.content.Context
import android.util.Log
import com.ivanna.omega.VoiceController
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

/**
 * IvannaAssistant — orquestador de la capa conversacional.
 *
 * Une las piezas sin tocar el audio thread:
 *
 *   Micrófono → SpeechInputProvider (ASR) → IvannaIntentMapper (intención)
 *     → agentes existentes (AgentApi / VoiceController) → IvannaVoiceEngine (TTS)
 *
 * Mantiene el estado de la conversación en un StateFlow que el panel Compose
 * observa directamente. Toda la ejecución de órdenes va a Dispatchers.IO;
 * el reconocedor y el TTS son del sistema y no tocan el pipeline DSP.
 *
 * Ciclo de vida: init() desde la UI (el recognizer necesita hilo principal),
 * release() al salir de la pantalla. start()/stop() controlan la escucha.
 */
class IvannaAssistant(context: Context) {

    companion object { private const val TAG = "IvannaAssistant" }

    data class ConversationTurn(
        val userText: String,
        val ivannaText: String,
        val timestampMs: Long = System.currentTimeMillis()
    )

    data class AssistantUiState(
        val listening: Boolean = false,
        val speaking: Boolean = false,
        val available: Boolean = true,
        val statusLine: String = "Toca el micrófono y habla conmigo.",
        val lastTurn: ConversationTurn? = null,
        val voiceName: String = ""
    )

    private val appContext = context.applicationContext
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)

    private val _ui = MutableStateFlow(AssistantUiState())
    val ui: StateFlow<AssistantUiState> = _ui.asStateFlow()

    private val speech: SpeechInputProvider = IvannaSpeechRecognizer(appContext)
    private val voice = IvannaVoiceEngine(appContext)
    private val memory = IvannaContextMemory(appContext)
    private val voiceController: VoiceController = VoiceController(appContext)

    // Vigilante de estado de voz para reflejar SPEAKING en la UI.
    private var voiceWatchStarted = false

    val speechState: StateFlow<SpeechState> = speech.state

    /** Inicializa el vigilante de TTS (llamar una vez desde el primer uso). */
    private fun watchVoice() {
        if (voiceWatchStarted) return
        voiceWatchStarted = true
        scope.launch {
            voice.state.collect { vs ->
                _ui.value = _ui.value.copy(speaking = vs == VoiceState.SPEAKING)
            }
        }
    }

    /** Saludo contextual al abrir el panel (usa la memoria, sin exagerar). */
    fun greet() {
        watchVoice()
        val scene = memory.lastScene
        val text = when {
            scene == "MUSIC" -> "Hola. La última vez estábamos escuchando música. ¿Qué ajustamos hoy?"
            scene == "VOICE" -> "Hola. La última vez había mucho diálogo; puedo mantener las voces claras si quieres."
            else             -> "Hola, soy IVANNA. Puedo ajustar el sonido por ti: dime qué necesitas."
        }
        _ui.value = _ui.value.copy(statusLine = text, voiceName = voice.selectedVoiceName ?: "")
        voice.speak(text)
    }

    /** Empieza a escuchar al usuario (botón mic). */
    fun startListening() {
        watchVoice()
        if (!speech.isAvailable()) {
            val msg = "Este dispositivo no tiene reconocimiento de voz instalado."
            _ui.value = _ui.value.copy(available = false, statusLine = msg)
            voice.speak(msg)
            return
        }
        voice.stop()  // si IVANNA estaba hablando, se calla al escuchar
        _ui.value = _ui.value.copy(listening = true, statusLine = "Te escucho…")
        speech.startListening(
            onResult = { text -> onUserSaid(text) },
            onError = { err ->
                _ui.value = _ui.value.copy(listening = false, statusLine = err)
            }
        )
    }

    fun stopListening() {
        speech.stopListening()
        _ui.value = _ui.value.copy(listening = false, statusLine = "Escucha detenida.")
    }

    /** También acepta texto escrito (pruebas / accesibilidad). */
    fun onUserSaid(text: String) {
        _ui.value = _ui.value.copy(listening = false, statusLine = "Procesando…")
        scope.launch(Dispatchers.IO) {
            val intent = IvannaIntentMapper.map(text)
            val reply = IvannaIntentMapper.execute(appContext, intent, voiceController)
            // Registrar en memoria (ajustes reales → explicabilidad futura).
            if (intent.target != IvannaIntentMapper.AgentTarget.NONE &&
                intent.command != "explain" && intent.command != "diagnose") {
                memory.recordAdjustment(intent.command, "petición del usuario: \"$text\"", applied = true)
            }
            memory.lastExplanation = reply
            launch(Dispatchers.Main) {
                _ui.value = _ui.value.copy(
                    statusLine = reply,
                    lastTurn = ConversationTurn(userText = text, ivannaText = reply)
                )
                voice.speak(reply)
            }
        }
    }

    fun clearMemory() {
        memory.clearAll()
        val msg = "He olvidado mis notas de esta y otras sesiones."
        _ui.value = _ui.value.copy(statusLine = msg, lastTurn = null)
        voice.speak(msg)
    }

    fun release() {
        speech.release()
        voice.release()
    }
}
