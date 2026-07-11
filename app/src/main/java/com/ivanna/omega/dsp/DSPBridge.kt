package com.ivanna.omega.dsp

import android.util.Log
import com.ivanna.omega.core.NativeLibraryLoader

/**
 * IVANNA-OMEGA-SUPREME — DSP Bridge
 * Wraps libivanna_omega.so, providing the full DSP chain:
 *   GainStage → HarmonicExciter → Compressor → ParametricEQ → StereoWidener → GainStage(out)
 *
 * Source lineage: IVANNA-FUSION-PRO (all FIX patches applied)
 */
object DSPBridge {

    private const val TAG = "IVANNA_OMEGA_DSP"
    private val loaded = NativeLibraryLoader.ensureLoaded()

    val isLoaded: Boolean get() = loaded

    fun init(sampleRate: Int = 48000) {
        if (loaded) nativeInit(sampleRate)
    }

    fun setParams(
        drive: Float, wet: Float, mix: Float,
        alpha: Float, beta: Float, gamma: Float,
        freq: Float, resonance: Float,
        low: Float, mid: Float, high: Float,
        presence: Float, master: Float
    ) {
        if (!loaded) return
        nativeSetParams(drive, wet, mix, alpha, beta, gamma, freq, resonance, low, mid, high, presence, master)
    }

    // FIX (tuning magistral): antes el ancho estéreo (DSPState.stereoWidth)
    // nunca llegaba al motor nativo — StereoWidener derivaba el ancho de
    // "gamma", que también controla el timing del compresor (colisión de
    // parámetros). Canal dedicado, sin relación con setParams()/gamma.
    fun setStereoWidth(width: Float) {
        if (!loaded) return
        nativeSetStereoWidth(width)
    }

    // FEATURE (Voice Protection): score 0..1 de voz detectada
    // (VoiceProtectionController, YamnetClassifier real). Canal dedicado,
    // mismo patrón que setStereoWidth.
    fun setVoiceProtectScore(score: Float) {
        if (!loaded) return
        nativeSetVoiceProtectScore(score)
    }

    fun process(buffer: FloatArray, numFrames: Int) {
        if (loaded) nativeProcess(buffer, numFrames)
    }

    fun reset() { if (loaded) nativeReset() }

    fun version(): String = if (loaded) nativeVersion() else "native unavailable"

    private external fun nativeInit(sampleRate: Int)
    private external fun nativeSetParams(
        drive: Float, wet: Float, mix: Float,
        alpha: Float, beta: Float, gamma: Float,
        freq: Float, resonance: Float,
        low: Float, mid: Float, high: Float,
        presence: Float, master: Float
    )
    private external fun nativeSetStereoWidth(width: Float)
    private external fun nativeSetVoiceProtectScore(score: Float)
    private external fun nativeProcess(buf: FloatArray, numFrames: Int)
    private external fun nativeReset()
    private external fun nativeVersion(): String
}
