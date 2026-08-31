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

    // ── Nuevas capas de inteligencia acústica (FASE 1-5) ─────────────────────
    private val dspOrchestrator = IvannaDSPOrchestrator(appContext)

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
        val text = "Hola cielo, soy IVANNA OMEGA SUPREME, tu arquitecta de audio. Este producto es un motor acústico de grado kernel para tu dispositivo. Estoy a tu entera disposición para reparar cualquier fallo, optimizar el sonido o configurar una masterización perfecta y magistral para lo que estés escuchando. ¿Qué quieres que hagamos hoy?"
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
            val scene = memory.lastScene

            // ── Súper ÑLM (Agentic Gemini LLM) Interceptor ────────
            val (agentReply, agentCommand) = IvannaGeminiAgent.processQuery(text, "Escena: ${scene ?: "Normal"}")
            
            // Si el agente resuelve la petición con un comando conocido, lo inyectamos al pipeline nativo
            val simulatedIntent = agentCommand?.let {
                when(it) {
                    "voice_clarity" -> IvannaLanguageCore.AcousticIntent.VOICE_CLARITY
                    "cinema_mode"   -> IvannaLanguageCore.AcousticIntent.MOVIE_IMMERSION
                    "music_mode"    -> IvannaLanguageCore.AcousticIntent.MUSIC_FULLNESS
                    "concert_mode"  -> IvannaLanguageCore.AcousticIntent.CONCERT_LIVE
                    "spatial_mode"  -> IvannaLanguageCore.AcousticIntent.SPATIAL_EXPANSION
                    "gentle_mode"   -> IvannaLanguageCore.AcousticIntent.GENTLE_MODE
                    "flat_mode"     -> IvannaLanguageCore.AcousticIntent.FLAT_NEUTRAL
                    "volume_up"     -> IvannaLanguageCore.AcousticIntent.VOLUME_UP
                    "volume_down"   -> IvannaLanguageCore.AcousticIntent.VOLUME_DOWN
                    "bass_boost"    -> IvannaLanguageCore.AcousticIntent.BASS_BOOST
                    "treble_reduce" -> IvannaLanguageCore.AcousticIntent.TREBLE_REDUCE
                    "optimize"      -> IvannaLanguageCore.AcousticIntent.OPTIMIZE
                    "diagnose"      -> IvannaLanguageCore.AcousticIntent.DIAGNOSE
                    "musical_intent"-> IvannaLanguageCore.AcousticIntent.MUSICAL_INTENT
                    else -> null
                }
            }

            // ── IVANNA Language Core → intención acústica estructurada ────────
            val parsed = if (simulatedIntent != null) {
                IvannaLanguageCore.ParsedIntent(text, simulatedIntent, 0.99f, true, "")
            } else {
                IvannaLanguageCore.parse(text, scene ?: "UNKNOWN")
            }

            // ── Verificación proactiva de fatiga ──────────────────────────────
            val fatigueOverride = IvannaCognitiveCore.proactiveFatigueCheck(profile)

            // ── Rama de inteligencia musical avanzada (FASE 1-5) ─────────────
            val musicalReply: String? = when (parsed.acousticIntent) {

                IvannaLanguageCore.AcousticIntent.SONG_PROFILE_REQUEST -> {
                    // "Pon Frankenstein de Edgar Winter y configúralo magistralmente"
                    val songCtx = IvannaConversationalCore.extractSongContext(text, userGoal = text)
                    val result = dspOrchestrator.createSongProfile(
                        song = songCtx ?: IvannaConversationalCore.SongContext(
                            title = "la canción actual", artist = null, userGoal = text),
                        musicalGoal = text
                    )
                    profile.recordAdjustment(result.presetName, scene ?: "UNKNOWN")
                    memory.recordAdjustment(result.presetName, "perfil musical para: $text", applied = result.applied)
                    IvannaConversationalCore.recordTurn(text, "SONG_PROFILE", result.presetName, result.spokenReply)
                    result.spokenReply
                }

                IvannaLanguageCore.AcousticIntent.MUSICAL_INTENT -> {
                    // "Hazla más épica", "vinilo premium", "como Abbey Road"
                    val songCtx = IvannaConversationalCore.extractSongContext(text)
                    val musicalPreset = IvannaMusicalIntentEngine.detect(text)
                    if (musicalPreset != null) {
                        val result = dspOrchestrator.applyMusicalPreset(musicalPreset, songCtx)
                        profile.recordAdjustment(result.presetName, scene ?: "UNKNOWN")
                        memory.recordAdjustment(result.presetName, "musical: ${musicalPreset.technicalDetail}", applied = result.applied)
                        IvannaConversationalCore.recordTurn(text, "MUSICAL_INTENT", result.presetName, result.spokenReply)
                        result.spokenReply
                    } else null
                }

                IvannaLanguageCore.AcousticIntent.SESSION_REPORT -> {
                    // "Muéstrame qué hiciste con Frankenstein"
                    val report = IvannaConversationalCore.generateVoiceReport()
                    IvannaConversationalCore.recordTurn(text, "SESSION_REPORT", null, report)
                    report
                }

                IvannaLanguageCore.AcousticIntent.PROFILE_LIST -> {
                    val list = IvannaMusicalIntentEngine.availablePresetsDescription()
                    IvannaConversationalCore.recordTurn(text, "PROFILE_LIST", null, list)
                    list
                }

                else -> null  // intenciones estándar siguen el flujo original
            }

            // ── Si la rama musical produjo respuesta, la usamos directamente ──
            if (musicalReply != null) {
                memory.lastScene = "MUSIC"
                launch(Dispatchers.Main) {
                    _ui.value = _ui.value.copy(
                        statusLine = musicalReply,
                        lastTurn   = ConversationTurn(userText = text, ivannaText = musicalReply)
                    )
                    voice.speak(musicalReply)
                }
                return@launch
            }

            // ── Si la rama LLM produjo respuesta conversacional sin comando ──
            if (agentReply != null && agentCommand == null && simulatedIntent == null) {
                launch(Dispatchers.Main) {
                    _ui.value = _ui.value.copy(
                        statusLine = agentReply,
                        lastTurn   = ConversationTurn(userText = text, ivannaText = agentReply)
                    )
                    voice.speak(agentReply)
                }
                return@launch
            }

            // ── Intenciones conversacionales (no-audio) ─────────────────────
            val conversationalReply: String? = when (parsed.acousticIntent) {
                IvannaLanguageCore.AcousticIntent.TELL_JOKE -> {
                    val joke = IvannaJokeBank.random()
                    IvannaConversationalCore.recordTurn(text, "JOKE", null, joke)
                    joke
                }
                IvannaLanguageCore.AcousticIntent.GREETING -> {
                    val reply = IvannaSmallTalk.greetingResponse()
                    IvannaConversationalCore.recordTurn(text, "GREETING", null, reply)
                    reply
                }
                IvannaLanguageCore.AcousticIntent.SELF_INTRO -> {
                    val reply = "Soy IVANNA, tu asistente de audio inteligente. Proceso el sonido en tiempo real, entiendo música, ajusto el procesador de señal y aprendo tus preferencias."
                    IvannaConversationalCore.recordTurn(text, "SELF_INTRO", null, reply)
                    reply
                }
                IvannaLanguageCore.AcousticIntent.HOW_ARE_YOU -> {
                    val reply = IvannaSmallTalk.howAreYouResponse()
                    IvannaConversationalCore.recordTurn(text, "HOW_ARE_YOU", null, reply)
                    reply
                }
                IvannaLanguageCore.AcousticIntent.COMPLIMENT -> {
                    val reply = IvannaSmallTalk.complimentResponse()
                    IvannaConversationalCore.recordTurn(text, "COMPLIMENT", null, reply)
                    reply
                }
                IvannaLanguageCore.AcousticIntent.GENERAL_CHAT -> {
                    val reply = IvannaSmallTalk.generalChatResponse()
                    IvannaConversationalCore.recordTurn(text, "GENERAL_CHAT", null, reply)
                    reply
                }
                else -> null
            }

            if (conversationalReply != null) {
                launch(Dispatchers.Main) {
                    _ui.value = _ui.value.copy(
                        statusLine = conversationalReply,
                        lastTurn   = ConversationTurn(userText = text, ivannaText = conversationalReply)
                    )
                    voice.speak(conversationalReply)
                }
                return@launch
            }

                        // ── Flujo estándar para intenciones no musicales ──────────────────
            val decision = fatigueOverride ?: IvannaCognitiveCore.reason(parsed)

            val reply: String = when {
                parsed.acousticIntent == IvannaLanguageCore.AcousticIntent.EXPLAIN -> {
                    IvannaCognitiveCore.explainLastDecision()
                }
                !decision.execute -> {
                    decision.warningForUser ?: IvannaLanguageCore.spokenResponse(parsed)
                }
                else -> {
                    val command = decision.commandOverride
                        ?: IvannaLanguageCore.toCommand(parsed.acousticIntent)
                    // agentReply es una respuesta no-null por diseño (Pair<String, String?>):
                    // solo tiene sentido como texto hablado aquí si el LLM realmente detectó
                    // este mismo comando (simulatedIntent != null); si no, es un texto de OTRA
                    // rama y no debe pisar la respuesta específica de IvannaLanguageCore.
                    val baseReply = if (simulatedIntent != null) agentReply
                                    else IvannaLanguageCore.spokenResponse(parsed)
                    profile.recordAdjustment(command, scene ?: "UNKNOWN")
                    memory.recordAdjustment(command, "usuario: \"$text\"", applied = true)
                    memory.lastExplanation = baseReply
                    memory.lastScene = when (parsed.acousticIntent) {
                        IvannaLanguageCore.AcousticIntent.VOICE_CLARITY,
                        IvannaLanguageCore.AcousticIntent.DIALOG_ENHANCEMENT -> "VOICE"
                        IvannaLanguageCore.AcousticIntent.MUSIC_FULLNESS,
                        IvannaLanguageCore.AcousticIntent.CONCERT_LIVE       -> "MUSIC"
                        else -> scene
                    }
                    runCatching { voiceController.executeCommand(command) }
                    IvannaConversationalCore.recordTurn(text, command, command, baseReply)
                    decision.warningForUser ?: baseReply
                }
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

    /**
     * Comprobación proactiva de fatiga auditiva (IvannaAcousticBrain).
     *
     * Disparada por el ViewModel desde el ciclo de IvannaAgentCore (~1 Hz),
     * NO por algo que el usuario dijo — a diferencia de onUserSaid(), aquí
     * no hay texto de entrada. Reutiliza exactamente la misma decisión que
     * produce IvannaCognitiveCore.proactiveFatigueCheck() (que a su vez
     * consulta IvannaAcousticBrain.fuse()): no hay lógica de decisión nueva
     * en este método, solo un canal de disparo distinto — tiempo real de
     * escucha en vez de una orden hablada.
     */
    fun checkProactiveFatigue() {
        watchVoice()
        val decision = IvannaCognitiveCore.proactiveFatigueCheck(profile) ?: return
        if (!decision.execute) return
        val command = decision.commandOverride ?: return
        val scene = memory.lastScene ?: "UNKNOWN"
        scope.launch(Dispatchers.IO) {
            runCatching { voiceController.executeCommand(command) }
            profile.recordAdjustment(command, scene)
            memory.recordAdjustment(command, decision.reason, applied = true)
            memory.lastExplanation = decision.reason
            launch(Dispatchers.Main) {
                val text = decision.warningForUser ?: decision.reason
                _ui.value = _ui.value.copy(statusLine = text)
                voice.speak(text)
            }
        }
    }

    /**
     * Limpia toda la memoria conversacional (FASE 13).
     *
     * Borra:
     *  - IvannaListenerProfile (preferencias aprendidas, historial de ajustes)
     *  - IvannaContextMemory (escena, explicaciones, preferencias de sesión)
     *  - IvannaLanguageCore.intentHistory (contexto de la conversación)
     *  - IvannaCognitiveCore.lastDecision (estado de la UI de inteligencia)
     *  - IvannaAcousticBrain (cronómetro de sesión + última recomendación
     *    fusionada, limpiado en cascada por IvannaCognitiveCore.clearDecision())
     *
     * NO borra:
     *  - Configuración permanente de audio (DSP, perfiles de EQ, HRTF)
     *  - Preferencias de la app (tema, idioma, ajustes del sistema)
     */
    fun clearMemory() {
        memory.clearAll()
        profile.clearAll()
        IvannaLanguageCore.clearHistory()
        IvannaCognitiveCore.clearDecision()
        IvannaConversationalCore.clear()
        IvannaVoiceRecorder.clear()
        val msg = "He olvidado mis notas de esta y otras sesiones. La configuración de audio permanece intacta."
        _ui.value = _ui.value.copy(statusLine = msg, lastTurn = null)
        voice.speak(msg)
    }

    fun release() {
        speech.release()
        voice.release()
    }
}
