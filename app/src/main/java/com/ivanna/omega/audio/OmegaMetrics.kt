package com.ivanna.omega.audio

data class OmegaMetrics(
    var rmsLevel: Float = 0f,
    var peakLevel: Float = 0f,
    var clipCount: Int = 0,
    var cpuPercent: Float = 0f,
    var latencyMs: Float = 2.8f,
    var sampleRate: Int = 48000,
    var yamnetCategory: String = "—",
    var yamnetConfidence: Float = 0f,
    var dspActive: Boolean = false,
    var hrtfActive: Boolean = false,
    var spatialWidth: Float = 0f
)
