package com.ivanna.omega.dsp

data class DSPState(
    var eqGains: FloatArray = FloatArray(8) { 0f },
    var compThreshold: Float = -20f,
    var compRatio: Float = 4f,
    var exciterDrive: Float = 1f,
    var stereoWidth: Float = 1f,
    var makeupGain: Float = 0f,
    var bypass: Boolean = false
) {
    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (javaClass != other?.javaClass) return false
        other as DSPState
        return eqGains.contentEquals(other.eqGains) &&
                compThreshold == other.compThreshold &&
                compRatio == other.compRatio &&
                exciterDrive == other.exciterDrive &&
                stereoWidth == other.stereoWidth &&
                makeupGain == other.makeupGain &&
                bypass == other.bypass
    }

    override fun hashCode(): Int {
        var result = eqGains.contentHashCode()
        result = 31 * result + compThreshold.hashCode()
        result = 31 * result + compRatio.hashCode()
        result = 31 * result + exciterDrive.hashCode()
        result = 31 * result + stereoWidth.hashCode()
        result = 31 * result + makeupGain.hashCode()
        result = 31 * result + bypass.hashCode()
        return result
    }
}
