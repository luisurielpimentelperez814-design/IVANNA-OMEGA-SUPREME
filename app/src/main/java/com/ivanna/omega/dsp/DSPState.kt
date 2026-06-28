package com.ivanna.omega.dsp

import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf

/**
 * DSP State - Mutable state holder for all DSP parameters
 * © 2026 Luis Uriel Pimentel Pérez - GORE TNS. All rights reserved.
 */
class DSPState {
    // Core parameters
    val drive = mutableFloatStateOf(0.65f)
    val wet = mutableFloatStateOf(0.50f)
    val mix = mutableFloatStateOf(0.70f)
    val alpha = mutableFloatStateOf(0.50f)
    val beta = mutableFloatStateOf(0.50f)
    val gamma = mutableFloatStateOf(0.50f)
    val freq = mutableFloatStateOf(1000f)
    val resonance = mutableFloatStateOf(0.707f)

    // EQ gains
    val low = mutableFloatStateOf(0.0f)
    val mid = mutableFloatStateOf(0.0f)
    val high = mutableFloatStateOf(0.0f)
    val presence = mutableFloatStateOf(0.0f)
    val master = mutableFloatStateOf(0.0f)

    // Compressor
    val compThreshold = mutableFloatStateOf(-18.0f)
    val compRatio = mutableFloatStateOf(4.0f)

    // Exciter
    val exciterDrive = mutableFloatStateOf(0.3f)

    // Stereo
    val stereoWidth = mutableFloatStateOf(1.0f)
    val makeupGain = mutableFloatStateOf(0.0f)

    // Bypass
    val bypass = mutableStateOf(false)

    // EQ gains array for native
    val eqGains: FloatArray
        get() = floatArrayOf(low.value, mid.value, high.value, presence.value)

    /**
     * Push all parameters to native DSP
     */
    fun pushToNative() {
        val params = floatArrayOf(
            drive.value,
            wet.value,
            mix.value,
            alpha.value,
            beta.value,
            gamma.value,
            freq.value,
            resonance.value,
            low.value,
            mid.value,
            high.value,
            presence.value,
            master.value
        )
        IvannaNativeLib.nativeSetParams(params)
    }

    /**
     * Reset to defaults
     */
    fun reset() {
        drive.value = 0.65f
        wet.value = 0.50f
        mix.value = 0.70f
        alpha.value = 0.50f
        beta.value = 0.50f
        gamma.value = 0.50f
        freq.value = 1000f
        resonance.value = 0.707f
        low.value = 0.0f
        mid.value = 0.0f
        high.value = 0.0f
        presence.value = 0.0f
        master.value = 0.0f
        compThreshold.value = -18.0f
        compRatio.value = 4.0f
        exciterDrive.value = 0.3f
        stereoWidth.value = 1.0f
        makeupGain.value = 0.0f
        bypass.value = false
        pushToNative()
    }
}
