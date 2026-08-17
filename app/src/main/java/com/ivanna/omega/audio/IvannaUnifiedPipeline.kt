package com.ivanna.omega.audio

import android.content.Context
import android.util.Log
import com.ivanna.omega.core.IvannaNativeLib
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

/** Ruta de audio activa detectada por nativeGetUnifiedPipelineStatus. */
enum class ActiveRoute { NONE, ROUTE_A, ROUTE_B }

data class PipelineState(
    val activeRoute: ActiveRoute = ActiveRoute.NONE,
    val rms: Float = 0f,
    val peak: Float = 0f,
    val voiceProtect: Float = 0f,
    val compAmount: Float = 0f,
    val excReduction: Float = 0f,
    val spatialWidth: Float = 1f,
    val adaptiveActive: Boolean = false
) {
    /**
     * Layout compatible con nativeGetAdaptiveTelemetry[0..9] para que
     * AdaptiveBackend pueda usarlo como fuente de verdad cuando Ruta B
     * está activa y Ruta A no está corriendo.
     */
    fun toAdaptiveTelemetryArray(): FloatArray = floatArrayOf(
        rms,          // [0] rms
        peak,         // [1] peak
        0f,           // [2] gr_db (no disponible desde shared mem)
        1f,           // [3] target_gain (neutro)
        compAmount,   // [4] comp_amount
        excReduction, // [5] exc_reduction
        spatialWidth, // [6] spatial_width
        1f,           // [7] safety_margin (neutro)
        voiceProtect, // [8] voice_protect
        0f            // [9] adaptive_applied_count
    )
}

/**
 * IvannaUnifiedPipeline — orquestador singleton.
 *
 * Sondea nativeGetUnifiedPipelineStatus() a 20Hz, detecta qué ruta
 * está produciendo audio (A = IvannaBridgePlayer, B = omega_effect /
 * Spotify/YouTube, NONE = silencio), y expone StateFlow<PipelineState>
 * para AdaptiveBackend y cualquier componente de UI.
 *
 * Arrancar: IVANNAApplication.onCreate() llama start(this) síncrono
 * en el hilo principal, justo después de AudioRouteManager.start(this).
 *
 * AudioPipeline llama notifyRouteAStarted/Stopped() para sincronizar
 * el estado local sin depender del poll de 50ms.
 */
object IvannaUnifiedPipeline {

    private const val TAG = "IvannaUnifiedPipeline"
    private const val POLL_MS = 50L   // 20 Hz

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private var job: Job? = null

    private val _state = MutableStateFlow(PipelineState())
    val state: StateFlow<PipelineState> = _state

    @Volatile private var routeARunning = false

    fun start(context: Context) {
        if (job?.isActive == true) return
        job = scope.launch {
            Log.i(TAG, "Unified pipeline monitor @20Hz")
            while (isActive) {
                poll()
                delay(POLL_MS)
            }
        }
    }

    fun stop() {
        job?.cancel()
        job = null
    }

    fun notifyRouteAStarted() {
        routeARunning = true
        Log.i(TAG, "Route A started (IvannaBridgePlayer active)")
    }

    fun notifyRouteAStopped() {
        routeARunning = false
        Log.i(TAG, "Route A stopped")
    }

    /** Array compatible con nativeGetAdaptiveTelemetry para AdaptiveBackend. */
    fun toAdaptiveTelemetryArray(): FloatArray = _state.value.toAdaptiveTelemetryArray()

    private fun poll() {
        try {
            val raw = IvannaNativeLib.nativeGetUnifiedPipelineStatus()

            // FIX "Ruta A desconectada":
            // nativeGetUnifiedPipelineStatus devuelve raw[0]=0 (NONE) hasta que el
            // audio thread nativo procesa el PRIMER bloque de audio. En cold-start,
            // AudioPipeline ya llamó notifyRouteAStarted() → routeARunning=true,
            // pero la UI mostraba "Desconocida" porque poll() ignoraba ese flag.
            // Solución: si native dice NONE pero routeARunning=true → ROUTE_A.
            // Si JNI no está disponible (null) → mismo fallback.

            if (raw == null || raw.size < 8) {
                // JNI no disponible: usar solo estado Kotlin
                if (routeARunning) {
                    _state.value = _state.value.copy(activeRoute = ActiveRoute.ROUTE_A)
                }
                return
            }

            val nativeRoute = when (raw[0].toInt()) {
                1    -> ActiveRoute.ROUTE_A
                2    -> ActiveRoute.ROUTE_B
                else -> ActiveRoute.NONE
            }
            // Si native dice NONE pero AudioPipeline confirmó que Ruta A arrancó,
            // mantenemos ROUTE_A hasta que el audio thread actualice g_activeRoute.
            val route = if (nativeRoute == ActiveRoute.NONE && routeARunning)
                ActiveRoute.ROUTE_A else nativeRoute

            _state.value = PipelineState(
                activeRoute    = route,
                rms            = raw[1],
                peak           = raw[2],
                voiceProtect   = raw[3],
                compAmount     = raw[4],
                excReduction   = raw[5],
                spatialWidth   = raw[6],
                adaptiveActive = raw[7] > 0.5f
            )
        } catch (_: Throwable) {
            // Motor aún no inicializado — ignorar
        }
    }
}
