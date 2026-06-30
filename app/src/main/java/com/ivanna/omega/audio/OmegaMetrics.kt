package com.ivanna.omega.audio

data class OmegaMetrics(
    var rmsLevel: Float = 0f,
    var peakLevel: Float = 0f,
    var clipCount: Int = 0
)
