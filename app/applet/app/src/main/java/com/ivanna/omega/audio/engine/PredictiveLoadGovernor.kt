package com.ivanna.omega.audio.engine

import com.ivanna.omega.audio.ThermalGovernor

/**
 * PredictiveLoadGovernor — Capa Preventiva de Predicción de Carga Acústica.
 *
 * Misión:
 * - Anticipar la saturación de procesamiento ANTES de que ocurran underruns o intervenciones térmicas.
 * - Analizar la combinación activa de módulos:
 *     * WFS / HRTF / Spatial Audio (convolución, reflexiones tempranas, ITD).
 *     * Cinematic Engine (SCIFI, COSMIC, HORROR, VOID).
 *     * Motor NPE / Volterra.
 *     * Ruta Bluetooth (A2DP / BLE con overhead de codificación del sistema).
 *     * Carga térmica actual del SoC (ThermalGovernor / PowerManager).
 * - Cuando la matriz predice una carga de CPU superior al 70%:
 *     * Modula proactivamente la complejidad espacial (ej. 2 taps de reflexiones en vez de 4).
 *     * Notifica al AdaptiveLatencyController para expandir preventivamente el headroom
 *       en 1 o 2 bloques de audio antes de que ocurra cualquier caída de frames.
 *
 * Cero asignaciones en el hot path.
 */
class PredictiveLoadGovernor(
    private val sampleRate: Int = 48000
) {
    companion object {
        private const val HIGH_LOAD_THRESHOLD   = 70.0f
        private const val CRITICAL_LOAD_THRESHOLD = 85.0f
        private const val RECOVERY_LOAD_THRESHOLD = 62.0f
    }

    @Volatile var predictedLoadPercent: Float = 0f
        private set

    @Volatile var isSpatialComplexityReduced: Boolean = false
        private set

    @Volatile var suggestedHeadroomMarginFrames: Long = 0L
        private set

    /**
     * Evalúa y predice la carga computacional esperada para el siguiente ciclo.
     */
    fun updatePrediction(
        dspActive: Boolean,
        cinematicModeOrdinal: Int, // 0: NONE, 1: SCIFI, 2: COSMIC, 3: HORROR, 4: VOID
        spatialActive: Boolean,
        npeActive: Boolean,
        isBluetooth: Boolean,
        blockFrames: Int = 320
    ) {
        var load = 0f

        if (dspActive) load += 15.0f

        load += when (cinematicModeOrdinal) {
            1 -> 18.0f // SCIFI (Formant + Reverb)
            2 -> 16.0f // COSMIC (ModDelay + Reverb)
            3 -> 14.0f // HORROR (SubHarmonic + Reverb)
            4 -> 22.0f // VOID (SubHarmonic + Delay + Reverb)
            else -> 0.0f
        }

        if (spatialActive) load += 22.0f
        if (npeActive) load += 12.0f
        if (isBluetooth) load += 10.0f

        // Factor térmico preventivo
        val thermal = ThermalGovernor.currentThermalLoad.coerceIn(0f, 1f)
        load += (thermal * 25.0f)

        predictedLoadPercent = load.coerceIn(0f, 100f)

        // Toma de decisiones preventiva
        when {
            predictedLoadPercent > CRITICAL_LOAD_THRESHOLD -> {
                isSpatialComplexityReduced = true
                suggestedHeadroomMarginFrames = (blockFrames * 2).toLong()
            }
            predictedLoadPercent > HIGH_LOAD_THRESHOLD -> {
                isSpatialComplexityReduced = true
                suggestedHeadroomMarginFrames = blockFrames.toLong()
            }
            predictedLoadPercent < RECOVERY_LOAD_THRESHOLD -> {
                isSpatialComplexityReduced = false
                suggestedHeadroomMarginFrames = 0L
            }
        }
    }

    fun reset() {
        predictedLoadPercent = 0f
        isSpatialComplexityReduced = false
        suggestedHeadroomMarginFrames = 0L
    }
}
