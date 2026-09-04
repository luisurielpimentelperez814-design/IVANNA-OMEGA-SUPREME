package com.ivanna.omega.ui.viewmodels

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.ivanna.omega.agent.IvannaAgentCore
import com.ivanna.omega.assistant.IvannaAcousticBrain
import com.ivanna.omega.assistant.IvannaAssistant
import com.ivanna.omega.assistant.IvannaCognitiveCore as IvannaStaticCore
import com.ivanna.omega.assistant.IvannaListenerProfile
import com.ivanna.omega.assistant.SpeechState
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

// ── Unified state machine ──────────────────────────────────────────────────────

/**
 * AssistantPhase — estados canónicos del pipeline conversacional.
 *
 * IDLE       → esperando input del usuario
 * LISTENING  → micrófono abierto, capturando audio
 * PROCESSING → ASR terminó, IvannaLanguageCore analiza la intención
 * EXECUTING  → AgentCore aplicando el comando al DSP
 * SPEAKING   → IvannaVoiceEngine reproduciendo la respuesta
 * ERROR      → sin micrófono, sin TTS, o error irrecuperable
 */
enum class AssistantPhase { IDLE, LISTENING, PROCESSING, EXECUTING, SPEAKING, ERROR }

/**
 * AssistantPanelState — estado completo observable por la UI.
 *
 * Un único StateFlow de este tipo alimenta TODOS los paneles de
 * IvannaAssistantScreen sin duplicar estado ni crear dependencias circulares.
 */
data class AssistantPanelState(

    // ── Estado global ──────────────────────────────────────────────────────
    val phase: AssistantPhase = AssistantPhase.IDLE,
    val statusLine: String = "Toca el micrófono y habla con IVANNA.",

    // ── Panel de inteligencia ──────────────────────────────────────────────
    val detectedIntent: String = "—",
    val activeAgent: String = "—",
    val lastAction: String = "—",
    val explanation: String = "Sin decisiones registradas.",

    // ── Panel de audio ─────────────────────────────────────────────────────
    val currentScene: String = "—",
    val activeProfile: String = "—",
    val dspStatus: String = "Normal",

    // ── Panel de memoria ───────────────────────────────────────────────────
    val sessionContext: String = "Sin datos de sesión.",
    val learnedPreferences: List<String> = emptyList(),

    // ── Transcripción (último turno) ───────────────────────────────────────
    val lastUserText: String = "",
    val lastIvannaText: String = "",

    // ── Disponibilidad ──────────────────────────────────────────────────────
    val micAvailable: Boolean = true,
    val ttsAvailable: Boolean = true,

    // ── API key Gemini (cableado FASE final) ─────────────────────────────
    val geminiApiKey: String = "",
    val geminiAvailable: Boolean = false,
    val errorMessage: String? = null
)

// ── ViewModel ─────────────────────────────────────────────────────────────────

/**
 * IvannaAssistantViewModel
 *
 * Conecta definitivamente (FASE 12):
 *   IvannaAssistant → IvannaLanguageCore → IvannaCognitiveCore
 *                  → IvannaAgentCore → IvannaVoiceEngine
 *
 * Integra memoria (FASE 13):
 *   IvannaListenerProfile + IvannaCognitiveCore → paneles de UI
 *
 * El ViewModel nunca escribe al hilo de audio. Toda la coordinación ocurre
 * en viewModelScope (Dispatchers.Main por defecto para flows de UI,
 * Dispatchers.IO delegado a IvannaAssistant para el razonamiento real).
 */
class IvannaAssistantViewModel(app: Application) : AndroidViewModel(app) {

    // ── Núcleos ───────────────────────────────────────────────────────────
    private val memoryArchitecture = com.ivanna.omega.ai.memory.IvannaMemoryArchitecture(app)

    private val geminiAgent = com.ivanna.omega.ai.gemini.IvannaGeminiAgent(
        context = app,
        memory = memoryArchitecture,
        contextEngine = com.ivanna.omega.assistant.core.DynamicContextEngine.getInstance()
            ?: com.ivanna.omega.assistant.core.DynamicContextEngine.init(app)
    )

    private val assistant = IvannaAssistant(
        app,
        geminiAgent
    )
    private val profile   = IvannaListenerProfile(app)

    // ── Estado público ────────────────────────────────────────────────────
    private val _panel = MutableStateFlow(AssistantPanelState())
    val panel: StateFlow<AssistantPanelState> = _panel.asStateFlow()

    // Borde ascendente del riesgo de fatiga fusionado por IvannaAcousticBrain:
    // dispara la voz proactiva una sola vez por episodio, no en cada ciclo.
    private var wasFatigueRisk = false

    /** Persiste la key vía IvannaGeminiAgent (SharedPreferences "ivanna_assistant")
     *  y refleja disponibilidad en el panel. El agente resuelve en cascada:
     *  setApiKey > persistida > BuildConfig — este setter es la fuente UI. */
    fun setGeminiApiKey(key: String) {
        runCatching {
            // FIX (leak/race real): Application.onCreate() inicializa esto
            // dentro de un coroutine async (appScope.launch) — si el usuario
            // abre esta pantalla y guarda su key ANTES de que ese launch
            // termine, requireInitialized() lanzaba IllegalStateException,
            // el runCatching la tragaba en silencio y la key JAMÁS se
            // persistía (bug de "preferencias que no persisten"). initialize()
            // ya es idempotente (early-return si isInitialized), así que
            // llamarla aquí también es seguro y cierra la ventana de carrera.
            com.ivanna.omega.assistant.core.SecureConfigurationManager.initialize(getApplication())
            com.ivanna.omega.assistant.core.SecureConfigurationManager.setApiKey(key.trim())
        }
        val avail = runCatching { com.ivanna.omega.assistant.core.SecureConfigurationManager.state.value.isConfigured }.getOrDefault(false)
        _panel.value = _panel.value.copy(geminiApiKey = key.trim(), geminiAvailable = avail)
    }

    init {
        // 0. Reflejar disponibilidad Gemini al abrir (key persistida o BuildConfig)
        runCatching {
            val avail = com.ivanna.omega.assistant.core.SecureConfigurationManager.state.value.isConfigured
            if (avail) _panel.value = _panel.value.copy(geminiAvailable = true)
        }
        // 1. Observar el estado de IvannaAssistant (ui + speech + voice)
        viewModelScope.launch {
            assistant.ui.collect { ui ->
                val phase = derivePhase(ui)
                _panel.value = _panel.value.copy(
                    phase        = phase,
                    statusLine   = ui.statusLine,
                    micAvailable = ui.available,
                    lastUserText  = ui.lastTurn?.userText ?: _panel.value.lastUserText,
                    lastIvannaText = ui.lastTurn?.ivannaText ?: _panel.value.lastIvannaText,
                    detectedIntent = ui.lastTurn?.let { inferIntentLabel(it.userText) }
                        ?: _panel.value.detectedIntent,
                    errorMessage = if (!ui.available) ui.statusLine else null
                )
                if (ui.lastTurn != null) refreshMemoryPanel()
            }
        }

        // 2. Refinar la fase con el estado del reconocedor de voz
        viewModelScope.launch {
            assistant.speechState.collect { ss ->
                val phase = when (ss) {
                    SpeechState.LISTENING         -> AssistantPhase.LISTENING
                    SpeechState.PROCESSING        -> AssistantPhase.PROCESSING
                    SpeechState.ERROR             -> AssistantPhase.ERROR
                    SpeechState.PERMISSION_DENIED -> AssistantPhase.ERROR
                    SpeechState.UNAVAILABLE       -> AssistantPhase.ERROR
                    SpeechState.IDLE              -> null  // deja la fase actual
                }
                if (phase != null) _panel.value = _panel.value.copy(phase = phase)
            }
        }

        // 3. Observar IvannaAgentCore → panel de audio
        viewModelScope.launch {
            IvannaAgentCore.state.collect { agentState ->
                val scene = agentState.perception.scene.name
                val profile = agentState.activePolicy
                val dsp = buildDspStatus(agentState.health)
                val lastAction = agentState.lastAction.takeIf { it != "—" }
                    ?: _panel.value.lastAction

                // Si el agente acaba de cambiar de política → fase EXECUTING
                val phase = if (agentState.running &&
                    agentState.activePolicy != _panel.value.activeProfile &&
                    agentState.activePolicy != "neutral")
                    AssistantPhase.EXECUTING
                else _panel.value.phase

                _panel.value = _panel.value.copy(
                    phase         = phase,
                    currentScene  = scene,
                    activeProfile = profile,
                    dspStatus     = dsp,
                    lastAction    = lastAction
                )
                // IvannaAcousticBrain fusiona percepción + salud + perfil del
                // oyente + duración real de sesión en cada ciclo del agente
                // (~1 Hz) — así el contexto de sesión vive incluso sin que
                // el usuario hable.
                refreshMemoryPanel()

                // Voz proactiva: si el riesgo de fatiga fusionado acaba de
                // aparecer (borde ascendente), IVANNA habla sin que se lo
                // pidan — misma decisión que proactiveFatigueCheck(), solo
                // que aquí el disparo es el tiempo de escucha, no el habla.
                val fatigueNow = IvannaAcousticBrain.insight.value.fatigueRisk
                if (fatigueNow && !wasFatigueRisk) assistant.checkProactiveFatigue()
                wasFatigueRisk = fatigueNow
            }
        }

        // 4. Observar IvannaCognitiveCore → panel de inteligencia
        viewModelScope.launch {
            IvannaStaticCore.lastDecision.collect { decision ->
                if (decision != null) {
                    _panel.value = _panel.value.copy(
                        activeAgent = if (decision.execute) "IvannaAgentCore"
                                      else "IvannaStaticCore (bloqueó)",
                        explanation = decision.warningForUser ?: decision.reason
                    )
                }
            }
        }

        // Saludo inicial + snapshot de memoria
        assistant.greet()
        refreshMemoryPanel()
    }

    // ── Helpers privados ──────────────────────────────────────────────────

    private fun derivePhase(ui: IvannaAssistant.AssistantUiState): AssistantPhase = when {
        !ui.available            -> AssistantPhase.ERROR
        ui.listening             -> AssistantPhase.LISTENING
        ui.speaking               -> AssistantPhase.SPEAKING
        ui.processing            -> AssistantPhase.PROCESSING
        else                     -> AssistantPhase.IDLE
    }

    private fun buildDspStatus(h: IvannaAgentCore.HealthSnapshot): String = buildString {
        if (h.clipping) append("⚠ Clipping ")
        if (h.clipCountDelta > 0) append("clips/s:${h.clipCountDelta} ")
        val t = h.thermalLoad
        if (t >= 0.6f) append("Térmico:${"%.0f".format(t * 100)}% ")
        if (h.latencyDegraded) append("⚠ Latencia ")
        if (h.adaptiveEngineRunning) append("Motor:activo")
        if (isEmpty()) append("Normal")
    }

    private fun inferIntentLabel(userText: String): String {
        val t = userText.lowercase()
        return when {
            t.contains("voz") || t.contains("diálogo") || t.contains("dialogo") -> "VOICE_CLARITY"
            t.contains("cine") || t.contains("pelícu") || t.contains("pelicula") -> "MOVIE_IMMERSION"
            t.contains("música") || t.contains("musica") || t.contains("cuerpo") -> "MUSIC_FULLNESS"
            t.contains("espacio") || t.contains("surround") || t.contains("3d") -> "SPATIAL_EXPANSION"
            t.contains("concierto") || t.contains("en vivo") -> "CONCERT_LIVE"
            t.contains("cansado") || t.contains("fatiga") || t.contains("duele") -> "LISTENING_FATIGUE"
            t.contains("suave") || t.contains("gentle") -> "GENTLE_MODE"
            t.contains("hiciste") || t.contains("cambiaste") || t.contains("qué hiciste") -> "EXPLAIN"
            t.contains("optimiza") || t.contains("batería") -> "OPTIMIZE"
            t.contains("plano") || t.contains("neutro") || t.contains("sin efectos") -> "FLAT_NEUTRAL"
            t.contains("sube") || t.contains("más alto") || t.contains("más fuerte") -> "VOLUME_UP"
            t.contains("baja") || t.contains("más bajo") -> "VOLUME_DOWN"
            t.contains("graves") || t.contains("bajos") || t.contains("bass") -> "BASS_BOOST"
            userText.isNotEmpty() -> "PROCESADO"
            else -> "—"
        }
    }

    private fun refreshMemoryPanel() {
        val topCmds = profile.topCommands().map { profile.labelOf(it) }
        val mode    = profile.labelOf(profile.preferredMode)
        val fatigue = profile.fatigueReports
        // FIX (CI rojo): `memory` no existe en el ViewModel (esa propiedad vive
        // en IvannaAssistant). La escena actual se lee de la misma fuente viva
        // que usa assistant.IvannaCognitiveCore: IvannaAgentCore.state.
        val scene   = IvannaAgentCore.state.value.perception.scene.name
        // Fusión en vivo: percepción + salud + perfil + duración real de
        // sesión. No ejecuta nada — solo informa el panel de memoria.
        val brainInsight = IvannaAcousticBrain.fuse(profile)

        val context = buildString {
            append("Modo preferido: $mode")
            if (fatigue > 0) append("  ·  Fatiga reportada: ${fatigue}×")
            append("  ·  Escena anterior: $scene")
            append("  ·  ${brainInsight.explanation}")
        }
        _panel.value = _panel.value.copy(
            sessionContext      = context,
            learnedPreferences  = topCmds
        )
    }

    // ── API pública ───────────────────────────────────────────────────────

    /** Inicia escucha por micrófono. */
    fun startListening() = assistant.startListening()

    /** Detiene escucha por micrófono. */
    fun stopListening() = assistant.stopListening()

    /** Procesa texto escrito (pruebas / accesibilidad). */
    fun onTextInput(text: String) {
        _panel.value = _panel.value.copy(phase = AssistantPhase.PROCESSING)
        assistant.onUserSaid(text)
    }

    /**
     * Limpia toda la memoria (FASE 13).
     * Borra ListenerProfile + ContextMemory + historial de LanguageCore.
     * NO borra configuración permanente de audio.
     */
    fun clearMemory() {
        assistant.clearMemory()
        refreshMemoryPanel()
    }

    /**
     * Prueba la conexión con Gemini usando la key actualmente configurada.
     * Lanza un ping real al agente (Dispatchers.IO) y actualiza
     * geminiAvailable + statusLine + errorMessage en el panel.
     *
     * FIX: IvannaAssistantScreen.kt llamaba esta función desde el botón
     * "PROBAR CONEXIÓN" del GeminiConfigPanel, pero no existía en el
     * ViewModel → Unresolved reference: testGeminiConnection → build roto.
     */
    fun testGeminiConnection() {
        viewModelScope.launch {
            _panel.value = _panel.value.copy(
                statusLine   = "Probando conexión…",
                errorMessage = null
            )
            val ok = withContext(Dispatchers.IO) {
                runCatching {
                    val result =
                        geminiAgent.processQuery("ping")

                    when(result) {
                        is com.ivanna.omega.ai.gemini.IvannaGeminiAgent.AgentResponse.Success ->
                            true

                        else ->
                            false
                    }
                }.getOrDefault(false)
            }
            val avail = runCatching {
                com.ivanna.omega.assistant.core.SecureConfigurationManager.state.value.isConfigured
            }.getOrDefault(false)
            _panel.value = _panel.value.copy(
                geminiAvailable = ok && avail,
                statusLine = if (ok && avail) "IVANNA: ✅ conectada"
                             else             "Toca el micrófono y habla con IVANNA.",
                errorMessage = if (!(ok && avail))
                    "IVANNA no respondió — verifica la API Key o la red"
                else null
            )
        }
    }

    override fun onCleared() {
        super.onCleared()
        assistant.release()
    }
}
