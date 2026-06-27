package com.ivanna.omega.dsp

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.ViewModel
import kotlin.math.log10
import kotlin.math.pow

class DSPState : ViewModel() {
    var drive     by mutableFloatStateOf(0.65f)
    var wet       by mutableFloatStateOf(0.50f)
    var mix       by mutableFloatStateOf(0.70f)
    var alpha     by mutableFloatStateOf(0.50f)
    var beta      by mutableFloatStateOf(0.50f)
    var gamma     by mutableFloatStateOf(0.50f)
    var freq      by mutableFloatStateOf(1000f)
    var resonance by mutableFloatStateOf(0.707f)
    var low       by mutableFloatStateOf(0f)
    var mid       by mutableFloatStateOf(0f)
    var high      by mutableFloatStateOf(0f)
    var presence  by mutableFloatStateOf(0f)
    var master    by mutableFloatStateOf(0f)

    fun pushToNative() {
        DSPBridge.setParams(drive, wet, mix, alpha, beta, gamma, freq, resonance, low, mid, high, presence, master)
    }

    companion object {
        fun sliderToDb(v: Float): Float  = v * 24f - 12f
        fun dbToSlider(db: Float): Float = (db + 12f) / 24f
        fun sliderToFreq(v: Float): Float = (20f * 1000.0.pow(v.toDouble())).toFloat()
        fun sliderToQ(v: Float): Float    = (0.1f * 100.0.pow(v.toDouble())).toFloat()
    }
}
