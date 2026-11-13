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

    // Umbrales de headroom y recortes por nivel: ver ThermalTier (misma tabla que la UI).

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private var job: kotlinx.coroutines.Job? = null

    /** Última intensidad térmica aplicada [0..1] — visible para telemetría. */
    @Volatile var currentThermalLoad: Float = 0f
        private set

    /** true si el HAL térmico respondió al menos una vez (API 29+ presente). */
    @Volatile var thermalApiAvailable: Boolean = false
        private set

    /** Nivel actual, o null mientras el HAL térmico no haya respondido (la UI muestra «SIN DATO»). */
    val currentTier: ThermalTier?
        get() = if (thermalApiAvailable) ThermalTier.of(currentThermalLoad) else null

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
        if (!headroom.isNaN()) {
            thermalApiAvailable = true
            currentThermalLoad = headroom
            applyThermalPolicy(headroom)
        }
        // Carga del DSP (v2.5.426): el tier de HOLOS tambien responde al shed,
        // no solo a la temperatura. Corre aunque el HAL termico no responda.
        tickLoad()
    }

    /**
     * Lee la carga real del DSP y actualiza el tier de HOLOS por carga
     * (DspLoadTierPolicy). Solo re-aplica la politica combinada cuando el
     * tier de carga cambia (el dedup de lastHolosTier evita escrituras JNI
     * de mas).
     *
     * v2.5.429: el disparador es la CARGA REAL (EMA del tiempo por bloque
     * dividida por el presupuesto), NO el shed. Medido en vivo (23:43,
     * v2.5.428): con HOLOS p99 13-25 ms y dsp_us 9-11 ms, el shed se quedo
     * en 1.3-1.4% y el tier en 0 — el shed no refleja la sobrecarga.
     * nativeGetDspShed() devuelve [shed, emaUs, peakWindowUs, budgetNs,
     * ...tiempos por etapa..., peakUs]; aqui usamos emaUs [1] y budgetNs [3].
     */
    private fun tickLoad() {
        if (!IvannaNativeLib.isLoaded) return
        val arr = runCatching { IvannaNativeLib.nativeGetDspShed() }.getOrNull() ?: return
        if (arr.size < 4) return
        val emaUs = arr[1]
        val budgetUs = arr[3] / 1000f
        if (emaUs.isNaN() || budgetUs <= 0f) return
        val load = (emaUs / budgetUs).coerceIn(0f, 2f)
        val loadTier = loadTierPolicy.update(load)
        if (loadTier != lastLoadTier) {
            lastLoadTier = loadTier
            Log.i(TAG, "DSP load: emaUs=${"%.0f".format(emaUs)}us budget=${"%.0f".format(budgetUs)}us load=${"%.2f".format(load)} -> HOLOS load tier=$loadTier")
            val thermalTier = if (thermalApiAvailable) ThermalTier.of(currentThermalLoad) else null
            applyCombinedPolicy(thermalTier)
        }
    }

    /**
     * Mapea headroom → reducción de carga DSP vía los setters nativos ya
     * existentes del AdaptiveDecisionEngine. Cero asignaciones, cero locks;
     * los setters nativos ya son atómicos por diseño (atomic store).
     */
    /**
     * Resource-pressure input from SAFMUS whole-device adapters.
     * 0=normal, 1=conservative, 2=safe. The strongest active pressure wins;
     * the app does not write CPU/GPU vendor nodes or alter Android security policy.
     */
    @Volatile private var externalResourcePressure: Int = 0

    fun setExternalResourceBudget(level: Int) {
        externalResourcePressure = level.coerceIn(0, 2)
        val thermalTier = if (thermalApiAvailable) ThermalTier.of(currentThermalLoad) else null
        applyCombinedPolicy(thermalTier)
    }

    private fun applyThermalPolicy(headroom: Float) {
        val tier = ThermalTier.of(headroom) ?: return
        applyCombinedPolicy(tier)
    }

    private fun applyCombinedPolicy(thermalTier: ThermalTier?) {
        if (!IvannaNativeLib.isLoaded) return
        val externalTier = when (externalResourcePressure) {
            2 -> ThermalTier.PROTECTED
            1 -> ThermalTier.LIMITED
            else -> ThermalTier.FULL
        }
        // Ordinals reflect increasing DSP sacrifice in ThermalTier declaration order.
        val tier = if (thermalTier == null || externalTier.ordinal > thermalTier.ordinal) {
            externalTier
        } else thermalTier
        val exciterRed = tier.exciterReduction
        val spatialCut = tier.spatialCut
        val compCut = tier.compressorCut

        // Solo escribir cuando hay cambio real (evita logspam y churn de la
        // rampa anti-zipper del exciter si el valor no se movió).
        runCatching {
            if (exciterRed != lastExciterRed) {
                IvannaNativeLib.nativeSetExciterReduction(exciterRed)
                lastExciterRed = exciterRed
                Log.i(TAG, "SAFMUS/DSP: tier=${tier.label}, exciter_reduction=$exciterRed, thermalHeadroom=${currentThermalLoad.takeIf { thermalApiAvailable } ?: "unknown"}")
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
            // HOLOS / ORBE / NPE de la Ruta A: antes ningún componente les pasaba el nivel
            // térmico (solo la Ruta B lo hacía en omega_effect). Mismo mapa que allí:
            // LIMITED → Warm, PROTECTED → Limited; ORBE y NPE se apagan desde LIMITED.
            // v2.5.426: el tier de carga (por shed del DSP) se combina con el
            // termico: manda el mas restrictivo. El spatialShed (apagar ORBE
            // y NPE) sigue siendo solo termico: el tier de carga solo baja
            // las bandas de HOLOS (24->12), sin apagar ORBE/NPE.
            val thermalHolosTier = when (tier) {
                ThermalTier.FULL, ThermalTier.REDUCED -> 0
                ThermalTier.LIMITED -> 1
                ThermalTier.PROTECTED -> 2
            }
            val holosTier = maxOf(thermalHolosTier, lastLoadTier)
            val spatialShed = thermalHolosTier >= 1
            if (holosTier != lastHolosTier) {
                IvannaNativeLib.nativeSetHolosThermalTier(holosTier, spatialShed)
                lastHolosTier = holosTier
                Log.i(TAG, "SAFMUS/DSP: HOLOS tier=$holosTier (tier=${tier.label}, loadTier=$lastLoadTier)")
            }
        }.onFailure { Log.w(TAG, "applyThermalPolicy: ${it.message}") }
    }

    @Volatile private var lastExciterRed = 0f
    @Volatile private var lastSpatialCut = 0f
    @Volatile private var lastCompCut = 0f
    @Volatile private var lastHolosTier = 0

    /** Tier de HOLOS por carga del DSP (v2.5.426) — ver DspLoadTierPolicy. */
    private val loadTierPolicy = DspLoadTierPolicy()
    @Volatile private var lastLoadTier = 0
}
