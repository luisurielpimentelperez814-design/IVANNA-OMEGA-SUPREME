package com.ivanna.omega.sensing

import kotlin.math.abs
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.sin
import kotlin.math.sqrt

/**
 * Hardware-neutral CSI feature extraction.
 *
 * This transforms actual CSI samples supplied by a verified chipset provider into
 * normalized signal features. It does not manufacture CSI from RSSI and does not
 * reconstruct people, anatomy, walls, or metric geometry by itself.
 */
object CsiFeatureExtractor {
    data class Features(
        val sampleCount: Int,
        val antennaCount: Int,
        val sampleRateHz: Int,
        val meanAmplitude: Double,
        val amplitudeStdDev: Double,
        val amplitudeMad: Double,
        val phaseCoherence: Double?,
        val temporalAmplitudeDelta: Double?,
        val quality: Double,
        val warnings: List<String>
    )

    private var previous: FloatArray? = null

    @Synchronized
    fun analyze(sample: UniversalWifiSensingAdapter.CsiSample): Features {
        val amplitudes = sample.subcarrierAmplitudes
            .filter { it.isFinite() && it >= 0f }
            .map { it.toDouble() }
        val warnings = mutableListOf<String>()
        if (amplitudes.isEmpty()) {
            previous = null
            return Features(0, sample.antennaCount.coerceAtLeast(0), sample.sampleRateHz.coerceAtLeast(0),
                0.0, 0.0, 0.0, null, null, 0.0,
                listOf("CSI sin amplitudes finitas válidas."))
        }

        if (sample.antennaCount < 1) warnings += "El proveedor no informó una cantidad válida de antenas."
        if (sample.sampleRateHz < 1) warnings += "La frecuencia de muestreo no está validada."
        val mean = amplitudes.average()
        val variance = amplitudes.map { (it - mean) * (it - mean) }.average()
        val sorted = amplitudes.sorted()
        val median = median(sorted)
        val mad = median(sorted.map { abs(it - median) }.sorted())
        val phase = sample.subcarrierPhases
            ?.filter { it.isFinite() }
            ?.takeIf { it.isNotEmpty() }
            ?.let { phases ->
                val x = phases.map { cos(it.toDouble()) }.average()
                val y = phases.map { sin(it.toDouble()) }.average()
                sqrt(x * x + y * y).coerceIn(0.0, 1.0)
            }

        val old = previous
        val temporalDelta = if (old != null && old.size == amplitudes.size) {
            amplitudes.indices.map { abs(amplitudes[it] - old[it].toDouble()) }.average()
                .let { it / (mean + 1e-9) }
                .coerceIn(0.0, 10.0)
        } else null
        previous = amplitudes.toFloatArray()

        val validAntenna = sample.antennaCount > 0
        val validRate = sample.sampleRateHz > 0
        val quality = (0.5 * (amplitudes.size / 64.0).coerceIn(0.0, 1.0) +
            0.25 * if (phase != null) 1.0 else 0.0 +
            0.15 * if (validAntenna) 1.0 else 0.0 +
            0.10 * if (validRate) 1.0 else 0.0).coerceIn(0.0, 1.0)

        return Features(
            sampleCount = amplitudes.size,
            antennaCount = sample.antennaCount.coerceAtLeast(0),
            sampleRateHz = sample.sampleRateHz.coerceAtLeast(0),
            meanAmplitude = mean,
            amplitudeStdDev = sqrt(variance),
            amplitudeMad = mad,
            phaseCoherence = phase,
            temporalAmplitudeDelta = temporalDelta,
            quality = quality,
            warnings = warnings
        )
    }

    @Synchronized
    fun reset() {
        previous = null
    }

    private fun median(values: List<Double>): Double {
        if (values.isEmpty()) return 0.0
        val mid = values.size / 2
        return if (values.size % 2 == 0) (values[mid - 1] + values[mid]) / 2.0 else values[mid]
    }
}
