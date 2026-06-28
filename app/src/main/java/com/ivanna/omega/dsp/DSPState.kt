package com.ivanna.omega.dsp

data class DSPState(
    var low: Float = 0f,
    var mid: Float = 0f,
    var high: Float = 0f,
    var presence: Float = 0f,
    var master: Float = 0f,
    var drive: Float = 0f,
    var wet: Float = 0f,
    var mix: Float = 1f,
    var alpha: Float = 0f,
    var beta: Float = 0f,
    var gamma: Float = 0f,
    var freq: Float = 1000f,
    var resonance: Float = 0.707f,
    var eqGains: FloatArray = FloatArray(8) { 0f },
    var compThreshold: Float = -20f,
    var compRatio: Float = 4f,
    var exciterDrive: Float = 1f,
    var stereoWidth: Float = 1f,
    var makeupGain: Float = 0f,
    var bypass: Boolean = false
) {
    companion object {
        fun sliderToFreq(slider: Float): Float {
            val clamped = slider.coerceIn(0f, 1f)
            return 20f * kotlin.math.pow(1000f, clamped)
        }
        fun sliderToQ(slider: Float): Float {
            val clamped = slider.coerceIn(0f, 1f)
            return 0.1f * kotlin.math.pow(100f, clamped)
        }
        fun dbToSlider(db: Float): Float {
            return ((db + 12f) / 24f).coerceIn(0f, 1f)
        }
        fun sliderToDb(slider: Float): Float {
            return (slider * 24f - 12f).coerceIn(-12f, 12f)
        }
    }
    fun pushToNative() {
        DSPBridge.setParams(
            drive = drive, wet = wet, mix = mix,
            alpha = alpha, beta = beta, gamma = gamma,
            freq = freq, resonance = resonance,
            low = low, mid = mid, high = high,
            presence = presence, master = master
        )
    }
    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (javaClass != other?.javaClass) return false
        other as DSPState
        return low == other.low && mid == other.mid && high == other.high &&
               presence == other.presence && master == other.master &&
               drive == other.drive && wet == other.wet && mix == other.mix &&
               alpha == other.alpha && beta == other.beta && gamma == other.gamma &&
               freq == other.freq && resonance == other.resonance &&
               eqGains.contentEquals(other.eqGains) &&
               compThreshold == other.compThreshold && compRatio == other.compRatio &&
               exciterDrive == other.exciterDrive && stereoWidth == other.stereoWidth &&
               makeupGain == other.makeupGain && bypass == other.bypass
    }
    override fun hashCode(): Int {
        var result = low.hashCode()
        result = 31 * result + mid.hashCode()
        result = 31 * result + high.hashCode()
        result = 31 * result + presence.hashCode()
        result = 31 * result + master.hashCode()
        result = 31 * result + drive.hashCode()
        result = 31 * result + wet.hashCode()
        result = 31 * result + mix.hashCode()
        result = 31 * result + alpha.hashCode()
        result = 31 * result + beta.hashCode()
        result = 31 * result + gamma.hashCode()
        result = 31 * result + freq.hashCode()
        result = 31 * result + resonance.hashCode()
        result = 31 * result + eqGains.contentHashCode()
        result = 31 * result + compThreshold.hashCode()
        result = 31 * result + compRatio.hashCode()
        result = 31 * result + exciterDrive.hashCode()
        result = 31 * result + stereoWidth.hashCode()
        result = 31 * result + makeupGain.hashCode()
        result = 31 * result + bypass.hashCode()
        return result
    }
}

