package com.ivanna.omega.dsp

/**
 * DSP State — inmutable data class para todos los parámetros DSP.
 * © 2026 Luis Uriel Pimentel Pérez - GORE TNS. All rights reserved.
 */
data class DSPState(
    // Core parameters
    val drive: Float     = 0.65f,
    val wet: Float       = 0.50f,
    val mix: Float       = 0.70f,
    val alpha: Float     = 0.50f,
    val beta: Float      = 0.50f,
    val gamma: Float     = 0.50f,
    val freq: Float      = 1000f,
    val resonance: Float = 0.707f,

    // EQ gains (dB)
    val low: Float      = 0.0f,
    val mid: Float      = 0.0f,
    val high: Float     = 0.0f,
    val presence: Float = 0.0f,
    val master: Float   = 0.0f,

    // Compressor
    val compThreshold: Float = -18.0f,
    val compRatio: Float     =   4.0f,

    // Exciter
    val exciterDrive: Float = 0.3f,

    // Stereo
    val stereoWidth: Float = 1.0f,
    val makeupGain: Float  = 0.0f,

    // Bypass
    val bypass: Boolean = false
) {
    /** EQ gains array para JNI */
    val eqGains: FloatArray
        get() = floatArrayOf(low, mid, high, presence)

    /**
     * Envía todos los parámetros al DSP nativo vía DSPBridge.
     */
    fun pushToNative() {
        DSPBridge.setParams(
            drive, wet, mix,
            alpha, beta, gamma,
            freq, resonance,
            low, mid, high, presence, master
        )
    }

    companion object {
        // ── Campos estáticos del PF Engine (escritos desde AudioEngine) ──────
        @JvmField var pfDrive:     Float = 0.65f
        @JvmField var pfWet:       Float = 0.50f
        @JvmField var pfAlpha:     Float = 0.50f
        @JvmField var pfBeta:      Float = 0.50f
        @JvmField var pfDelta:     Float = 0.50f
        @JvmField var pfSigma:     Float = 0.50f
        @JvmField var pfFreq:      Float = 1000f
        @JvmField var pfResonance: Float = 0.707f
        @JvmField var pfMix:       Float = 0.70f
        @JvmField var pfLowGain:   Float = 0.0f
        @JvmField var pfMidGain:   Float = 0.0f
        @JvmField var pfHighGain:  Float = 0.0f
        @JvmField var pfPresence:  Float = 0.0f
        @JvmField var pfAmpModel:  Int   = 0

        /**
         * Slider [0..1] → ganancia dB [-18..+18]
         * Rango ampliado de ±12 a ±18 dB para mayor dinámica.
         */
        fun sliderToDb(slider: Float): Float = slider * 36f - 18f

        /**
         * Ganancia dB [-18..+18] → slider [0..1]
         */
        fun dbToSlider(db: Float): Float = (db + 18f) / 36f

        /**
         * Slider [0..1] → drive con curva logarítmica suave.
         * Permite control fino en valores bajos y agresivo en altos.
         * Rango efectivo: 0.0 (limpio) → 4.0 (saturación máxima).
         */
        fun sliderToDrive(slider: Float): Float =
            (Math.pow(slider.toDouble(), 2.0) * 4.0).toFloat().coerceIn(0f, 4f)

        /**
         * Drive [0..4] → slider [0..1]
         */
        fun driveToSlider(drive: Float): Float =
            Math.sqrt((drive / 4.0).coerceIn(0.0, 1.0)).toFloat()

        /** Slider [0..1] → frecuencia Hz [20..20000] (escala logarítmica) */
        fun sliderToFreq(slider: Float): Float =
            (20.0 * Math.pow(1000.0, slider.toDouble())).toFloat()

        /** Slider [0..1] → factor Q [0.1..10] (escala logarítmica) */
        fun sliderToQ(slider: Float): Float =
            (0.1 * Math.pow(100.0, slider.toDouble())).toFloat()
    }
}
