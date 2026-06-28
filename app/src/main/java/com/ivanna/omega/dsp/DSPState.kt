package com.ivanna.omega.dsp

/**
 * DSP State - Immutable data class for all DSP parameters
 * © 2026 Luis Uriel Pimentel Pérez - GORE TNS. All rights reserved.
 */
data class DSPState(
    // Core parameters
    val drive: Float = 0.65f,
    val wet: Float = 0.50f,
    val mix: Float = 0.70f,
    val alpha: Float = 0.50f,
    val beta: Float = 0.50f,
    val gamma: Float = 0.50f,
    val freq: Float = 1000f,
    val resonance: Float = 0.707f,

    // EQ gains
    val low: Float = 0.0f,
    val mid: Float = 0.0f,
    val high: Float = 0.0f,
    val presence: Float = 0.0f,
    val master: Float = 0.0f,

    // Compressor
    val compThreshold: Float = -18.0f,
    val compRatio: Float = 4.0f,

    // Exciter
    val exciterDrive: Float = 0.3f,

    // Stereo
    val stereoWidth: Float = 1.0f,
    val makeupGain: Float = 0.0f,

    // Bypass
    val bypass: Boolean = false
) {
    // EQ gains array for native
    val eqGains: FloatArray
        get() = floatArrayOf(low, mid, high, presence)

    /**
     * Push all parameters to native DSP
     */
    fun pushToNative() {
        val params = floatArrayOf(
            drive,
            wet,
            mix,
            alpha,
            beta,
            gamma,
            freq,
            resonance,
            low,
            mid,
            high,
            presence,
            master
        )
        IvannaNativeLib.nativeSetParams(params)
    }

    companion object {
        // PF Engine static properties (used by AudioEngine)
        var pfDrive: Float = 0.65f
        var pfWet: Float = 0.50f
        var pfAlpha: Float = 0.50f
        var pfBeta: Float = 0.50f
        var pfDelta: Float = 0.50f
        var pfSigma: Float = 0.50f
        var pfFreq: Float = 1000f
        var pfResonance: Float = 0.707f
        var pfMix: Float = 0.70f
        var pfLowGain: Float = 0.0f
        var pfMidGain: Float = 0.0f
        var pfHighGain: Float = 0.0f
        var pfPresence: Float = 0.0f
        var pfAmpModel: Int = 0

        /**
         * Convert dB slider value (-12..+12) to linear gain
         */
        fun sliderToDb(slider: Float): Float {
            return slider * 24f - 12f  // 0..1 -> -12..+12 dB
        }

        /**
         * Convert linear gain to dB slider value
         */
        fun dbToSlider(db: Float): Float {
            return (db + 12f) / 24f  // -12..+12 dB -> 0..1
        }

        /**
         * Convert slider (0..1) to frequency (20..20000 Hz)
         */
        fun sliderToFreq(slider: Float): Float {
            return 20f * kotlin.math.pow(1000f, slider)  // 20..20000 Hz
        }

        /**
         * Convert slider (0..1) to Q factor (0.1..10.0)
         */
        fun sliderToQ(slider: Float): Float {
            return 0.1f * kotlin.math.pow(100f, slider)  // 0.1..10.0
        }
    }
}
