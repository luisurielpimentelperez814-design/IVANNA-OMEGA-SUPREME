package com.ivanna.omega.ai

import org.json.JSONObject

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
    // FIX: default 1f causaba 100% saturacion tanh cada vez que la IA
    // tomaba una decision sin calcular harmonicGain explicitamente.
    // 0f = excitador BYPASS hasta que el usuario lo active.
    val harmonicGain: Float = 0f,
    val antiDolbyIntensity: Float = 0f,

    val roomSize: Float = 0f,
    val headTrackingEnabled: Boolean = false,

    val confidence: Float = 0f,
    val confidenceScore: Float = 0f,

    val executionLatencyMs: Float = 0f
) {
    fun toJson(): JSONObject = JSONObject().apply {
        put("compressorAmount", compressorAmount)
        put("exciterReduction", exciterReduction)
        put("eqLowDb", eqLowDb)
        put("eqMidDb", eqMidDb)
        put("eqHighDb", eqHighDb)
        put("eqHighCut", eqHighCut)
        put("spatialWidth", spatialWidth)
        put("spatialMode", spatialMode)
        put("loudnessTargetLUFS", loudnessTargetLUFS)
        put("fatigueProtectionDb", fatigueProtectionDb)
        put("moodAdaptation", moodAdaptation)
        put("harmonicGain", harmonicGain)
        put("antiDolbyIntensity", antiDolbyIntensity)
        put("roomSize", roomSize)
        put("headTrackingEnabled", headTrackingEnabled)
        put("confidence", confidence)
        put("executionLatencyMs", executionLatencyMs)
    }
}
