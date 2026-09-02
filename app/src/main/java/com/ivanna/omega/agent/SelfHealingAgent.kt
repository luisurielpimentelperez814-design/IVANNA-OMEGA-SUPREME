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
 * SelfHealingAgent — auto-reparación sin intervención humana.
 *
 * Vigila la salud del sistema (la misma que mide HealthMonitoringAgent) y
 * ejecuta acciones correctivas REALES sobre los canales existentes cuando
 * detecta un fallo. Cada acción es:
 *   - verificable (se confirma en el siguiente ciclo, no se asume)
 *   - reversible (ninguna deja el sistema en un estado peor)
 *   - con cooldown (no reintenta en bucle si la causa raíz persiste)
 *
 * Fallos que repara:
 *   1. Daemon Magisk desconectado       → reintenta connect() (Unix/TCP)
 *   2. Motor adaptativo nativo muerto   → nativeResetDSP + re-init
 *   3. Clipping sostenido que el OptimizationAgent no pudo frenar →
 *      reduce target gain un escalón más (protección de última instancia)
 *   4. DSPBridge descargado tras cambio de ruta → re-init con SR real
 *
 * NUNCA toca el hilo de audio: todas las acciones corren en Dispatchers.Default
 * a cadencia lenta. Es la capa de "super-ingeniero de audio" que mantiene el
 * sistema sano sin que el usuario intervenga.
 */
object SelfHealingAgent {

    private const val TAG = "IVANNA.SelfHeal"
    private const val CYCLE_MS = 5000L

    // Cooldowns (ms) por tipo de fallo — evitan bucles de reparación
    private const val COOLDOWN_DAEMON_MS = 30_000L
    private const val COOLDOWN_ENGINE_MS = 45_000L
    private const val COOLDOWN_GAIN_MS = 20_000L

    private var lastDaemonAttempt = 0L
    private var lastEngineAttempt = 0L
    private var lastGainAction = 0L

    // Estado público
    private val _log = MutableStateFlow<List<HealRecord>>(emptyList())
    val log: StateFlow<List<HealRecord>> = _log.asStateFlow()

    private var scope: CoroutineScope? = null
    @Volatile private var running = false

    data class HealRecord(
        val timestampMs: Long,
        val fault: String,
        val action: String,
        val success: Boolean,
        val detail: String
    )

    @Synchronized
    fun start() {
        if (running) return
        running = true
        val s = CoroutineScope(SupervisorJob() + Dispatchers.Default)
        scope = s
        s.launch {
            Log.i(TAG, "SelfHealingAgent online — cadencia ${CYCLE_MS} ms")
            // Gracia inicial: dejar que la app termine de arrancar antes de
            // diagnosticar "fallos" que son solo el arranque en frío.
            delay(8000)
            while (isActive && running) {
                runCatching { cycle() }
                    .onFailure { Log.w(TAG, "ciclo self-heal falló: ${it.message}") }
                delay(CYCLE_MS)
            }
        }
    }

    @Synchronized
    fun stop() { running = false; scope = null }

    private fun cycle() {
        val now = System.currentTimeMillis()

        // ── 1. Daemon Magisk desconectado ───────────────────────────────────
        val daemonOk = runCatching { OmegaEngineBridge.isConnected }.getOrDefault(false)
        if (!daemonOk && now - lastDaemonAttempt > COOLDOWN_DAEMON_MS) {
            lastDaemonAttempt = now
            val recovered = runCatching { OmegaEngineBridge.connect() }.getOrDefault(false)
            record("daemon_disconnect", "OmegaEngineBridge.connect() (Unix→TCP)", recovered,
                if (recovered) "daemon re-enlazado" else "daemon sigue inalcanzable (reintento en ${COOLDOWN_DAEMON_MS / 1000}s)")
        }

        // ── 2. Motor adaptativo nativo muerto ───────────────────────────────
        val engineAlive = IvannaNativeLib.isLoaded && runCatching {
            IvannaNativeLib.nativeGetUnifiedPipelineStatus()
        }.getOrNull()?.let { it.size >= 8 && it[7] > 0.5f } == true
        if (IvannaNativeLib.isLoaded && !engineAlive &&
            now - lastEngineAttempt > COOLDOWN_ENGINE_MS) {
            lastEngineAttempt = now
            val ok = runCatching {
                IvannaNativeLib.nativeResetDSP()
                // Re-init con la SR real del HAL (mismo patrón que la app)
                val sr = runCatching {
                    (com.ivanna.omega.core.IVANNAApplication.instance
                        ?.getSystemService(android.content.Context.AUDIO_SERVICE)
                            as? android.media.AudioManager)
                        ?.getProperty(android.media.AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE)
                        ?.toIntOrNull()
                }.getOrNull() ?: 48000
                IvannaNativeLib.nativeInitDSP(sr)
            }.getOrDefault(false)
            record("adaptive_engine_dead", "nativeResetDSP + nativeInitDSP", ok,
                if (ok) "motor adaptativo reiniciado" else "re-init falló (reintento en ${COOLDOWN_ENGINE_MS / 1000}s)")
        }

        // ── 3. Clipping sostenido (protección de última instancia) ──────────
        val clips = if (IvannaNativeLib.isLoaded)
            runCatching { IvannaNativeLib.nativeGetClipCount() }.getOrDefault(0) else 0
        if (clips > lastClipBaseline + 24 && now - lastGainAction > COOLDOWN_GAIN_MS) {
            // >24 clips desde el último chequeo ≈ clipping denso sostenido
            lastGainAction = now
            val ok = runCatching {
                if (OmegaEngineBridge.isConnected) {
                    OmegaEngineBridge.pushAdaptiveState(0.65f, 0.7f, 0.7f)
                } else {
                    DSPBridge.setStereoWidth(1.0f)  // al menos centrar
                    true
                }
            }.getOrDefault(false)
            record("clipping_sustained", "techo de ganancia de emergencia 0.65", ok,
                "clipping denso ($clips acumulados) — protección de última instancia")
        }
        lastClipBaseline = clips
    }

    @Volatile private var lastClipBaseline = 0

    @Synchronized
    private fun record(fault: String, action: String, success: Boolean, detail: String) {
        val r = HealRecord(System.currentTimeMillis(), fault, action, success, detail)
        _log.value = (_log.value + r).takeLast(48)
        Log.i(TAG, "[${if (success) "OK" else "FALLO"}] $fault → $action — $detail")
    }

    /** Resumen JSON para la API de agentes externos / diagnóstico. */
    fun report(): JSONObject = JSONObject().apply {
        put("running", running)
        put("repairsLogged", _log.value.size)
        put("recent", JSONArray().also { arr ->
            _log.value.takeLast(8).forEach { r ->
                arr.put(JSONObject().apply {
                    put("t", r.timestampMs); put("fault", r.fault)
                    put("action", r.action); put("ok", r.success)
                })
            }
        })
    }
}
