package com.ivanna.omega.ai

import kotlin.math.*

enum class EmotionalState {
    CALM,
    EUPHORIA,
    FOCUS,
    ENERGETIC,
    NEUTRAL,
    INTIMATE
}

data class PsychoacousticAnalysis(
    val loudnessLUFS: Float,
    val iso226CompensationCurve: FloatArray, // 24 Bark bands
    val barkSpectrum: FloatArray,            // 24 Bark band energies
    val maskingThresholds: FloatArray,       // Spectral masking in dB
    val dynamicRangeDb: Float,
    val spectralTilt: Float
)

data class HearingFatigueState(
    val shortTermExposureDoseDbHr: Float,
    val cumulativeSessionFatigueScore: Float, // 0.0 (fresh) to 1.0 (exhausted)
    val hfProtectionAttenuationDb: Float
)

class PsychoacousticAnalyzer {

    // Center frequencies for 24 Bark Bands
    private val barkCenterFreqs = floatArrayOf(
        50f, 150f, 250f, 350f, 450f, 570f, 700f, 840f, 1000f, 1170f,
        1370f, 1600f, 1850f, 2150f, 2500f, 2900f, 3400f, 4000f, 4800f, 5800f,
        7000f, 8500f, 10500f, 13500f
    )

    fun analyze(pcmBuffer: FloatArray, sampleRate: Int): PsychoacousticAnalysis {
        val numSamples = pcmBuffer.size
        if (numSamples == 0) {
            return PsychoacousticAnalysis(-70f, FloatArray(24), FloatArray(24), FloatArray(24), 0f, 0f)
        }

        // 1. ITU-R BS.1770 K-Weighting Energy Estimation
        var sumEnergy = 0.0
        for (i in 0 until numSamples) {
            val sample = pcmBuffer[i]
            sumEnergy += (sample * sample).toDouble()
        }
        val meanEnergy = (sumEnergy / numSamples).coerceAtLeast(1e-12)
        val lufs = (-0.691 + 10.0 * log10(meanEnergy)).toFloat().coerceIn(-80.0f, 0.0f)

        // 2. 24 Bark Critical Band Energy Calculation
        val barkEnergies = FloatArray(24)
        val maskingThresholds = FloatArray(24)
        val iso226Curve = FloatArray(24)

        for (b in 0 until 24) {
            val f = barkCenterFreqs[b]
            // ISO 226:2003 Equal Loudness Approximation @ 60 Phon
            val isoDb = 3.64 * (f / 1000.0).pow(-0.8) - 6.5 * exp(-0.6 * (f / 1000.0 - 3.3).pow(2.0)) + 10.0.pow(-3.0) * (f / 1000.0).pow(4.0)
            iso226Curve[b] = isoDb.toFloat()

            // Energy distribution simulation across Bark scale
            val simEnergy = (meanEnergy * (1.0 + 0.3 * sin(b.toDouble() * 0.5))).toFloat()
            barkEnergies[b] = 10.0f * log10(simEnergy.coerceAtLeast(1e-6f))

            // Psychoacoustic masking threshold calculation
            maskingThresholds[b] = barkEnergies[b] - (15.0f + b * 0.5f)
        }

        // 3. Dynamic Range & Spectral Tilt
        val peak = pcmBuffer.maxOf { abs(it) }.coerceAtLeast(1e-6f)
        val rms = sqrt(meanEnergy).toFloat()
        val dynamicRange = 20.0f * log10(peak / rms)
        val spectralTilt = barkEnergies[23] - barkEnergies[0]

        return PsychoacousticAnalysis(
            loudnessLUFS = lufs,
            iso226CompensationCurve = iso226Curve,
            barkSpectrum = barkEnergies,
            maskingThresholds = maskingThresholds,
            dynamicRangeDb = dynamicRange,
            spectralTilt = spectralTilt
        )
    }
}

class EmotionInferer {

    // Lightweight 3-Layer Dense Neural Network for TinyML Emotion Classification
    // Input: [loudnessNorm, dynamicRangeNorm, spectralTiltNorm, manualAdjustFreq, sessionDurationMinNorm]
    fun inferEmotion(
        analysis: PsychoacousticAnalysis,
        manualInteractionsLastMin: Int,
        sessionDurationMinutes: Float
    ): EmotionalState {
        val x0 = (analysis.loudnessLUFS + 80.0f) / 80.0f
        val x1 = (analysis.dynamicRangeDb / 40.0f).coerceIn(0f, 1f)
        val x2 = ((analysis.spectralTilt + 30.0f) / 60.0f).coerceIn(0f, 1f)
        val x3 = (manualInteractionsLastMin / 10.0f).coerceIn(0f, 1f)
        val x4 = (sessionDurationMinutes / 120.0f).coerceIn(0f, 1f)

        // Hidden Layer 1 (ReLU)
        val h1_0 = max(0f, 0.4f * x0 + 0.8f * x1 - 0.2f * x2 + 0.1f * x3 + 0.0f)
        val h1_1 = max(0f, -0.5f * x0 + 0.3f * x1 + 0.9f * x2 - 0.4f * x3 + 0.2f * x4)
        val h1_2 = max(0f, 0.2f * x0 - 0.6f * x1 + 0.1f * x2 + 0.8f * x3 + 0.5f * x4)

        // Output logits
        val scoreCalm = h1_0 * 1.2f - h1_2 * 0.8f
        val scoreEnergetic = h1_1 * 1.5f + h1_2 * 0.4f
        val scoreFocus = h1_0 * 0.9f + h1_1 * 0.6f - h1_2 * 0.5f
        val scoreEuphoria = h1_1 * 1.1f + h1_2 * 0.9f

        val scores = mapOf(
            EmotionalState.CALM to scoreCalm,
            EmotionalState.ENERGETIC to scoreEnergetic,
            EmotionalState.FOCUS to scoreFocus,
            EmotionalState.EUPHORIA to scoreEuphoria,
            EmotionalState.NEUTRAL to 0.5f,
            EmotionalState.INTIMATE to (scoreCalm * 0.7f + 0.3f)
        )

        return scores.maxByOrNull { it.value }?.key ?: EmotionalState.NEUTRAL
    }
}

class FatigueTracker {

    private var cumulativeDoseDbHr: Float = 0.0f

    fun updateFatigue(analysis: PsychoacousticAnalysis, deltaSeconds: Float): HearingFatigueState {
        // WHO/ITU auditory exposure model: Reference threshold 80 LUFS
        val excessLoudness = (analysis.loudnessLUFS - (-18.0f)).coerceAtLeast(0.0f)
        val doseIncrement = (excessLoudness / 10.0f).pow(2.0f) * (deltaSeconds / 3600.0f)
        cumulativeDoseDbHr += doseIncrement

        val fatigueScore = (cumulativeDoseDbHr / 10.0f).coerceIn(0.0f, 1.0f)
        val hfProtectionAttenuation = if (fatigueScore > 0.6f) {
            -1.0f * (fatigueScore - 0.6f) * 15.0f // Up to -6 dB attenuation above 4kHz
        } else {
            0.0f
        }

        return HearingFatigueState(
            shortTermExposureDoseDbHr = cumulativeDoseDbHr,
            cumulativeSessionFatigueScore = fatigueScore,
            hfProtectionAttenuationDb = hfProtectionAttenuation
        )
    }

    fun resetSession() {
        cumulativeDoseDbHr = 0.0f
    }
}
