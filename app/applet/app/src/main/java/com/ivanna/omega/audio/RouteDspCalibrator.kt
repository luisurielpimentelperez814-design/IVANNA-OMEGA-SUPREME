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

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private var job: Job? = null

    @Volatile private var lastRoute: OutputRoute = OutputRoute.UNKNOWN

    /** Idempotente. Llamar desde IVANNAApplication tras AudioRouteManager.start(). */
    fun start(context: Context) {
        if (job?.isActive == true) return
        val appCtx = context.applicationContext
        job = scope.launch {
            // Primer chequeo inmediato + sondeo periódico.
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
        applyRouteCalibration(context, route)
    }

    private fun applyRouteCalibration(context: Context, route: OutputRoute) {
        val nativeReady = IvannaNativeLib.isLoaded
        val saPrefs = runCatching { SpatialAudioPrefs.load(context) }.getOrNull()
        val rirAllowed = saPrefs?.rirEnabled == true
        val hrtfAllowed = saPrefs?.hrtfEnabled ?: true
        val safAllowed = saPrefs?.safEnabled == true

        when (route) {
            OutputRoute.SPEAKER -> {
                // Altavoz: HRTF de oreja fuera; sala abierta RIR #81 (rir_0081.wav, RT60=0.613s) acoplada con SAF + XTC Transaural.
                if (nativeReady) runCatching {
                    IvannaNativeLib.nativeSetHRTFEnabled(false)
                    NativeBridge.safeSetWarpedLatticeRouteArchetype(2)
                }.onFailure { Log.w(TAG, "HRTF off (speaker): ${it.message}") }
                runCatching {
                    if (safAllowed) {
                        SaFRoomBridge.optimiseForCurrentRoom(rt60 = 0.613f, drr = 7.40f, steps = 24)
                        val q = SaFRoomBridge.getParams()
                        OmegaEngineBridge.pushSafLatentQ(q, gain = 0.65f)
                    }
                    if (rirAllowed) {
                        OmegaEngineBridge.setRoom(
                            rt60S = saPrefs?.rirRt60 ?: 0.613f,
                            wet = saPrefs?.rirWet ?: 0.16f,
                            roomIdx = 81
                        )
                    } else {
                        OmegaEngineBridge.disableRoom()
                    }
                }.onFailure { Log.w(TAG, "room speaker: ${it.message}") }
                Log.i(TAG, "Ruta SPEAKER → HRTF off + RIR=${if (rirAllowed) "#81" else "OFF"} + Arquetipo Bark #2")
            }

            OutputRoute.WIRED_AUX, OutputRoute.USB -> {
                // Canal directo a auricular/DAC/Genezi: HRTF binaural completo + Sala de Control Maestra
                // ITU-R BS.1116 (rir_0051.wav, RT60=0.340s, DRR=10.31dB, C80=16.66dB) acoplada con SOFA-SAF + True-Stereo 4-Caminos.
                if (nativeReady) runCatching {
                    IvannaNativeLib.nativeSetHRTFEnabled(hrtfAllowed)
                    NativeBridge.safeSetWarpedLatticeRouteArchetype(0)
                }.onFailure { Log.w(TAG, "HRTF on (aux/usb): ${it.message}") }
                runCatching {
                    if (safAllowed) {
                        SaFRoomBridge.optimiseForCurrentRoom(rt60 = 0.340f, drr = 10.31f, steps = 32)
                        val q = SaFRoomBridge.getParams()
                        OmegaEngineBridge.pushSafLatentQ(q, gain = 0.85f)
                    }
                    if (rirAllowed) {
                        OmegaEngineBridge.setRoom(
                            rt60S = saPrefs?.rirRt60 ?: 0.340f,
                            wet = saPrefs?.rirWet ?: 0.22f,
                            roomIdx = 51
                        )
                    } else {
                        OmegaEngineBridge.disableRoom()
                    }
                }.onFailure { Log.w(TAG, "room aux/usb: ${it.message}") }
                Log.i(TAG, "Ruta ${route.name} → HRTF=$hrtfAllowed + RIR=${if (rirAllowed) "#51" else "OFF"} + Arquetipo Bark #0")
            }

            OutputRoute.BLUETOOTH -> {
                // Codec con pérdida: HRTF on + Sala Compacta Anti-Codec #63 (rir_0063.wav, RT60=0.293s) acoplada con SOFA-SAF.
                if (nativeReady) {
                    runCatching {
                        IvannaNativeLib.nativeSetHRTFEnabled(hrtfAllowed)
                        NativeBridge.safeSetWarpedLatticeRouteArchetype(1)
                    }.onFailure { Log.w(TAG, "HRTF on (bt): ${it.message}") }
                    runCatching {
                        IvannaNativeLib.nativeSetSpatialWidthDirect(0.88f)
                    }.onFailure { Log.w(TAG, "width bt: ${it.message}") }
                }
                runCatching {
                    if (safAllowed) {
                        SaFRoomBridge.optimiseForCurrentRoom(rt60 = 0.293f, drr = 9.85f, steps = 24)
                        val q = SaFRoomBridge.getParams()
                        OmegaEngineBridge.pushSafLatentQ(q, gain = 0.78f)
                    }
                    if (rirAllowed) {
                        OmegaEngineBridge.setRoom(
                            rt60S = saPrefs?.rirRt60 ?: 0.293f,
                            wet = saPrefs?.rirWet ?: 0.18f,
                            roomIdx = 63
                        )
                    } else {
                        OmegaEngineBridge.disableRoom()
                    }
                }.onFailure { Log.w(TAG, "room bt: ${it.message}") }
                runCatching { MagiskBridge.setMid(1.12f) }
                runCatching { MagiskBridge.setMaster(0.91f) }
                Log.i(TAG, "Ruta BLUETOOTH GRADO MAGISTRAL → HRTF=$hrtfAllowed + RIR=${if (rirAllowed) "#63" else "OFF"}, presencia +mid")
            }

            OutputRoute.UNKNOWN -> Unit
        }
    }
}
