package com.ivanna.omega.dsp

data class AdaptiveDSPState(
    val gain: Float = 1.0f,
    val compressor: Float = 0f,
    val exciter: Float = 0f,
    val spatial: Float = 1f,

    val lowEqDb: Float = 0f,
    val midEqDb: Float = 0f,
    val highEqDb: Float = 0f,

    val iso226Compensation: Float = 0f,
    val fatigueProtection: Float = 0f
)
