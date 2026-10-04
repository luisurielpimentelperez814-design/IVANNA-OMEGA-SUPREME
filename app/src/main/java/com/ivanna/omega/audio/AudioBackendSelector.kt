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
     */
    private val _effectState = MutableStateFlow(DSPBridge.EffectState.UNAVAILABLE)
    val effectState: StateFlow<DSPBridge.EffectState> = _effectState.asStateFlow()

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    @Volatile private var noRoot: NoRootAudioProcessor? = null
    @Volatile private var started = false

    val isNoRootFallbackActive: Boolean get() = noRoot?.isActive() == true

    /** Idempotente: seguro llamarlo varias veces. */
    fun start(context: Context) {
        if (started) return
        started = true
        val app = context.applicationContext
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
        refreshEffectState(daemon)
        val root = if (daemon) true else RootAccess.probeSu()
        val next = when {
            root && daemon -> Mode.ROOT_DAEMON
            root -> Mode.ROOT_NO_DAEMON
            else -> Mode.NO_ROOT
        }
        if (next == _mode.value) return
        Log.i(TAG, "modo de audio: ${_mode.value} -> $next (root=$root daemon=$daemon)")
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
                // El daemon procesa system-wide: el fallback local solo
                // duplicaria compresion/EQ sobre el mismo audio.
                runCatching { noRoot?.stop() }
                Log.i(TAG, "daemon system-wide activo — fallback local detenido")
            }
            Mode.UNKNOWN -> Unit
        }
    }

    fun stop() {
        runCatching { noRoot?.stop() }
        noRoot = null
        started = false
        _mode.value = Mode.UNKNOWN
    }
}
