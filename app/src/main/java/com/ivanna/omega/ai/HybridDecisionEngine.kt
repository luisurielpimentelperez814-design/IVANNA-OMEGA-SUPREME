package com.ivanna.omega.ai

import kotlin.math.coerceIn

enum class SpatialMode {
    STEREO,
    SURROUND_5_1,
    BINAURAL_3D,
    ATMOS_OBJECTS
}

class QLearningAgent {
    // Q-Table: State [Emotion Index][Fatigue Bucket] -> Action [EQ Profile Index]
    private val qTable = Array(6) { FloatArray(5) { 0.0f } }
    private val learningRate = 0.1f
    private val discountFactor = 0.9f

    fun selectAction(emotion: EmotionalState, fatigueBucket: Int): Int {
        val emotionIdx = emotion.ordinal
        val actions = qTable[emotionIdx]
        var maxAction = 0
        var maxQ = actions[0]
        for (i in 1 until actions.size) {
            if (actions[i] > maxQ) {
                maxQ = actions[i]
                maxAction = i
            }
        }
        return maxAction
    }

    fun updateQ(emotion: EmotionalState, fatigueBucket: Int, action: Int, reward: Float) {
        val eIdx = emotion.ordinal
        val currentQ = qTable[eIdx][action]
        val newQ = currentQ + learningRate * (reward + discountFactor * currentQ - currentQ)
        qTable[eIdx][action] = newQ
    }
}

class HybridDecisionEngine {

    private val psychoacousticAnalyzer = PsychoacousticAnalyzer()
    private val emotionInferer = EmotionInferer()
    private val fatigueTracker = FatigueTracker()
    private val qAgent = QLearningAgent()

    fun computeDecision(
        pcmBuffer: FloatArray,
        sampleRate: Int,
        manualInteractions: Int,
        sessionDurationMin: Float,
        userBassPreference: Float, // -1.0 to +1.0
        userTreblePreference: Float
    ): DSPDecision {
        val psychoAnalysis = psychoacousticAnalyzer.analyze(pcmBuffer, sampleRate)
        val emotion = emotionInferer.inferEmotion(psychoAnalysis, manualInteractions, sessionDurationMin)
        val fatigue = fatigueTracker.updateFatigue(psychoAnalysis, 1.0f)

        val fatigueBucket = (fatigue.cumulativeSessionFatigueScore * 4).toInt().coerceIn(0, 4)
        val qAction = qAgent.selectAction(emotion, fatigueBucket)

        // 1. Rule-Based ISO 226 Loudness Compensation
        val lufsDeficit = (-14.0f - psychoAnalysis.loudnessLUFS).coerceIn(-12.0f, 12.0f)
        var eqLow = lufsDeficit * 0.4f + (userBassPreference * 4.0f)
        var eqMid = 0.0f
        var eqHigh = lufsDeficit * 0.2f + (userTreblePreference * 4.0f)

        // Apply Q-Agent action bias
        when (qAction) {
            1 -> { eqLow += 1.5f; eqHigh += 1.0f } // Warm Bass boost
            2 -> { eqMid += 2.0f }                // Vocal clarity
            3 -> { eqHigh += 2.5f }               // Air & Spatial
            4 -> { eqLow -= 2.0f; eqHigh -= 2.0f } // Relaxed/Smooth
        }

        // Apply Hearing Safety Fatigue Protection
        eqHigh += fatigue.hfProtectionAttenuationDb

        // 2. Dynamic Range Compression calculation
        val compression = if (psychoAnalysis.dynamicRangeDb > 20.0f) {
            ((psychoAnalysis.dynamicRangeDb - 20.0f) / 20.0f).coerceIn(0.1f, 0.8f)
        } else {
            0.05f
        }

        // 3. Mood Adaptation & Spatial Modes
        val (spatialWidth, mode, room) = when (emotion) {
            EmotionalState.EUPHORIA -> Triple(1.6f, SpatialMode.ATMOS_OBJECTS, 0.7f)
            EmotionalState.CALM -> Triple(1.1f, SpatialMode.BINAURAL_3D, 0.4f)
            EmotionalState.FOCUS -> Triple(0.9f, SpatialMode.STEREO, 0.1f)
            EmotionalState.ENERGETIC -> Triple(1.4f, SpatialMode.SURROUND_5_1, 0.5f)
            EmotionalState.INTIMATE -> Triple(1.2f, SpatialMode.BINAURAL_3D, 0.3f)
            EmotionalState.NEUTRAL -> Triple(1.0f, SpatialMode.STEREO, 0.2f)
        }

        val confidence = (0.85f + 0.10f * (1.0f - fatigue.cumulativeSessionFatigueScore)).coerceIn(0.5f, 0.99f)

        return DSPDecision(
            compressorAmount = compression,
            exciterReduction = (fatigue.cumulativeSessionFatigueScore * 0.5f).coerceIn(0.0f, 0.8f),
            eqLowDb = eqLow.coerceIn(-12f, 12f),
            eqMidDb = eqMid.coerceIn(-12f, 12f),
            eqHighDb = eqHigh.coerceIn(-12f, 12f),
            spatialWidth = spatialWidth,
            loudnessTargetLUFS = -14.0f,
            fatigueProtectionDb = fatigue.hfProtectionAttenuationDb,
            moodAdaptation = 0.2f * emotion.ordinal,
            spatialMode = mode,
            roomSize = room,
            headTrackingEnabled = (mode == SpatialMode.BINAURAL_3D || mode == SpatialMode.ATMOS_OBJECTS),
            confidenceScore = confidence
        )
    }

    fun provideUserFeedbackReward(emotion: EmotionalState, fatigueBucket: Int, action: Int, positiveReward: Boolean) {
        val reward = if (positiveReward) +1.0f else -1.5f
        qAgent.updateQ(emotion, fatigueBucket, action, reward)
    }
}
