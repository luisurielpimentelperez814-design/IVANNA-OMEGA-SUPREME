package com.ivanna.omega.audio

import android.content.Context
import android.os.Build
import android.os.PowerManager
import android.util.Log
import com.ivanna.omega.core.IvannaNativeLib
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

/**
 * ThermalGovernor — gobernador térmico de nivel OEM (Qualcomm/Pixel/Samsung).
 *
 * Antes: NINGUNA capa del sistema consultaba el estado térmico del SoC.
 * El DSP corría a máxima carga (exciter 2x-oversampled, convolver RIR,
 * PDEngine NHO+Spatial, limiter) sin importar la temperatura — en sesiones
 * largas con carga sostenida el SoC hace thermal throttling por su cuenta
 * (el kernel baja clocks de golpe) y eso se oye como XRuns/glitches.
 *
 * Ahora: el sistema DEGRADA PROACTIVAMENTE la carga antes de que el kernel
 * intervenga, preservando la continuidad de audio (la regla de oro OEM:
 * mejor bajar calidad que producir un glitch).
 *
 * Jerarquía de reducción (de menor a mayor sacrificio perceptual):
 *   1. exciter_reduction  — el más caro (oversampling 2x + biquads OS)
 *   2. spatial_width      — ITD/ILD barato pero RIR pesado lo alimenta
 *   3. compressor_amount  — envolvente log por muestra
 *   4. target_gain        — nunca se toca (volumen es sagrado)
 *
 * Fuente de verdad: PowerManager.getThermalHeadroom() (API 29+) en
 * polling de 2s desde hilo IO — NUNCA en el callback de audio.
 * En API < 29 (o si el HAL térmico no responde) el gobernador queda
 * inerte: intensidad adaptativa intacta, cero efecto colateral.
 */
object ThermalGovernor {

    private const val TAG = "ThermalGovernor"
    private const val POLL_MS = 2_000L

    // Umbrales de headroom (0.0 = sin carga térmica … 1.0 = throttling severo).
    // Mismos puntos de corte que usan los frameworks de juego OEM.
    private const val HEADROOM_LIGHT    = 0.3f   // empieza a degradar exciter
    private const val HEADROOM_MODERATE = 0.6f   // reduce espacial + compresor
    private const val HEADROOM_SEVERE   = 0.8f   // protección agresiva

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private var job: kotlinx.coroutines.Job? = null

    /** Última intensidad térmica aplicada [0..1] — visible para telemetría. */
    @Volatile var currentThermalLoad: Float = 0f
        private set

    /** true si el HAL térmico respondió al menos una vez (API 29+ presente). */
    @Volatile var thermalApiAvailable: Boolean = false
        private set

    /** Idempotente. Llamar desde IVANNAApplication tras AudioEngine init. */
    fun start(context: Context) {
        if (job?.isActive == true) return
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) {
            Log.i(TAG, "API < 29: sin getThermalHeadroom — gobernador inerte")
            return
        }
        val pm = context.applicationContext
            .getSystemService(Context.POWER_SERVICE) as? PowerManager
        if (pm == null) {
            Log.w(TAG, "PowerManager no disponible — gobernador inerte")
            return
        }
        job = scope.launch {
            while (isActive) {
                runCatching { tick(pm) }
                    .onFailure { Log.w(TAG, "tick: ${it.message}") }
                delay(POLL_MS)
            }
        }
        Log.i(TAG, "ThermalGovernor activo (sondeo ${POLL_MS}ms)")
    }

    fun stop() {
        job?.cancel()
        job = null
    }

    private fun tick(pm: PowerManager) {
        // getThermalHeadroom(status) disponible desde API 31; devuelve NaN si HAL no responde.
        // PowerManager.THERMAL_STATUS_NONE = 0
        val headroom = if (android.os.Build.VERSION.SDK_INT >= 31)
            runCatching { pm.getThermalHeadroom(0) }.getOrDefault(Float.NaN)
        else Float.NaN
        if (headroom.isNaN()) return  // HAL térmico no responde este ciclo
        thermalApiAvailable = true
        currentThermalLoad = headroom
        applyThermalPolicy(headroom)
    }

    /**
     * Mapea headroom → reducción de carga DSP vía los setters nativos ya
     * existentes del AdaptiveDecisionEngine. Cero asignaciones, cero locks;
     * los setters nativos ya son atómicos por diseño (atomic store).
     */
    private fun applyThermalPolicy(headroom: Float) {
        if (!IvannaNativeLib.isLoaded) return

        // Factor de reducción [0..1] por subsistema según severidad.
        val exciterRed: Float
        val spatialCut: Float
        val compCut: Float
        when {
            headroom >= HEADROOM_SEVERE -> {
                exciterRed = 1.0f    // exciter prácticamente mudo
                spatialCut = 0.5f    // mitad del ancho espacial
                compCut    = 0.3f    // compresor al mínimo efectivo
            }
            headroom >= HEADROOM_MODERATE -> {
                exciterRed = 0.7f
                spatialCut = 0.25f
                compCut    = 0.15f
            }
            headroom >= HEADROOM_LIGHT -> {
                exciterRed = 0.4f
                spatialCut = 0.0f
                compCut    = 0.0f
            }
            else -> {
                exciterRed = 0.0f
                spatialCut = 0.0f
                compCut    = 0.0f
            }
        }

        // Solo escribir cuando hay cambio real (evita logspam y churn de la
        // rampa anti-zipper del exciter si el valor no se movió).
        runCatching {
            if (exciterRed != lastExciterRed) {
                IvannaNativeLib.nativeSetExciterReduction(exciterRed)
                lastExciterRed = exciterRed
                Log.i(TAG, "térmico: exciter_reduction=$exciterRed (headroom=$headroom)")
            }
            if (compCut != lastCompCut) {
                IvannaNativeLib.nativeSetCompressorAmount(1.0f - compCut)
                lastCompCut = compCut
            }
            if (spatialCut != lastSpatialCut) {
                // nativeSetSpatialWidth espera el ancho absoluto; recortamos
                // sobre el nominal de cadena (1.0 = unity).
                IvannaNativeLib.nativeSetSpatialWidth(1.0f - spatialCut)
                lastSpatialCut = spatialCut
            }
        }.onFailure { Log.w(TAG, "applyThermalPolicy: ${it.message}") }
    }

    @Volatile private var lastExciterRed = 0f
    @Volatile private var lastSpatialCut = 0f
    @Volatile private var lastCompCut = 0f
}
