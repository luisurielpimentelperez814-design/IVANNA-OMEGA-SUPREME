package com.ivanna.omega.ai

data class DSPDecision(
    val compressorAmount: Float = 0f,
    val exciterReduction: Float = 0f,

    val eqLowDb: Float = 0f,
    val eqMidDb: Float = 0f,
    val eqHighDb: Float = 0f,
    val eqHighCut: Float = 20000f,

    val spatialWidth: Float = 1f,
    val spatialMode: String = "STEREO",

    val loudnessTarget: Float = -14f,
    val loudnessTargetLUFS: Float = -14f,

    val fatigueProtectionDb: Float = 0f,

    val moodAdaptation: Float = 0f,
    val harmonicGain: Float = 1f,
    val antiDolbyIntensity: Float = 0f,

    val roomSize: Float = 0f,
    val headTrackingEnabled: Boolean = false,

    val confidence: Float = 0f,
    val confidenceScore: Float = 0f,

    val executionLatencyMs: Float = 0f
)
