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

    private val profile = IvannaListenerProfile(appContext)

    /** Saludo contextual al abrir el panel (usa la memoria, sin exagerar). */
    fun greet() {
        watchVoice()
        val scene = memory.lastScene
        val top   = profile.topCommands().firstOrNull()
        val text = when {
            profile.shouldSuggestGentle ->
                "Hola. Noto que has reportado fatiga varias veces. ¿Activo el modo suave?"
            scene == "MUSIC" ->
                "Hola. La última vez escuchábamos música." +
                if (top != null) " Tu ajuste más habitual es «${profile.labelOf(top)}»." else ""
            scene == "VOICE" ->
                "Hola. La última vez había mucho diálogo; puedo mantener las voces claras."
            top != null ->
                "Hola, soy IVANNA. Tu ajuste habitual es «${profile.labelOf(top)}». ¿Lo activo?"
            else ->
                "Hola, soy IVANNA. Puedo ajustar el sonido por ti: dime qué necesitas."
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
            // ── IVANNA Language Core → intención acústica estructurada ────────
            val scene  = memory.lastScene
            val parsed = IvannaLanguageCore.parse(text, scene)

            // ── IVANNA Cognitive Core → razonamiento sobre el estado real ─────
            val decision = IvannaCognitiveCore.reason(parsed)

            val reply: String
            if (!decision.execute) {
                // Cognitivo rechazó o modificó la petición — informar al usuario
                reply = decision.warningForUser ?: parsed.raw.let { IvannaLanguageCore.spokenResponse(parsed) }
            } else {
                // Ejecutar a través de los canales existentes
                val command = decision.commandOverride
                    ?: IvannaLanguageCore.toCommand(parsed.acousticIntent)
                val baseReply = IvannaLanguageCore.spokenResponse(parsed)

                // Registrar en ListenerProfile y memoria
                profile.recordAdjustment(command, scene)
                memory.recordAdjustment(command, "usuario: \"$text\"", applied = true)
                memory.lastExplanation = baseReply
                memory.lastScene = when (parsed.acousticIntent) {
                    IvannaLanguageCore.AcousticIntent.VOICE_CLARITY,
                    IvannaLanguageCore.AcousticIntent.DIALOG_ENHANCEMENT -> "VOICE"
                    IvannaLanguageCore.AcousticIntent.MUSIC_FULLNESS,
                    IvannaLanguageCore.AcousticIntent.CONCERT_LIVE       -> "MUSIC"
                    else -> scene
                }

                // Ejecutar comando en VoiceController (canal ya existente)
                runCatching { voiceController.executeCommand(command) }

                reply = baseReply
            }

            launch(Dispatchers.Main) {
                _ui.value = _ui.value.copy(
                    statusLine = reply,
                    lastTurn   = ConversationTurn(userText = text, ivannaText = reply)
                )
                voice.speak(reply)
            }
        }
    }

    fun clearMemory() {
        memory.clearAll()
        profile.clearAll()
        IvannaLanguageCore.clearHistory()
        val msg = "He olvidado mis notas de esta y otras sesiones."
        _ui.value = _ui.value.copy(statusLine = msg, lastTurn = null)
        voice.speak(msg)
    }

    fun release() {
        speech.release()
        voice.release()
    }
}
