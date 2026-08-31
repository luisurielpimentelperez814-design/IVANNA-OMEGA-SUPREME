package com.ivanna.omega.agent

import android.util.Log
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.dsp.DSPBridge
import com.ivanna.omega.magisk.OmegaEngineBridge
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import org.json.JSONArray
import org.json.JSONObject

/**
 * IvannaAgentCore — capa agéntica de IVANNA-OMEGA-SUPREME.
 *
 * Arquitectura (2026-08-31): cinco agentes especializados orquestados sobre
 * los canales REALES ya existentes — ninguno toca el hilo de audio, ninguno
 * duplica el bucle adaptativo rápido (AdaptiveDecisionEngine C++ @50 ms,
 * que sigue siendo la autoridad de control fino muestra-a-muestra). Esta capa
 * opera a cadencia lenta (~1 s): percibe escena, vigila salud, propone y
 * aplica ajustes de nivel de PERFIL (target gain / comp amount / exciter
 * reduction / sala) por los mismos buses que ya usa la UI.
 *
 *   Telemetría nativa (atomics JNI)  ─┐
 *   Yamnet voice score (vía bus)      ├─► AcousticPerceptionAgent (escena)
 *   Clip count / latencia RT          ─┘            │
 *                                                   ▼
 *                                       DecisionAgent (política por escena)
 *                                                   │
 *                                                   ▼
 *                              DspControlAgent (canales seguros existentes:
 *                              OmegaEngineBridge.pushAdaptiveState / setRoom
 *                              + DSPBridge.setStereoWidth in-process)
 *                                                   │
 *              HealthMonitoringAgent ◄── clip/latencia/engine post-cambio
 *                    │
 *                    ▼
 *              OptimizationAgent (reduce ganancia si clipping sostenido,
 *              desactiva RIR si latencia degrada — siempre reversible)
 *
 * Todo cambio queda registrado en un ring buffer de decisiones con su razón
 * — es la base de la explicabilidad ("¿por qué cambiaste este perfil?").
 */
object IvannaAgentCore {

    private const val TAG = "IVANNA.AgentCore"
    private const val CYCLE_MS = 1000L
    private const val MAX_LOG = 64

    // ── Estado público observable (UI / API de agentes externos) ────────────
    private val _state = MutableStateFlow(AgentState())
    val state: StateFlow<AgentState> = _state.asStateFlow()

    private val decisionLog = ArrayDeque<DecisionRecord>(MAX_LOG)

    private var scope: CoroutineScope? = null
    @Volatile private var running = false

    // ══════════════════════════════════════════════════════════════════════
    // Modelos
    // ══════════════════════════════════════════════════════════════════════

    /** Escena acústica estimada desde telemetría real (heurística documentada,
     *  no un clasificador inventado — ver AcousticPerceptionAgent.classify). */
    enum class AcousticScene { SILENCE, VOICE, MUSIC, DYNAMIC, UNKNOWN }

    data class PerceptionSnapshot(
        val scene: AcousticScene = AcousticScene.UNKNOWN,
        val rms: Float = 0f,
        val peak: Float = 0f,
        val crestDb: Float = 0f,
        val voiceScore: Float = 0f,
        val gainReductionDb: Float = 0f,
        val playbackActive: Boolean = false
    )

    data class HealthSnapshot(
        val clipCountDelta: Int = 0,
        val roundTripLatencyUs: Long = 0L,
        val adaptiveEngineRunning: Boolean = false,
        val daemonConnected: Boolean = false,
        val clipping: Boolean = false,
        val latencyDegraded: Boolean = false
    )

    data class DecisionRecord(
        val timestampMs: Long,
        val scene: AcousticScene,
        val action: String,
        val reason: String,
        val applied: Boolean
    )

    data class AgentState(
        val running: Boolean = false,
        val perception: PerceptionSnapshot = PerceptionSnapshot(),
        val health: HealthSnapshot = HealthSnapshot(),
        val activePolicy: String = "neutral",
        val lastAction: String = "—",
        val cycles: Long = 0L
    )

    // ══════════════════════════════════════════════════════════════════════
    // 1. Acoustic Perception Agent — consume telemetría, clasifica escena
    // ══════════════════════════════════════════════════════════════════════
    object AcousticPerceptionAgent {

        fun perceive(): PerceptionSnapshot {
            if (!IvannaNativeLib.isLoaded) return PerceptionSnapshot()
            val t = runCatching { IvannaNativeLib.nativeGetAdaptiveTelemetry() }
                .getOrNull() ?: return PerceptionSnapshot()
            if (t.size < 10) return PerceptionSnapshot()

            // Layout del contrato JNI (nativeGetAdaptiveTelemetry, 10 floats):
            val rms = t[0]; val peak = t[1]; val grDb = t[2]
            val voiceScore = t[8]
            val applied = t[9] > 0.5f

            val crestDb = if (rms > 1e-6f && peak > 1e-6f)
                20f * kotlin.math.log10(peak / rms) else 0f
            val active = rms > 0.001f

            return PerceptionSnapshot(
                scene = classify(rms, peak, crestDb, voiceScore, active),
                rms = rms, peak = peak, crestDb = crestDb,
                voiceScore = voiceScore, gainReductionDb = grDb,
                playbackActive = active || applied
            )
        }

        /**
         * Clasificación de escena desde señales reales disponibles en la app:
         *  - voiceScore: score de voz de Yamnet (ya fluye por el bus nativo)
         *  - crest: peak/RMS — material dinámico (cine/juego) vs comprimido
         *  - rms: nivel — silencio/ambiente vs reproducción activa
         * Es una heurística conservadora, NO un modelo nuevo: las clases
         * finas (música/voz/ruido/transiente) las sigue decidiendo el
         * IvannaAudioClassifier TinyML en el hilo nativo.
         */
        private fun classify(
            rms: Float, peak: Float, crestDb: Float,
            voiceScore: Float, active: Boolean
        ): AcousticScene = when {
            !active -> AcousticScene.SILENCE
            voiceScore > 0.55f -> AcousticScene.VOICE
            crestDb > 14f && rms > 0.02f -> AcousticScene.DYNAMIC
            active -> AcousticScene.MUSIC
            else -> AcousticScene.UNKNOWN
        }
    }

    // ══════════════════════════════════════════════════════════════════════
    // 2. Decision Agent — política por escena. NO toca audio: solo decide.
    // ══════════════════════════════════════════════════════════════════════
    object DecisionAgent {

        data class Policy(
            val name: String,
            val targetGain: Float,     // 0.5..1.0
            val compAmount: Float,     // 0..1
            val excReduction: Float,   // 0..1
            val spatialWidth: Float,   // 0.5..2.0
            val reason: String
        )

        fun decide(p: PerceptionSnapshot, h: HealthSnapshot): Policy = when {
            // La salud manda sobre la escena: clipping sostenido fuerza
            // política de protección sin importar qué suena.
            h.clipping -> Policy(
                "protect",
                targetGain = 0.75f, compAmount = 0.55f, excReduction = 0.5f,
                spatialWidth = 1.0f,
                reason = "clipping sostenido (${h.clipCountDelta} clips/ciclo) — reducir ganancia y levantar compresión"
            )
            p.scene == AcousticScene.VOICE -> Policy(
                "voice-focus",
                targetGain = 1.0f, compAmount = 0.35f, excReduction = 0.35f,
                spatialWidth = 0.85f,
                reason = "voz dominante (score=${"%.2f".format(p.voiceScore)}) — centrar imagen y domar exciter para inteligibilidad"
            )
            p.scene == AcousticScene.DYNAMIC -> Policy(
                "cinematic",
                targetGain = 1.0f, compAmount = 0.15f, excReduction = 0.1f,
                spatialWidth = 1.35f,
                reason = "material dinámico (crest=${"%.1f".format(p.crestDb)} dB) — dejar transientes libres y ensanchar escena"
            )
            p.scene == AcousticScene.MUSIC -> Policy(
                "music-balanced",
                targetGain = 1.0f, compAmount = 0.25f, excReduction = 0.15f,
                spatialWidth = 1.15f,
                reason = "reproducción musical activa — balance neutro con leve apertura espacial"
            )
            p.scene == AcousticScene.SILENCE -> Policy(
                "neutral",
                targetGain = 1.0f, compAmount = 0.0f, excReduction = 0.0f,
                spatialWidth = 1.0f,
                reason = "sin reproducción — política neutra, cero intervención"
            )
            else -> Policy(
                "neutral",
                targetGain = 1.0f, compAmount = 0.0f, excReduction = 0.0f,
                spatialWidth = 1.0f,
                reason = "escena indeterminada — no intervenir"
            )
        }
    }

    // ══════════════════════════════════════════════════════════════════════
    // 3. DSP Control Agent — ejecuta por canales seguros YA existentes.
    //    Nunca escribe parámetros crudos al DSP: solo los 3 knobs adaptativos
    //    (que el motor C++ suaviza por bloque) y el ancho espacial in-process.
    // ══════════════════════════════════════════════════════════════════════
    object DspControlAgent {

        // Histeresis: no reenviar la misma política en cada ciclo.
        private var lastPolicyName: String? = null

        fun apply(policy: DecisionAgent.Policy): Boolean {
            if (policy.name == lastPolicyName) return true  // ya aplicada
            var ok = false

            // Ruta daemon (system-wide, root): los 3 knobs adaptativos.
            if (OmegaEngineBridge.isConnected) {
                ok = runCatching {
                    OmegaEngineBridge.pushAdaptiveState(
                        policy.targetGain, policy.compAmount, policy.excReduction
                    )
                }.getOrDefault(false)
            }

            // Ruta in-process (sin root): ancho espacial suavizado por DSPBridge.
            if (DSPBridge.isLoaded) {
                runCatching { DSPBridge.setStereoWidth(policy.spatialWidth) }
                ok = true
            }

            if (ok) lastPolicyName = policy.name
            return ok
        }

        fun resetHysteresis() { lastPolicyName = null }
    }

    // ══════════════════════════════════════════════════════════════════════
    // 4. Health Monitoring Agent — latencia, clipping, motor, daemon.
    // ══════════════════════════════════════════════════════════════════════
    object HealthMonitoringAgent {

        private var lastClipCount = -1
        // Latencia round-trip > 2 ms en el bridge JNI indica saturación del
        // pipeline; se confirma solo con 3 lecturas consecutivas degradadas.
        private var degradedStreak = 0

        fun check(): HealthSnapshot {
            val clips = if (IvannaNativeLib.isLoaded)
                runCatching { IvannaNativeLib.nativeGetClipCount() }.getOrDefault(0) else 0
            val delta = if (lastClipCount < 0) 0 else (clips - lastClipCount).coerceAtLeast(0)
            lastClipCount = clips

            val latencyUs = if (IvannaNativeLib.isLoaded)
                runCatching { IvannaNativeLib.nativeMeasureRoundTripLatencyUs() }
                    .getOrDefault(0L) else 0L

            val engineRunning = IvannaNativeLib.isLoaded &&
                runCatching { IvannaNativeLib.nativeGetUnifiedPipelineStatus() }
                    .getOrNull()?.let { it.size >= 8 && it[7] > 0.5f } == true

            val daemon = runCatching { OmegaEngineBridge.isConnected }.getOrDefault(false)

            degradedStreak = if (latencyUs > 2000L) degradedStreak + 1 else 0

            return HealthSnapshot(
                clipCountDelta = delta,
                roundTripLatencyUs = latencyUs,
                adaptiveEngineRunning = engineRunning,
                daemonConnected = daemon,
                clipping = delta > 8,               // >8 clips/s = clipping audible
                latencyDegraded = degradedStreak >= 3
            )
        }
    }

    // ══════════════════════════════════════════════════════════════════════
    // 5. Optimization Agent — mejoras automáticas reversibles.
    // ══════════════════════════════════════════════════════════════════════
    object OptimizationAgent {

        /** Devuelve una acción correctiva o null si el sistema está sano. */
        fun propose(h: HealthSnapshot): DecisionAgent.Policy? = when {
            h.latencyDegraded && h.daemonConnected -> DecisionAgent.Policy(
                "latency-relief",
                targetGain = 1.0f, compAmount = 0.2f, excReduction = 0.3f,
                spatialWidth = 1.0f,
                reason = "latencia JNI degradada (${h.roundTripLatencyUs} µs ×3 ciclos) — aligerar carga del bridge"
            )
            h.clipping -> DecisionAgent.Policy(
                "clip-relief",
                targetGain = 0.7f, compAmount = 0.6f, excReduction = 0.6f,
                spatialWidth = 1.0f,
                reason = "optimización automática: clipping sostenido — techo de ganancia 0.7 hasta que ceda"
            )
            else -> null
        }
    }

    // ══════════════════════════════════════════════════════════════════════
    // Orquestador
    // ══════════════════════════════════════════════════════════════════════

    @Synchronized
    fun start() {
        if (running) return
        running = true
        val s = CoroutineScope(SupervisorJob() + Dispatchers.Default)
        scope = s
        s.launch {
            Log.i(TAG, "IvannaAgentCore online — cadencia ${CYCLE_MS} ms")
            while (isActive && running) {
                runCatching { cycle() }
                    .onFailure { Log.w(TAG, "ciclo falló (no fatal): ${it.message}") }
                delay(CYCLE_MS)
            }
        }
    }

    @Synchronized
    fun stop() {
        running = false
        scope = null
        DspControlAgent.resetHysteresis()
    }

    private fun cycle() {
        val cur = _state.value

        val perception = AcousticPerceptionAgent.perceive()
        val health = HealthMonitoringAgent.check()

        // Optimización (salud) tiene prioridad sobre política de escena.
        val policy = OptimizationAgent.propose(health)
            ?: DecisionAgent.decide(perception, health)

        val applied = DspControlAgent.apply(policy)

        if (policy.name != cur.activePolicy || applied && policy.name != cur.lastAction) {
            record(
                DecisionRecord(
                    timestampMs = System.currentTimeMillis(),
                    scene = perception.scene,
                    action = policy.name,
                    reason = policy.reason,
                    applied = applied
                )
            )
        }

        _state.value = AgentState(
            running = true,
            perception = perception,
            health = health,
            activePolicy = policy.name,
            lastAction = if (applied) policy.name else cur.lastAction,
            cycles = cur.cycles + 1
        )
    }

    @Synchronized
    private fun record(r: DecisionRecord) {
        if (decisionLog.size >= MAX_LOG) decisionLog.removeFirst()
        decisionLog.addLast(r)
        Log.i(TAG, "decisión: ${r.action} [${r.scene}] — ${r.reason} (aplicada=${r.applied})")
    }

    /** Historial de decisiones — base de la explicabilidad para agentes externos. */
    @Synchronized
    fun recentDecisions(): List<DecisionRecord> = decisionLog.toList()
}
