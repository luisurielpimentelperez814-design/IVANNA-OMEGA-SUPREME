package com.ivanna.omega.audio

import kotlin.math.exp
import kotlin.math.PI

/**
 * Lightweight amplitude-independent three-band energy estimator for the 16 kHz
 * mono analysis window. This is a spectral heuristic, not a semantic speech/music
 * classifier; callers must label its scores accordingly.
 */
internal object AudioBandEnergyClassifier {
    data class Scores(val speech: Float, val music: Float, val bass: Float, val rms: Float)

    fun analyze(samples: FloatArray, sampleRate: Int = 16_000): Scores {
        if (samples.isEmpty() || sampleRate <= 0) return Scores(0f, 0f, 0f, 0f)

        val lowAlpha = (1.0 - exp(-2.0 * PI * 250.0 / sampleRate)).toFloat()
        val midAlpha = (1.0 - exp(-2.0 * PI * 4_000.0 / sampleRate)).toFloat()
        var low = 0f
        var midLowpass = 0f
        var bassEnergy = 0.0
        var midEnergy = 0.0
        var highEnergy = 0.0
        var sumSquares = 0.0

        for (sampleRaw in samples) {
            val sample = if (sampleRaw.isFinite()) sampleRaw.coerceIn(-1f, 1f) else 0f
            low += lowAlpha * (sample - low)
            midLowpass += midAlpha * (sample - midLowpass)
            val mid = midLowpass - low
            val high = sample - midLowpass
            bassEnergy += low.toDouble() * low
            midEnergy += mid.toDouble() * mid
            highEnergy += high.toDouble() * high
            sumSquares += sample.toDouble() * sample
        }

        val total = bassEnergy + midEnergy + highEnergy
        val rms = kotlin.math.sqrt(sumSquares / samples.size).toFloat()
        if (total <= 1e-12 || !total.isFinite()) return Scores(0f, 0f, 0f, rms)
        val bass = (bassEnergy / total).toFloat().coerceIn(0f, 1f)
        val mid = (midEnergy / total).toFloat().coerceIn(0f, 1f)
        val high = (highEnergy / total).toFloat().coerceIn(0f, 1f)
        return Scores(mid, (bass + high).coerceIn(0f, 1f), bass, rms)
    }
}
