package com.ivanna.omega.audio

import android.content.Context
import android.util.Log
import com.ivanna.omega.core.RootAccess
import com.ivanna.omega.dsp.DSPBridge
import com.ivanna.omega.magisk.MagiskBridge
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

/**
 * AudioBackendSelector — elige y arranca el backend de audio segun el
 * entorno real del dispositivo, y lo re-evalua si el entorno cambia.
 *
 * PROBLEMA QUE ARREGLA (sub-entorno desconectado):
 *   NoRootAudioProcessor.kt existia pero NO estaba referenciado en NINGUN
 *   archivo del repo (grep NoRootAudioProcessor = 0 hits fuera de su propio
 *   archivo). Es decir: en un dispositivo SIN root la app cargaba, la UI se
 *   pintaba y los sliders escribian a un daemon que no existe
 *   (MagiskBridge.sendCommand -> "" offline sentinel, OmegaEngineBridge no conectado),
 *   sin ningun camino de procesado alternativo. La app "funcionaba" en
 *   pantalla y no hacia absolutamente nada al audio.
 *
 * MODOS:
 *   ROOT_DAEMON  — root + daemon del modulo Magisk respondiendo por socket.
 *                  Procesado system-wide en libomega_effect (camino elite).
 *   ROOT_NO_DAEMON — hay root pero el daemon no responde (modulo no instalado
 *                  o late_start pendiente): se usa el camino AudioEffect y se
 *                  sigue reintentando el socket.
 *   NO_ROOT      — sin root: AudioEffect por sesion (IvannaGlobalEffectManager)
 *                  + NoRootAudioProcessor (DynamicsProcessing API 28+).
 */
object AudioBackendSelector {

    private const val TAG = "IVANNA-BackendSel"

    enum class Mode { UNKNOWN, ROOT_DAEMON, ROOT_NO_DAEMON, NO_ROOT }

    private val _mode = MutableStateFlow(Mode.UNKNOWN)
    val mode: StateFlow<Mode> = _mode.asStateFlow()

    /**
     * Estado REAL del efecto omega_effect en audioserver (beacon). "Daemon vivo" NO implica "efecto
     * procesando" (AGENT_CLAIMS 2026-09-24); esto es la senal que faltaba. Por ahora SOLO
     * observabilidad: no cambia que backend se elige, porque no esta verificado en dispositivo que
     * reactivar el fallback con efecto ausente no duplique el procesado (eco/desface).
     * UNAVAILABLE = beacon ilegible (DAC/SELinux) -> no se afirma nada.
     *
     * ACTUALIZADO (Ruta B perfecta, v2.5.369): el beacon SÍ cambia el backend ahora.
     * ROOT_DAEMON exige efecto confirmado (ENABLED_IDLE o PROCESSING); sin
     * confirmación el modo es ROOT_NO_DAEMON y el fallback local SIGUE
     * procesando, para que el audio nunca quede sin IVANNA. El riesgo de
     * doble procesado no existe mientras el efecto no está insertado: no hay
     * nada que duplicar, porque Ruta B no está procesando nada.
     */
    private val _effectState = MutableStateFlow(DSPBridge.EffectState.UNAVAILABLE)
    val effectState: StateFlow<DSPBridge.EffectState> = _effectState.asStateFlow()

    /**
     * Ruta B confirmada: el efecto omega_effect está insertado/procesando
     * (beacon ENABLED_IDLE/PROCESSING). Es la señal que autoriza detener el
     * fallback local. "Daemon vivo" por sí solo NO la confirma.
     */
    private val _routeBConfirmed = MutableStateFlow(false)
    val routeBConfirmed: StateFlow<Boolean> = _routeBConfirmed.asStateFlow()

    /**
     * FUSIÓN MAGISTRAL confirmada (v2.5.401, ABI v12): el cerebro (daemon v2)
     * escribe el heartbeat en /data/local/tmp y el JNI lo lee. true = el HAL
     * ya está procesando TODO el audio (ASRC v6 + campo Ψ). Igual que Ruta B
     * confirmada, autoriza detener el fallback local (Ruta A): si la fusión
     * procesa y la Ruta A también, el audio se procesa dos veces.
     */
    private val _fusionActive = MutableStateFlow(false)
    val fusionActive: StateFlow<Boolean> = _fusionActive.asStateFlow()

    /** Último estado completo de la fusión (para la UI de diagnóstico). */
    private val _fusionState = MutableStateFlow<DSPBridge.FusionState?>(null)
    val fusionState: StateFlow<DSPBridge.FusionState?> = _fusionState.asStateFlow()

    /** Daemon respondiendo por socket (última evaluación). */
    private val _daemonAlive = MutableStateFlow(false)
    val daemonAlive: StateFlow<Boolean> = _daemonAlive.asStateFlow()

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    @Volatile private var appContext: Context? = null

    @Volatile private var noRoot: NoRootAudioProcessor? = null
    @Volatile private var started = false

    val isNoRootFallbackActive: Boolean get() = noRoot?.isActive() == true

    /** Idempotente: seguro llamarlo varias veces. */
    fun start(context: Context) {
        if (started) return
        started = true
        val app = context.applicationContext
        appContext = app
        scope.launch {
            evaluate(app)
            // Re-evaluacion periodica: el daemon del modulo puede subir tarde
            // (Magisk late_start_service) o caerse en caliente. Sin esto, un
            // dispositivo rooteado que arranca la app antes del daemon se
            // quedaba clavado en el fallback para toda la sesion.
            while (true) {
                delay(10_000L)
                evaluate(app)
            }
        }
    }

    /**
     * La app creó el efecto Omega en una sesión (IvannaGlobalEffectManager).
     * Re-evalúa el backend DE INMEDIATO (sin esperar al ciclo de 10 s) para
     * que, si el beacon ya confirma el efecto, el fallback se detenga pronto
     * y no haya doble procesado. Si el beacon aún no lo confirma, el modo se
     * queda en ROOT_NO_DAEMON y el fallback sigue (fail-closed).
     */
    fun notifyOmegaAttached() {
        val app = appContext ?: return
        scope.launch { evaluate(app) }
    }

    /** Decisión pura (probable en host): ¿Ruta B está confirmada? */
    internal fun isRouteBConfirmed(effect: DSPBridge.EffectState): Boolean =
        effect == DSPBridge.EffectState.ENABLED_IDLE || effect == DSPBridge.EffectState.PROCESSING

    /**
     * Decisión pura del modo (probable en host). ROOT_DAEMON exige las tres:
     * root + daemon vivo + Ruta B confirmada (efecto insertado/procesando)
     * O fusión confirmada (el HAL procesa TODO el audio con ASRC v6 + Ψ).
     * Con daemon vivo pero ni Ruta B ni fusión confirmadas, el modo es
     * ROOT_NO_DAEMON y el fallback local SIGUE procesando (fail-closed).
     * La fusión vale como confirmación porque, igual que Ruta B, significa
     * que el audio ya se procesa fuera de la app: si la Ruta A también
     * procesara, habría doble procesado.
     */
    internal fun decideMode(root: Boolean, daemon: Boolean, routeBConfirmed: Boolean, fusionActive: Boolean = false): Mode = when {
        root && daemon && (routeBConfirmed || fusionActive) -> Mode.ROOT_DAEMON
        root && daemon -> Mode.ROOT_NO_DAEMON
        root -> Mode.ROOT_NO_DAEMON
        else -> Mode.NO_ROOT
    }

    /**
     * Resumen legible del estado de Ruta B para diagnóstico (UI/logcat).
     * Sin I/O: solo refleja los StateFlow actuales. Para el detalle del
     * daemon (consumer, dsp_frames, generaciones) usar
     * OmegaEngineBridge.requestTelemetry().
     */
    fun routeBStatusLine(): String {
        val m = _mode.value
        val d = _daemonAlive.value
        val e = _effectState.value
        val c = _routeBConfirmed.value
        val f = _fusionActive.value
        val fs = _fusionState.value
        val fusionTxt = if (f && fs != null) " fusión=ACTIVA η=${"%.2f".format(fs.eta)} Ψ=${if (fs.psiActive) "sí" else "no"} deriva=${"%.1f".format(fs.driftPpm)}ppm" else if (f) " fusión=ACTIVA" else ""
        return "RutaB modo=$m daemon=${if (d) "vivo" else "caído"} efecto=$e confirmada=${if (c) "sí" else "no"}$fusionTxt" +
            if (m == Mode.ROOT_NO_DAEMON && d) " (daemon responde pero efecto no creado/procesando — ver docs/RUTA_B_DAEMON_FIX.md)" else ""
    }

    private fun refreshEffectState(daemon: Boolean) {
        val fx = DSPBridge.effectState()
        val prev = _effectState.value
        if (fx == prev) return
        _effectState.value = fx
        Log.i(TAG, "efecto omega_effect: $prev -> $fx (daemon=$daemon)")
        if (daemon && fx == DSPBridge.EffectState.NO_EFFECT) {
            Log.w(TAG, "daemon vivo pero omega_effect NO esta insertado/habilitado en audioserver: " +
                "Ruta B no esta sonando (con audio activo esto significa IVANNA sin procesar)")
        }
    }

    private fun evaluate(app: Context) {
        val daemon = runCatching { MagiskBridge.isDaemonRunning }.getOrDefault(false)
        _daemonAlive.value = daemon
        refreshEffectState(daemon)
        val root = if (daemon) true else RootAccess.probeSu()
        // Ruta B confirmada solo si el efecto está insertado/procesando
        // (beacon). "Daemon vivo" NO basta: sin consumidor el daemon acepta
        // comandos pero no procesa nada (applied:false, dsp_frames:0). Sin
        // confirmación el modo es ROOT_NO_DAEMON y el fallback local SIGUE
        // procesando (Ruta A) para que el audio nunca quede sin IVANNA.
        // Solo con Ruta B confirmada se detiene el fallback (evita doble
        // procesado). Si el beacon es ilegible (UNAVAILABLE) no se afirma
        // nada: se conserva el fallback (fail-closed).
        val confirmed = isRouteBConfirmed(_effectState.value)
        _routeBConfirmed.value = confirmed
        // FUSIÓN MAGISTRAL (v2.5.401): el cerebro escribe el heartbeat en
        // /data/local/tmp y el JNI lo lee. Si la fusión está activa, el HAL
        // ya procesa TODO el audio: la Ruta A debe ceder (igual que con
        // Ruta B confirmada) para no procesar dos veces. Si no hay fusión
        // (null / heartbeat obsoleto) se conserva el fallback (fail-closed).
        val fusion = runCatching { DSPBridge.fusionState() }.getOrNull()
        _fusionState.value = fusion
        val fusionOn = fusion?.active == true
        _fusionActive.value = fusionOn
        val next = decideMode(root, daemon, confirmed, fusionOn)
        if (next == _mode.value) return
        Log.i(TAG, "modo de audio: ${_mode.value} -> $next (root=$root daemon=$daemon routeB=$confirmed fusion=$fusionOn effect=${_effectState.value})")
        _mode.value = next
        applyMode(app, next)
    }

    private fun applyMode(app: Context, mode: Mode) {
        when (mode) {
            Mode.NO_ROOT, Mode.ROOT_NO_DAEMON -> {
                if (noRoot == null) noRoot = NoRootAudioProcessor(app)
                val ok = runCatching { noRoot?.start() == true }.getOrDefault(false)
                Log.i(TAG, if (ok) "NoRootAudioProcessor activo (DynamicsProcessing)"
                           else "NoRootAudioProcessor no disponible en este dispositivo")
            }
            Mode.ROOT_DAEMON -> {
                // Solo se llega aquí con Ruta B confirmada (efecto insertado/
                // procesando, ver evaluate()): el daemon procesa system-wide
                // y el fallback local solo duplicaria compresion/EQ.
                runCatching { noRoot?.stop() }
                Log.i(TAG, "Ruta B confirmada (efecto procesando) — fallback local detenido")
            }
            Mode.UNKNOWN -> Unit
        }
    }

    fun stop() {
        runCatching { noRoot?.stop() }
        noRoot = null
        started = false
        appContext = null
        _routeBConfirmed.value = false
        _daemonAlive.value = false
        _effectState.value = DSPBridge.EffectState.UNAVAILABLE
        _mode.value = Mode.UNKNOWN
    }
}
