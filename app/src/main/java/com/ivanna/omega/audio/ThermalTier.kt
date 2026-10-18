package com.ivanna.omega.audio

/**
 * Nivel térmico del DSP y el recorte que el [ThermalGovernor] aplica en ese nivel.
 *
 * Es Kotlin puro (sin Android) para poder probarse en JVM y para que la política que se EJECUTA y
 * la que muestra la pantalla Sistema salgan de la MISMA tabla. Antes la pantalla leía la propiedad
 * `persist.ivanna.thermal_tier`, que ningún componente escribe: mostraba siempre «SIN DATO», y además
 * describía «sin Volterra / sin RIR / bypass», recortes que el gobernador nunca hace.
 */
enum class ThermalTier(
    val label: String,
    val exciterReduction: Float,
    val spatialCut: Float,
    val compressorCut: Float,
) {
    FULL("FULL", 0.0f, 0.0f, 0.0f),
    REDUCED("REDUCED", 0.4f, 0.0f, 0.0f),
    LIMITED("LIMITED", 0.7f, 0.25f, 0.15f),
    PROTECTED("PROTECTED", 1.0f, 0.5f, 0.3f);

    /** Texto del recorte realmente aplicado en este nivel. */
    fun describeCuts(): String = when (this) {
        FULL -> "sin recortes"
        REDUCED -> "exciter −${pct(exciterReduction)} %"
        LIMITED -> "exciter −${pct(exciterReduction)} % · ancho −${pct(spatialCut)} % · compresor −${pct(compressorCut)} %"
        PROTECTED -> "exciter mudo · ancho −${pct(spatialCut)} % · compresor −${pct(compressorCut)} %"
    }

    private fun pct(v: Float) = (v * 100f).toInt()

    companion object {
        const val HEADROOM_LIGHT = 0.3f      // empieza a degradar el exciter
        const val HEADROOM_MODERATE = 0.6f   // reduce espacial + compresor
        const val HEADROOM_SEVERE = 0.8f     // protección agresiva

        /** null si el headroom no es un número (HAL térmico sin respuesta): no se inventa un nivel. */
        fun of(headroom: Float): ThermalTier? = when {
            headroom.isNaN() -> null
            headroom >= HEADROOM_SEVERE -> PROTECTED
            headroom >= HEADROOM_MODERATE -> LIMITED
            headroom >= HEADROOM_LIGHT -> REDUCED
            else -> FULL
        }
    }
}
