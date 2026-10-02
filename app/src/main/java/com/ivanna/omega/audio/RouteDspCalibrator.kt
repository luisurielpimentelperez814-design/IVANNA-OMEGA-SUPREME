package com.ivanna.omega.audio

import android.content.Context
import android.util.Log
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.core.NativeBridge
import com.ivanna.omega.magisk.MagiskBridge
import com.ivanna.omega.magisk.OmegaEngineBridge
import com.ivanna.omega.saf.SaFRoomBridge
import com.ivanna.omega.ui.SpatialAudioPrefs
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

/**
 * RouteDspCalibrator — convierte la detección de ruta de AudioRouteManager
 * en parámetros DSP reales, por ruta de salida.
 *
 * POR QUÉ EXISTE (TAREA 4 — integración runtime):
 *   AudioRouteManager detecta la ruta y aplica compensación bass/dialog/
 *   widener, pero la ruta nunca gobernaba el motor espacial ni la sala:
 *   con auriculares el HRTF binaural quedaba a medio gas y el RIR de sala
 *   sonaba igual que en altavoz (donde no debe aplicarse convolución de
 *   oreja, sino respuesta de habitación). La detección era correcta; el
 *   gobierno del DSP, inexistente.
 *
 *   Este bridge NO modifica AudioRouteManager (su política bass/dialog/
 *   widener sigue intacta). Solo lee su detectOutputRoute() público cada
 *   2 s y aplica la calibración de motor correspondiente:
 *
 *     SPEAKER    → HRTF off, sala RIR activa (RT60 0.7s, wet 0.30):
 *                  el altavoz suena EN una habitación, la reverb de sala
 *                  es la herramienta correcta; el HRTF de oreja no aplica.
 *     WIRED_AUX  → HRTF on, sin sala (auricular cableado = canal directo,
 *                  sala cero; la "sala" del usuario ya es la real).
 *     USB        → igual que AUX (DAC externo, canal limpio).
 *     BLUETOOTH  → HRTF on, ancho contenido (0.85) y sala moderada
 *                  (0.4s/0.20): los codecs con pérdida (SBC/AAC) colapsan
 *                  la banda de presencia y un campo demasiado ancho se
 *                  recodifica peor; se deja headroom al codec.
 *     UNKNOWN    → no toca nada (estado conservador).
 *
 *   Todo va al hilo IO; los setters JNI se llaman en runCatching
 *   individual (una ruta sin engine nativo listo no aborta las demás).
 */
object RouteDspCalibrator {

    private const val TAG = "RouteDspCalibrator"
    private const val POLL_MS = 2_000L
    const val DRY_ROOM_T60_CEILING_SEC = 0.50f
    const val LIVE_ROOM_T60_CUTOFF_SEC = 1.20f

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private var job: Job? = null

    @Volatile private var lastRoute: OutputRoute = OutputRoute.UNKNOWN

    /**
     * §6.2 & §7.7: En sala viva (T60 >= 1.2 s) la cola y las ER sintéticas están OFF (0.0f) por defecto.
     */
    fun limitSyntheticReverbWetForRoomT60(requestedWet: Float, roomT60Sec: Float): Float {
        if (requestedWet <= 0f || roomT60Sec >= LIVE_ROOM_T60_CUTOFF_SEC) return 0f
        val clamped = requestedWet.coerceIn(0f, 1f)
        if (roomT60Sec <= DRY_ROOM_T60_CEILING_SEC) return clamped
        val scale = (LIVE_ROOM_T60_CUTOFF_SEC - roomT60Sec) /
                    (LIVE_ROOM_T60_CUTOFF_SEC - DRY_ROOM_T60_CEILING_SEC)
        return clamped * scale.coerceIn(0f, 1f)
    }

    /**
     * §0.1: Llamado síncronamente desde AudioRouteManager.applyRoute() al arrancar
     * y en cada cambio de dispositivo físico, publicando la activación de etapas
     * ANTES del primer bloque de audio.
     */
    fun onRouteChanged(context: Context, route: OutputRoute) {
        if (route == OutputRoute.UNKNOWN) return
        val appCtx = context.applicationContext
        lastRoute = route
        applySynchronousInProcessStages(appCtx, route)
        scope.launch {
            runCatching { applyAsyncDaemonStages(appCtx, route) }
                .onFailure { Log.w(TAG, "applyAsyncDaemonStages: ${it.message}") }
        }
    }

    /** Idempotente. Llamar desde IVANNAApplication tras AudioRouteManager.start(). */
    fun start(context: Context) {
        val appCtx = context.applicationContext
        // §0.1: Publicación síncrona inmediata antes del primer bloque de audio
        runCatching {
            val initialRoute = AudioRouteManager.detectOutputRoute()
            if (initialRoute != OutputRoute.UNKNOWN && initialRoute != lastRoute) {
                onRouteChanged(appCtx, initialRoute)
            }
        }
        if (job?.isActive == true) return
        job = scope.launch {
            // Sondeo periódico de respaldo.
            while (isActive) {
                runCatching { calibrate(appCtx, AudioRouteManager.detectOutputRoute()) }
                    .onFailure { Log.w(TAG, "calibrate: ${it.message}") }
                delay(POLL_MS)
            }
        }
        Log.i(TAG, "RouteDspCalibrator activo (sondeo ${POLL_MS}ms)")
    }

    fun stop() {
        job?.cancel()
        job = null
    }

    private fun calibrate(context: Context, route: OutputRoute) {
        if (route == lastRoute || route == OutputRoute.UNKNOWN) return
        lastRoute = route
        applySynchronousInProcessStages(context, route)
        applyAsyncDaemonStages(context, route)
    }

    private fun applySynchronousInProcessStages(context: Context, route: OutputRoute) {
        if (!IvannaNativeLib.isLoaded) return
        val saPrefs = runCatching { SpatialAudioPrefs.load(context) }.getOrNull()
        val hrtfAllowed = saPrefs?.hrtfEnabled ?: true
        when (route) {
            OutputRoute.SPEAKER -> {
                runCatching {
                    IvannaNativeLib.nativeSetHRTFEnabled(false)
                    NativeBridge.safeSetWarpedLatticeRouteArchetype(2)
                }.onFailure { Log.w(TAG, "HRTF off (speaker): ${it.message}") }
            }
            OutputRoute.WIRED_AUX, OutputRoute.USB -> {
                runCatching {
                    IvannaNativeLib.nativeSetHRTFEnabled(hrtfAllowed)
                    NativeBridge.safeSetWarpedLatticeRouteArchetype(0)
                }.onFailure { Log.w(TAG, "HRTF on (aux/usb): ${it.message}") }
            }
            OutputRoute.BLUETOOTH -> {
                runCatching {
                    IvannaNativeLib.nativeSetHRTFEnabled(hrtfAllowed)
                    NativeBridge.safeSetWarpedLatticeRouteArchetype(1)
                }.onFailure { Log.w(TAG, "HRTF on (bt): ${it.message}") }
                runCatching {
                    IvannaNativeLib.nativeSetSpatialWidthDirect(0.88f)
                }.onFailure { Log.w(TAG, "width bt: ${it.message}") }
            }
            OutputRoute.UNKNOWN -> Unit
        }
    }

    private fun applyAsyncDaemonStages(context: Context, route: OutputRoute) {
        val saPrefs = runCatching { SpatialAudioPrefs.load(context) }.getOrNull()
        val rirAllowed = saPrefs?.rirEnabled == true
        val hrtfAllowed = saPrefs?.hrtfEnabled ?: true
        val safAllowed = saPrefs?.safEnabled == true

        when (route) {
            OutputRoute.SPEAKER -> {
                val rt60 = saPrefs?.rirRt60 ?: 0.613f
                val effectiveWet = limitSyntheticReverbWetForRoomT60(saPrefs?.rirWet ?: 0.16f, rt60)
                runCatching {
                    if (safAllowed) {
                        SaFRoomBridge.optimiseForCurrentRoom(rt60 = 0.613f, drr = 7.40f, steps = 24)
                        val q = SaFRoomBridge.getParams()
                        OmegaEngineBridge.pushSafLatentQ(q, gain = 0.65f)
                    }
                    if (rirAllowed && effectiveWet > 0.001f) {
                        OmegaEngineBridge.setRoom(
                            rt60S = rt60,
                            wet = effectiveWet,
                            roomIdx = 81
                        )
                    } else {
                        OmegaEngineBridge.disableRoom()
                    }
                }.onFailure { Log.w(TAG, "room speaker: ${it.message}") }
                Log.i(TAG, "Ruta SPEAKER → HRTF off + RIR=${if (rirAllowed && effectiveWet > 0.001f) "#81" else "OFF"} + Arquetipo Bark #2")
            }

            OutputRoute.WIRED_AUX, OutputRoute.USB -> {
                val rt60 = saPrefs?.rirRt60 ?: 0.340f
                val effectiveWet = limitSyntheticReverbWetForRoomT60(saPrefs?.rirWet ?: 0.22f, rt60)
                runCatching {
                    if (safAllowed) {
                        SaFRoomBridge.optimiseForCurrentRoom(rt60 = 0.340f, drr = 10.31f, steps = 32)
                        val q = SaFRoomBridge.getParams()
                        OmegaEngineBridge.pushSafLatentQ(q, gain = 0.85f)
                    }
                    if (rirAllowed && effectiveWet > 0.001f) {
                        OmegaEngineBridge.setRoom(
                            rt60S = rt60,
                            wet = effectiveWet,
                            roomIdx = 51
                        )
                    } else {
                        OmegaEngineBridge.disableRoom()
                    }
                }.onFailure { Log.w(TAG, "room aux/usb: ${it.message}") }
                Log.i(TAG, "Ruta ${route.name} → HRTF=$hrtfAllowed + RIR=${if (rirAllowed && effectiveWet > 0.001f) "#51" else "OFF"} + Arquetipo Bark #0")
            }

            OutputRoute.BLUETOOTH -> {
                val rt60 = saPrefs?.rirRt60 ?: 0.293f
                val effectiveWet = limitSyntheticReverbWetForRoomT60(saPrefs?.rirWet ?: 0.18f, rt60)
                runCatching {
                    if (safAllowed) {
                        SaFRoomBridge.optimiseForCurrentRoom(rt60 = 0.293f, drr = 9.85f, steps = 24)
                        val q = SaFRoomBridge.getParams()
                        OmegaEngineBridge.pushSafLatentQ(q, gain = 0.78f)
                    }
                    if (rirAllowed && effectiveWet > 0.001f) {
                        OmegaEngineBridge.setRoom(
                            rt60S = rt60,
                            wet = effectiveWet,
                            roomIdx = 63
                        )
                    } else {
                        OmegaEngineBridge.disableRoom()
                    }
                }.onFailure { Log.w(TAG, "room bt: ${it.message}") }
                runCatching { MagiskBridge.setMid(1.12f) }
                runCatching { MagiskBridge.setMaster(0.91f) }
                Log.i(TAG, "Ruta BLUETOOTH GRADO MAGISTRAL → HRTF=$hrtfAllowed + RIR=${if (rirAllowed && effectiveWet > 0.001f) "#63" else "OFF"}, presencia +mid")
            }

            OutputRoute.UNKNOWN -> Unit
        }
    }
}
