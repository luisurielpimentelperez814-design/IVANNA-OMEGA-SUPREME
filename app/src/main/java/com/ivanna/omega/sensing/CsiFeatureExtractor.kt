package com.ivanna.omega.sensing

import kotlin.math.abs
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

    private data class PreviousFrame(
        val timestampNanos: Long,
        val amplitudes: FloatArray,
        val antennaCount: Int,
        val sampleRateHz: Int
    )

    private var previous: PreviousFrame? = null

    @Synchronized
    fun analyze(sample: UniversalWifiSensingAdapter.CsiSample): Features {
        val warnings = mutableListOf<String>()
        val rawAmplitudes = sample.subcarrierAmplitudes
        if (rawAmplitudes.isEmpty() || rawAmplitudes.any { !it.isFinite() || it < 0f }) {
            previous = null
            return Features(0, sample.antennaCount.coerceAtLeast(0), sample.sampleRateHz.coerceAtLeast(0),
                0.0, 0.0, 0.0, null, null, 0.0,
                listOf("CSI descartado: amplitudes vacías, no finitas o negativas."))
        }
        val amplitudes = rawAmplitudes.map { it.toDouble() }

        if (sample.timestampNanos <= 0L) warnings += "La marca temporal CSI no es válida."
        if (sample.antennaCount < 1) warnings += "El proveedor no informó una cantidad válida de antenas."
        if (sample.sampleRateHz < 1) warnings += "La frecuencia de muestreo no está validada."
        val mean = amplitudes.average()
        val variance = amplitudes.map { (it - mean) * (it - mean) }.average()
        val sorted = amplitudes.sorted()
        val median = median(sorted)
        val mad = median(sorted.map { abs(it - median) }.sorted())
        val phaseArray = sample.subcarrierPhases
        val phase = if (phaseArray == null) {
            null
        } else if (phaseArray.size != rawAmplitudes.size || phaseArray.any { !it.isFinite() }) {
            warnings += "La fase CSI no coincide en longitud o contiene valores no finitos."
            null
        } else {
            val x = phaseArray.map { cos(it.toDouble()) }.average()
            val y = phaseArray.map { sin(it.toDouble()) }.average()
            sqrt(x * x + y * y).coerceIn(0.0, 1.0)
        }

        val old = previous
        val compatible = old != null &&
            sample.timestampNanos > old.timestampNanos &&
            sample.timestampNanos > 0L &&
            sample.antennaCount > 0 &&
            sample.sampleRateHz > 0 &&
            old.antennaCount == sample.antennaCount &&
            old.sampleRateHz == sample.sampleRateHz &&
            old.amplitudes.size == rawAmplitudes.size
        if (old != null && sample.timestampNanos > 0L && sample.timestampNanos <= old.timestampNanos) {
            warnings += "Trama CSI fuera de orden; se omite el delta temporal."
        }
        val temporalDelta = if (compatible) {
            amplitudes.indices.map { abs(amplitudes[it] - old!!.amplitudes[it].toDouble()) }.average()
                .let { it / (mean + 1e-9) }
                .coerceIn(0.0, 10.0)
        } else null
        if (sample.timestampNanos > 0L && sample.antennaCount > 0 && sample.sampleRateHz > 0 &&
            (old == null || sample.timestampNanos > old.timestampNanos)
        ) {
            previous = PreviousFrame(
                timestampNanos = sample.timestampNanos,
                amplitudes = rawAmplitudes.copyOf(),
                antennaCount = sample.antennaCount,
                sampleRateHz = sample.sampleRateHz
            )
        }

        val validAntenna = sample.antennaCount > 0
        val validRate = sample.sampleRateHz > 0
        val quality = (0.5 * (amplitudes.size / 64.0).coerceIn(0.0, 1.0) +
            0.25 * (if (phase != null) 1.0 else 0.0) +
            0.15 * (if (validAntenna) 1.0 else 0.0) +
            0.10 * (if (validRate) 1.0 else 0.0)).coerceIn(0.0, 1.0)

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
