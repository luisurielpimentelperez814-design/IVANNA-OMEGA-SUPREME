package com.ivanna.omega.audio

import android.content.Context
import android.util.Log
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.magisk.OmegaEngineBridge
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
                runCatching { calibrate(AudioRouteManager.detectOutputRoute()) }
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

    private fun calibrate(route: OutputRoute) {
        if (route == lastRoute || route == OutputRoute.UNKNOWN) return
        lastRoute = route
        applyRouteCalibration(route)
    }

    private fun applyRouteCalibration(route: OutputRoute) {
        val nativeReady = IvannaNativeLib.isLoaded

        when (route) {
            OutputRoute.SPEAKER -> {
                // Altavoz: el audio habita la habitación del usuario.
                // HRTF de oreja fuera; sala RIR real encendida.
                if (nativeReady) runCatching {
                    IvannaNativeLib.nativeSetHRTFEnabled(false)
                }.onFailure { Log.w(TAG, "HRTF off (speaker): ${it.message}") }
                runCatching {
                    OmegaEngineBridge.setRoom(rt60S = 0.7f, wet = 0.30f, roomIdx = -1)
                }.onFailure { Log.w(TAG, "room speaker: ${it.message}") }
                Log.i(TAG, "Ruta SPEAKER → HRTF off + sala RIR (RT60 0.7s, wet 0.30)")
            }

            OutputRoute.WIRED_AUX, OutputRoute.USB -> {
                // Canal directo a auricular/DAC: HRTF binaural completo,
                // sin sala sintética (la sala real del usuario ya existe).
                if (nativeReady) runCatching {
                    IvannaNativeLib.nativeSetHRTFEnabled(true)
                }.onFailure { Log.w(TAG, "HRTF on (aux/usb): ${it.message}") }
                runCatching {
                    OmegaEngineBridge.setRoom(rt60S = 0f, wet = 0f, roomIdx = -1) // sala off
                }.onFailure { Log.w(TAG, "room off (aux/usb): ${it.message}") }
                Log.i(TAG, "Ruta ${route.name} → HRTF on, sala off")
            }

            OutputRoute.BLUETOOTH -> {
                // Codec con pérdida: HRTF sí, pero campo contenido para no
                // alimentar al codec con side-channel extremo, y sala corta
                // (reverb larga + SBC = cola empastada).
                if (nativeReady) {
                    runCatching {
                        IvannaNativeLib.nativeSetHRTFEnabled(true)
                    }.onFailure { Log.w(TAG, "HRTF on (bt): ${it.message}") }
                    runCatching {
                        IvannaNativeLib.nativeSetSpatialWidthDirect(0.85f)
                    }.onFailure { Log.w(TAG, "width bt: ${it.message}") }
                }
                runCatching {
                    OmegaEngineBridge.setRoom(rt60S = 0.4f, wet = 0.20f, roomIdx = -1)
                }.onFailure { Log.w(TAG, "room bt: ${it.message}") }
                Log.i(TAG, "Ruta BLUETOOTH → HRTF on, width 0.85, sala corta (0.4s/0.20)")
            }

            OutputRoute.UNKNOWN -> Unit
        }
    }
}
