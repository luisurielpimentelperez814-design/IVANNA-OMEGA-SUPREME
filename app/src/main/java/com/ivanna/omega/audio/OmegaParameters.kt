package com.ivanna.omega.audio

data class OmegaParameters(
    var agcEnabled: Boolean = false,
    var inputTrimDb: Float = 0f,
    var outputTrimDb: Float = 0f
)
