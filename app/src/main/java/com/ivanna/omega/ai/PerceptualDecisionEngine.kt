package com.ivanna.omega.ai

import org.json.JSONObject
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min
import kotlin.math.tanh

/**
 * 12-Dimension Psychoacoustic Perceptual Snapshot.
 */
data class PerceptualSnapshot(
    val spectralCentroidHz: Float = 2450.0f,
    val loudnessLUFS: Float = -14.2f,
    val dynamicRangeDb: Float = 12.5f,
    val transientDensity: Float = 0.35f,
    val harmonicDistortionThd: Float = 0.002f,
    val earFatigueIndex: Float = 0.15f,
    val ambientNoiseDb: Float = -45.0f,
    val userMood: Float = 0.5f,              // 0.0 = relaxed, 1.0 = excited
    val listeningDurationMinutes: Float = 35.0f,
    val roomReverbRt60Ms: Float = 220.0f,
    val spatialWidthRatio: Float = 1.25f,
    val clippingRiskFactor: Float = 0.02f
) {
    fun toJson(): JSONObject {
        return JSONObject().apply {
            put("spectralCentroidHz", spectralCentroidHz.toDouble())
            put("loudnessLUFS", loudnessLUFS.toDouble())
            put("dynamicRangeDb", dynamicRangeDb.toDouble())
            put("transientDensity", transientDensity.toDouble())
            put("harmonicDistortionThd", harmonicDistortionThd.toDouble())
            put("earFatigueIndex", earFatigueIndex.toDouble())
            put("ambientNoiseDb", ambientNoiseDb.toDouble())
            put("userMood", userMood.toDouble())
            put("listeningDurationMinutes", listeningDurationMinutes.toDouble())
            put("roomReverbRt60Ms", roomReverbRt60Ms.toDouble())
            put("spatialWidthRatio", spatialWidthRatio.toDouble())
            put("clippingRiskFactor", clippingRiskFactor.toDouble())
        }
    }
}

/**
 * DSP Action Decision vector outputted by the Cognitive Cortex.
 */
data class DSPDecision(
    val targetGainDb: Float,
    val compThresholdDb: Float,
    val compRatio: Float,
    val harmonicExciteEven: Float,
    val harmonicExciteOdd: Float,
    val spatialMode: String, // "STEREO_WIDE", "SURROUND_3D", "HRTF_BINAURAL"
    val moodAdaptation: Float,
    val fatigueMitigationAlpha: Float,
    val lowPassCutoffHz: Float
) {
    fun toJson(): JSONObject {
        return JSONObject().apply {
            put("targetGainDb", targetGainDb.toDouble())
            put("compThresholdDb", compThresholdDb.toDouble())
            put("compRatio", compRatio.toDouble())
            put("harmonicExciteEven", harmonicExciteEven.toDouble())
            put("harmonicExciteOdd", harmonicExciteOdd.toDouble())
            put("spatialMode", spatialMode)
            put("moodAdaptation", moodAdaptation.toDouble())
            put("fatigueMitigationAlpha", fatigueMitigationAlpha.toDouble())
            put("lowPassCutoffHz", lowPassCutoffHz.toDouble())
        }
    }
}

/**
 * Two-Layer Cognitive Engine: Psychoacoustic Matrix + TinyML Perceptron Refiner with RL Q-Feedback.
 */
class PerceptualDecisionEngine {

    // TinyML 2-layer MLP Weights (Input: 12 dims, Hidden: 8, Output: 4 parameters)
    private val wHidden = Array(8) { FloatArray(12) { (Math.random().toFloat() - 0.5f) * 0.1f } }
    private val wOutput = Array(4) { FloatArray(8) { (Math.random().toFloat() - 0.5f) * 0.1f } }
    
    // Q-Learning Feedback Weight Adjuster
    private var qLearningFactor = 0.05f

    fun evaluate(snapshot: PerceptualSnapshot, profile: UserProfile, aggressiveness: Float = 0.5f): DSPDecision {
        // --- Layer 1: Psychoacoustic Analytical Rules ---
        var baseGainDb = profile.bassBoostDb * 0.8f
        if (snapshot.loudnessLUFS > -10.0f) {
            baseGainDb -= (snapshot.loudnessLUFS + 10.0f) * 0.5f
        }

        // Ear Fatigue Dampening
        val fatigueFactor = min(1.0f, max(0.0f, snapshot.earFatigueIndex * profile.fatigueSensitivity))
        val lowPassCutoff = 22000.0f - (fatigueFactor * 6000.0f)

        // Dynamic Compression
        val compThresh = -18.0f - (snapshot.transientDensity * 6.0f)
        val compRatio = 2.0f + (fatigueFactor * 2.0f) + (aggressiveness * 1.5f)

        // --- Layer 2: TinyML MLP Inference ---
        val inputs = floatArrayOf(
            snapshot.spectralCentroidHz / 10000.0f,
            (snapshot.loudnessLUFS + 30.0f) / 30.0f,
            snapshot.dynamicRangeDb / 20.0f,
            snapshot.transientDensity,
            snapshot.harmonicDistortionThd * 100.0f,
            fatigueFactor,
            (snapshot.ambientNoiseDb + 80.0f) / 80.0f,
            snapshot.userMood,
            min(1.0f, snapshot.listeningDurationMinutes / 120.0f),
            snapshot.roomReverbRt60Ms / 1000.0f,
            snapshot.spatialWidthRatio / 2.0f,
            snapshot.clippingRiskFactor
        )

        val hidden = FloatArray(8) { i ->
            var sum = 0.0f
            for (j in 0..11) sum += inputs[j] * wHidden[i][j]
            tanh(sum)
        }

        val mlpOut = FloatArray(4) { i ->
            var sum = 0.0f
            for (j in 0..7) sum += hidden[j] * wOutput[i][j]
            tanh(sum)
        }

        // Refine Parameters using MLP Output
        val moodColoration = snapshot.userMood * 0.25f + mlpOut[0] * 0.1f
        val evenHarmonics = 0.12f + (profile.bassBoostDb * 0.05f) + mlpOut[1] * 0.05f
        val oddHarmonics = 0.04f + (profile.trebleBoostDb * 0.02f) + mlpOut[2] * 0.02f

        val spatialMode = when {
            snapshot.spatialWidthRatio > 1.4f -> "HRTF_BINAURAL"
            snapshot.transientDensity > 0.5f -> "SURROUND_3D"
            else -> "STEREO_WIDE"
        }

        return DSPDecision(
            targetGainDb = min(6.0f, max(-12.0f, baseGainDb)),
            compThresholdDb = compThresh,
            compRatio = compRatio,
            harmonicExciteEven = max(0.0f, evenHarmonics),
            harmonicExciteOdd = max(0.0f, oddHarmonics),
            spatialMode = spatialMode,
            moodAdaptation = moodColoration,
            fatigueMitigationAlpha = fatigueFactor,
            lowPassCutoffHz = lowPassCutoff
        )
    }

    fun applyFeedback(userDeltaDb: Float) {
        // Simple Q-learning online update step for weights
        val reward = -abs(userDeltaDb)
        for (i in 0..3) {
            for (j in 0..7) {
                wOutput[i][j] += qLearningFactor * reward * 0.01f
            }
        }
    }
}
