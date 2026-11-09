package com.ivanna.omega.agent

import android.content.Context
import android.util.Log
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.dsp.DSPBridge
import com.ivanna.omega.magisk.OmegaEngineBridge
import com.ivanna.omega.audio.AudioStateManager
import com.ivanna.omega.audio.OmegaMetrics
import com.ivanna.omega.audio.PlaybackCaptureService
import com.ivanna.omega.assistant.core.DynamicContextEngine
import com.ivanna.omega.core.IVANNAApplication
import com.ivanna.omega.ui.AdaptiveControlsPrefs
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
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
 *   - autorizada (cada reparación pasa por IvannaDecisionAuthority: las
 *     reparaciones son decisiones AUTÓNOMAS por diseño, pero quedan
 *     registradas y el usuario puede vetarlas con markUndone()).
 *
 * Fallos que repara:
 *   1. Daemon Magisk desconectado       → reintenta connect() (Unix/TCP)
 *   2. Motor adaptativo nativo muerto   → nativeResetDSP + re-init
 *   3. Clipping sostenido que el OptimizationAgent no pudo frenar →
 *      reduce target gain un escalón más (protección de última instancia).
 *      El piso de ganancia EVOLUCIONA (BehaviorEvolution): si el corte no
 *      frena los clips dos veces seguidas, baja un paso (más protector);
 *      si vuelve a funcionar, sube hacia el default. Verificado en el
 *      ciclo siguiente (crecimiento de clips), no asumido.
 *   4. DSP sin inicializar con captura viva → DSPBridge.init() con el SR
 *      real (el caso que el docstring prometía y no existía). Señal:
 *      captura corriendo 3 ciclos seguidos con activeRoute==0.
 *   5. Captura perdida (true→false) → NO reinicia a ciegas: la
 *      MediaProjection invalidada exige consentimiento nuevo del usuario
 *      (el servicio ya reintenta 6 veces internamente). Registra el fallo
 *      con la guía de acción para el usuario.
 *   6. Térmica severa/crítica → mitigación segura (exciter 0.8, ancho 1.0,
 *      ganancia adaptativa 0.8) por los canales existentes; nunca toca el
 *      throttling térmico del sistema (PROHIBIDO).
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
    private const val COOLDOWN_DSP_MS = 60_000L
    private const val COOLDOWN_CAPTURE_MS = 60_000L
    private const val COOLDOWN_THERMAL_MS = 120_000L

    // El log persiste entre reinicios (SharedPreferences, máx. 48 registros)
    // para que el historial de reparaciones sobreviva y alimente el prompt
    // de Gemini (salud del sistema).
    private const val LOG_PREFS = "ivanna_selfheal_log"
    private const val LOG_KEY = "heal_log"
    private const val MAX_LOG = 48

    private var lastDaemonAttempt = 0L
    private var lastEngineAttempt = 0L
    private var lastGainAction = 0L
    private var lastDspAttempt = 0L
    private var lastCaptureNote = 0L
    private var lastThermalAction = 0L

    // ── Estado de verificación post-reparación ─────────────────────────────
    // Clipping: (clips en el momento del corte, instante). En el ciclo
    // siguiente se mide el crecimiento real: <24 = contenido, ≥24 = sigue.
    private var pendingClipCheck: Pair<Int, Long>? = null
    private var consecutiveClipFailures = 0
    // DSP: true tras intentar re-init; se verifica en los ciclos siguientes.
    private var pendingDspVerification = false
    private var pendingDspAt = 0L
    // Ruta: ciclos consecutivos con captura viva y activeRoute==0.
    private var consecutiveNoRoute = 0
    // Captura: transición true→false (pérdida, no pausa: solo si capturaba).
    private var wasCapturing = false
    // Térmica: mitigación ya aplicada (no re-aplicar cada ciclo en caliente).
    private var thermalMitigationActive = false

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
        runCatching {
            val ctx = IVANNAApplication.instance
            BehaviorEvolution.init(ctx)
            IvannaDecisionAuthority.init(ctx)
            loadPersistedLog(ctx)
        }
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
    fun stop() {
        running = false
        // FIX (auditoria 2026-09-10, flanco capa de agente): mismo bug que
        // IvannaAgentCore.stop() — scope=null sin cancel() dejaba la corrutina
        // viva; un toggle rapido start/stop/start creaba dos bucles cycle()
        // concurrentes (ambos con isActive=true y running compartido @Volatile).
        scope?.cancel()
        scope = null
    }

    private fun cycle() {
        val now = System.currentTimeMillis()

        // ── 0. Verificaciones post-reparación del ciclo anterior ────────────
        verifyPendingClip(now)
        verifyPendingDsp(now)

        // Señales compartidas por varios chequeos (una sola lectura por ciclo).
        val clips = if (IvannaNativeLib.isLoaded)
            runCatching { IvannaNativeLib.nativeGetClipCount() }.getOrDefault(0) else 0
        val capturing = runCatching { PlaybackCaptureService.isCapturing.value }.getOrDefault(false)
        val route = if (IvannaNativeLib.isLoaded) runCatching {
            IvannaNativeLib.nativeGetUnifiedPipelineStatus()?.getOrNull(0) ?: 0f
        }.getOrDefault(0f) else 0f

        // ── 1. Daemon Magisk desconectado ───────────────────────────────────
        val daemonOk = runCatching { OmegaEngineBridge.isConnected }.getOrDefault(false)
        if (!daemonOk && now - lastDaemonAttempt > COOLDOWN_DAEMON_MS) {
            lastDaemonAttempt = now
            val auth = IvannaDecisionAuthority.authorize("repair_daemon_reconnect")
            if (auth.verdict == IvannaDecisionAuthority.Verdict.ALLOWED) {
                val recovered = runCatching { OmegaEngineBridge.connect() }.getOrDefault(false)
                IvannaDecisionAuthority.recordDecision(
                    "repair_daemon_reconnect", emptyMap(), recovered,
                    if (recovered) "daemon re-enlazado" else "daemon sigue inalcanzable"
                )
                record("daemon_disconnect", "OmegaEngineBridge.connect() (Unix→TCP)", recovered,
                    if (recovered) "daemon re-enlazado" else "daemon sigue inalcanzable (reintento en ${COOLDOWN_DAEMON_MS / 1000}s)")
            } else {
                record("daemon_disconnect", "reconexión no autorizada (${auth.reason})", false, auth.reason)
            }
        }

        // ── 2. Motor adaptativo: reparar solo si el usuario lo solicitó ──
        // La falta de bloques no demuestra que el hilo haya muerto: puede ser
        // silencio o pausa. La salud del hilo se consulta directamente; la
        // evidencia de bloques aplicados permanece como telemetría separada.
        val adaptiveRequested = runCatching {
            AdaptiveControlsPrefs.load(IVANNAApplication.instance).binauralAdaptEnabled &&
                !AudioStateManager.audioState.value.manualModeEnabled
        }.getOrDefault(false)
        val engineRunning = if (IvannaNativeLib.isLoaded) runCatching {
            IvannaNativeLib.nativeIsAdaptiveEngineRunning()
        }.getOrDefault(false) else false

        if (IvannaNativeLib.isLoaded && adaptiveRequested &&
            !engineRunning && now - lastEngineAttempt > COOLDOWN_ENGINE_MS) {
            lastEngineAttempt = now
            val auth = IvannaDecisionAuthority.authorize("repair_ade_restart")
            if (auth.verdict == IvannaDecisionAuthority.Verdict.ALLOWED) {
                val handle = runCatching {
                    IvannaNativeLib.nativeCreateAdaptiveEngine()
                }.getOrDefault(0L)
                val recovered = handle != 0L &&
                    runCatching { IvannaNativeLib.nativeIsAdaptiveEngineRunning() }.getOrDefault(false)
                IvannaDecisionAuthority.recordDecision(
                    "repair_ade_restart", emptyMap(), recovered,
                    if (recovered) "hilo de control ADE confirmado vivo" else "ADE no pudo iniciarse"
                )
                record(
                    "adaptive_engine_inactive_on_active_route",
                    "nativeCreateAdaptiveEngine()",
                    recovered,
                    if (recovered)
                        "hilo de control ADE confirmado vivo; el DSP no fue reiniciado"
                    else
                        "ADE no pudo iniciarse; preferencia preservada y DSP intacto, reintento en ${COOLDOWN_ENGINE_MS / 1000}s"
                )
            } else {
                record("adaptive_engine_inactive_on_active_route",
                    "reinicio no autorizado (${auth.reason})", false, auth.reason)
            }
        }

        // ── 3. Clipping sostenido (protección de última instancia) ──────────
        if (clips > lastClipBaseline + 24 && now - lastGainAction > COOLDOWN_GAIN_MS) {
            // >24 clips desde el último chequeo ≈ clipping denso sostenido
            lastGainAction = now
            val floor = BehaviorEvolution.get(BehaviorEvolution.CLIP_GAIN_FLOOR)
            val auth = IvannaDecisionAuthority.authorize(
                "repair_clip_gain_cut", mapOf("targetGain" to floor)
            )
            if (auth.verdict == IvannaDecisionAuthority.Verdict.ALLOWED) {
                val ok = runCatching {
                    var applied = false
                    if (OmegaEngineBridge.isConnected) {
                        applied = OmegaEngineBridge.pushAdaptiveState(floor, 0.7f, 0.7f)
                    }
                    if (DSPBridge.isLoaded) {
                        DSPBridge.applyPerceptualGain(floor)
                        DSPBridge.applyExciterReduction(0.7f)
                        DSPBridge.setStereoWidth(1.0f)
                        applied = true
                    }
                    applied
                }.getOrDefault(false)
                // La verificación real llega en el ciclo siguiente (ver
                // verifyPendingClip): el piso evoluciona según el resultado.
                if (ok) pendingClipCheck = clips to now
                IvannaDecisionAuthority.recordDecision(
                    "repair_clip_gain_cut", mapOf("targetGain" to floor), ok,
                    "clipping denso ($clips acumulados), piso de ganancia $floor"
                )
                record("clipping_sustained", "techo de ganancia de emergencia $floor", ok,
                    "clipping denso ($clips acumulados) — protección de última instancia")
            } else {
                record("clipping_sustained",
                    "corte no autorizado (${auth.reason})", false, auth.reason)
            }
        }
        lastClipBaseline = clips

        // ── 4. DSP sin inicializar con captura viva (ruta sin ruta) ─────────
        // Captura corriendo pero activeRoute==0: el grafo nativo nunca se
        // inicializó (o se perdió tras un cambio de ruta). La reparación es
        // DSPBridge.init() con el SR real — idempotente y fail-closed por
        // IntegrityGate dentro de init(). Solo actúa con captura viva para
        // no confundir "sin audio" (pausa/standby) con "DSP roto".
        if (capturing && route == 0f) consecutiveNoRoute++ else consecutiveNoRoute = 0
        if (capturing && route == 0f && consecutiveNoRoute >= 3 &&
            !pendingDspVerification && now - lastDspAttempt > COOLDOWN_DSP_MS) {
            lastDspAttempt = now
            val auth = IvannaDecisionAuthority.authorize("repair_dsp_reinit")
            if (auth.verdict == IvannaDecisionAuthority.Verdict.ALLOWED) {
                val sr = runCatching { OmegaMetrics.shared.value.sampleRate }.getOrDefault(48000)
                val ok = runCatching { DSPBridge.init(sr); true }.getOrDefault(false)
                if (ok) {
                    pendingDspVerification = true
                    pendingDspAt = now
                }
                IvannaDecisionAuthority.recordDecision(
                    "repair_dsp_reinit", mapOf("sampleRate" to sr.toFloat()), ok,
                    "captura viva $consecutiveNoRoute ciclos sin ruta activa; re-init con SR $sr"
                )
                record("dsp_no_route_with_live_capture", "DSPBridge.init(SR=$sr)", ok,
                    if (ok) "re-init solicitado; verificación en los próximos ciclos"
                    else "re-init falló (ver IntegrityGate / estado de la librería)")
            } else {
                record("dsp_no_route_with_live_capture",
                    "re-init no autorizado (${auth.reason})", false, auth.reason)
            }
        }

        // ── 5. Captura perdida (transición true→false) ───────────────────────
        // NO se reinicia a ciegas: una MediaProjection invalidada por el
        // sistema exige consentimiento NUEVO del usuario (el servicio ya
        // reintenta 6 veces internamente y se rinde honestamente). El
        // self-heal detecta, registra y guía; la acción es del usuario.
        if (wasCapturing && !capturing && now - lastCaptureNote > COOLDOWN_CAPTURE_MS) {
            lastCaptureNote = now
            IvannaDecisionAuthority.recordDecision(
                "repair_capture_guidance", emptyMap(), false,
                "captura perdida; requiere conceder de nuevo el permiso"
            )
            record("capture_stopped",
                "aviso: conceder de nuevo el permiso de captura desde la app",
                false,
                "la captura se detuvo; el servicio ya reintentó internamente. " +
                    "Sin MediaProjection nueva no se puede reiniciar sola.")
        }
        wasCapturing = capturing

        // ── 6. Térmica severa/crítica (mitigación segura) ────────────────────
        // Solo reduce carga por los canales existentes (exciter, ancho,
        // ganancia adaptativa). NUNCA toca el throttling térmico del sistema
        // ni desactiva protecciones (PROHIBIDO en la matriz de autoridad).
        val thermal = runCatching {
            DynamicContextEngine.getInstance()?.systemContext?.value?.thermalStatus
        }.getOrNull()
        if (thermal in setOf("severe", "critical", "emergency")) {
            if (!thermalMitigationActive && now - lastThermalAction > COOLDOWN_THERMAL_MS) {
                lastThermalAction = now
                val auth = IvannaDecisionAuthority.authorize("repair_thermal_mitigation")
                if (auth.verdict == IvannaDecisionAuthority.Verdict.ALLOWED) {
                    val ok = runCatching {
                        var applied = false
                        if (OmegaEngineBridge.isConnected) {
                            applied = OmegaEngineBridge.pushAdaptiveState(0.8f, 0.8f, 0.8f)
                        }
                        if (DSPBridge.isLoaded) {
                            DSPBridge.applyExciterReduction(0.8f)
                            DSPBridge.setStereoWidth(1.0f)
                            applied = true
                        }
                        // Verificación: el DSP sigue respondiendo tras el cambio.
                        applied && IvannaNativeLib.nativeGetUnifiedPipelineStatus() != null
                    }.getOrDefault(false)
                    thermalMitigationActive = ok
                    IvannaDecisionAuthority.recordDecision(
                        "repair_thermal_mitigation", emptyMap(), ok,
                        "térmica $thermal: carga reducida (exciter 0.8, ancho 1.0)"
                    )
                    record("thermal_$thermal",
                        "mitigación: exciter 0.8, ancho 1.0, ganancia adaptativa 0.8", ok,
                        if (ok) "carga reducida; el throttling del sistema sigue activo"
                        else "no se pudo aplicar la mitigación")
                } else {
                    record("thermal_$thermal",
                        "mitigación no autorizada (${auth.reason})", false, auth.reason)
                }
            }
        } else {
            // Térmica normalizada: permitir una futura mitigación si vuelve a subir.
            thermalMitigationActive = false
        }
    }

    /** Verificación post-reparación del corte anti-clipping del ciclo anterior. */
    private fun verifyPendingClip(now: Long) {
        val pending = pendingClipCheck ?: return
        if (now - pending.second < CYCLE_MS) return
        pendingClipCheck = null
        val clips = if (IvannaNativeLib.isLoaded)
            runCatching { IvannaNativeLib.nativeGetClipCount() }.getOrDefault(pending.first) else pending.first
        val growth = clips - pending.first
        if (growth < 24) {
            consecutiveClipFailures = 0
            BehaviorEvolution.adaptOnSuccess(BehaviorEvolution.CLIP_GAIN_FLOOR)
            record("clipping_verification", "crecimiento de clips tras el corte", true,
                "contenido: +$growth clips en el ciclo siguiente (umbral 24)")
        } else {
            consecutiveClipFailures++
            record("clipping_verification", "crecimiento de clips tras el corte", false,
                "sigue creciendo: +$growth clips (fallo consecutivo $consecutiveClipFailures)")
            if (consecutiveClipFailures >= 2) {
                consecutiveClipFailures = 0
                val floorBefore = BehaviorEvolution.get(BehaviorEvolution.CLIP_GAIN_FLOOR)
                BehaviorEvolution.adaptOnRepeatedFailure(BehaviorEvolution.CLIP_GAIN_FLOOR)
                val floorAfter = BehaviorEvolution.get(BehaviorEvolution.CLIP_GAIN_FLOOR)
                IvannaDecisionAuthority.recordDecision(
                    "evolution_step",
                    mapOf("clip_gain_floor" to floorAfter), floorAfter < floorBefore,
                    "piso de ganancia anti-clip $floorBefore → $floorAfter tras 2 verificaciones fallidas"
                )
                record("clip_floor_evolved", "BehaviorEvolution.adaptOnRepeatedFailure()", floorAfter < floorBefore,
                    "piso de ganancia anti-clip $floorBefore → $floorAfter (más protector)")
            }
        }
    }

    /** Verificación post-reparación del re-init del DSP. */
    private fun verifyPendingDsp(now: Long) {
        if (!pendingDspVerification) return
        val route = if (IvannaNativeLib.isLoaded) runCatching {
            IvannaNativeLib.nativeGetUnifiedPipelineStatus()?.getOrNull(0) ?: 0f
        }.getOrDefault(0f) else 0f
        if (route != 0f) {
            pendingDspVerification = false
            consecutiveNoRoute = 0
            record("dsp_route_restored", "verificación tras DSPBridge.init()", true,
                "ruta activa=$route tras el re-init")
        } else if (now - pendingDspAt > 30_000L) {
            pendingDspVerification = false
            record("dsp_route_restored", "verificación tras DSPBridge.init()", false,
                "sigue sin ruta activa 30 s después del re-init (revisar IntegrityGate / ruta)")
        }
    }

    @Volatile private var lastClipBaseline = 0

    @Synchronized
    private fun record(fault: String, action: String, success: Boolean, detail: String) {
        val r = HealRecord(System.currentTimeMillis(), fault, action, success, detail)
        _log.value = (_log.value + r).takeLast(MAX_LOG)
        persistLog()
        Log.i(TAG, "[${if (success) "OK" else "FALLO"}] $fault → $action — $detail")
    }

    /** Persiste el log de reparaciones (sobrevive a reinicios). */
    private fun persistLog() {
        runCatching {
            val ctx = IVANNAApplication.instance
            val arr = JSONArray()
            _log.value.forEach { r ->
                arr.put(JSONObject().apply {
                    put("t", r.timestampMs); put("fault", r.fault)
                    put("action", r.action); put("ok", r.success); put("detail", r.detail)
                })
            }
            ctx.getSharedPreferences(LOG_PREFS, Context.MODE_PRIVATE)
                .edit().putString(LOG_KEY, arr.toString()).apply()
        }
    }

    private fun loadPersistedLog(context: Context) {
        runCatching {
            val raw = context.getSharedPreferences(LOG_PREFS, Context.MODE_PRIVATE)
                .getString(LOG_KEY, null) ?: return
            val arr = JSONArray(raw)
            val records = List(arr.length()) { i ->
                val o = arr.getJSONObject(i)
                HealRecord(
                    o.optLong("t", 0L), o.optString("fault", "?"),
                    o.optString("action", "?"), o.optBoolean("ok", false),
                    o.optString("detail", "")
                )
            }
            _log.value = records.takeLast(MAX_LOG)
            Log.i(TAG, "Log de reparaciones restaurado: ${records.size} registros")
        }
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
