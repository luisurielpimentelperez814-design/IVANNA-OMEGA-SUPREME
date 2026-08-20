package com.ivanna.omega.dsp

import android.util.Log
import com.ivanna.omega.core.IvannaNativeLib
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

    fun init(sampleRate: Int = 96000) {
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

    fun setAdaptiveParams(params: FloatArray) {
        if (!loaded) return

        fun at(index: Int, default: Float): Float =
            if (index < params.size) params[index] else default

        setParams(
            drive = at(0, 0f),
            wet = at(1, 0f),
            mix = at(2, 0f),
            alpha = at(3, 0f),
            beta = at(4, 0f),
            gamma = at(5, 0f),
            freq = at(6, 1000f),
            resonance = at(7, 0.7f),
            low = at(8, 0f),
            mid = at(9, 0f),
            high = at(10, 0f),
            presence = at(11, 0f),
            master = at(12, 0f)
        )
    }

    fun process(buffer: FloatArray, numFrames: Int) {
        if (loaded) nativeProcess(buffer, numFrames)
    }

    fun reset() { if (loaded) nativeReset() }

    // ── AUDIT FIX PR 4: Métodos para que PerceptualCortex envíe parámetros DSP ──────
    /**
     * Aplicar ganancia calculada por PerceptualCortex.
     * Rango: 0.0..2.0 (0 = mute, 1 = unity, 2 = 6dB boost)
     *
     * @param gain ganancia perceptual calculada
     */
    fun applyPerceptualGain(gain: Float) {
        // FIX (desconexión): `loaded` refleja NativeLibraryLoader, pero estos
        // métodos llaman a IvannaNativeLib — otra librería con su propio
        // estado de carga. Si ésta falló, UnsatisfiedLinkError en caliente.
        if (!loaded || !IvannaNativeLib.isLoaded) return
        Log.d(TAG, "applyPerceptualGain: $gain")
        runCatching { IvannaNativeLib.nativeSetPerceptualGain(gain.coerceIn(0f, 2f)) }
    }

    /**
     * Aplicar compresión calculada por PerceptualCortex.
     * Rango: 0.0..1.0 (0 = no compression, 1 = máxima compresión)
     *
     * @param amount compresión calculada
     */
    fun applyCompressorAmount(amount: Float) {
        if (!loaded || !IvannaNativeLib.isLoaded) return
        Log.d(TAG, "applyCompressorAmount: $amount")
        runCatching { IvannaNativeLib.nativeSetCompressorAmount(amount.coerceIn(0f, 1f)) }
    }

    /**
     * Aplicar reducción de exciter calculada por PerceptualCortex.
     * Rango: 0.0..1.0 (0 = máximo exciter, 1 = sin exciter)
     *
     * @param amount reducción de exciter
     */
    fun applyExciterReduction(amount: Float) {
        if (!loaded || !IvannaNativeLib.isLoaded) return
        Log.d(TAG, "applyExciterReduction: $amount")
        runCatching { IvannaNativeLib.nativeSetExciterReduction(amount.coerceIn(0f, 1f)) }
    }

    /**
     * Aplicar ancho espacial calculado por PerceptualCortex.
     * Rango: 0.0..2.0 (0.5 = mono, 1.0 = stereo normal, 2.0 = extra wide)
     *
     * @param width ancho espacial calculado
     */
    fun applySpatialWidth(width: Float) {
        if (!loaded || !IvannaNativeLib.isLoaded) return
        Log.d(TAG, "applySpatialWidth: $width")
        runCatching { IvannaNativeLib.nativeSetSpatialWidth(width.coerceIn(0.5f, 2f)) }
    }

    /**
     * Aplicar EQ calculado por PerceptualCortex.
     * Rango: ±12 dB en 3 bandas (low, mid, high)
     *
     * @param lowDb ganancia en bajos
     * @param midDb ganancia en medios
     * @param highDb ganancia en altos
     */
    fun applyPerceptualEQ(lowDb: Float, midDb: Float, highDb: Float) {
        if (!loaded || !IvannaNativeLib.isLoaded) return
        Log.d(TAG, "applyPerceptualEQ: low=$lowDb, mid=$midDb, high=$highDb")
        runCatching {
            IvannaNativeLib.nativeSetPerceptualEQ(
                lowDb.coerceIn(-12f, 12f),
                midDb.coerceIn(-12f, 12f),
                highDb.coerceIn(-12f, 12f)
            )
        }
    }


    // ── Adaptive DSP State bridge ───────────────────────────────────────
    // Ruta:
    // Perceptual AI → AdaptiveDSPState → DSPBridge → JNI → DSP

    fun applyAdaptiveState(state: AdaptiveDSPState) {
        if (!loaded || !IvannaNativeLib.isLoaded) return

        applyPerceptualGain(state.gain)

        applyCompressorAmount(
            state.compressor
        )

        applyExciterReduction(
            state.exciter
        )

        applySpatialWidth(
            state.spatial
        )

        applyPerceptualEQ(
            state.lowEqDb,
            state.midEqDb,
            state.highEqDb
        )

        applyFatigueProtection(
            state.iso226Compensation,
            state.fatigueProtection
        )
    }

    private fun applyFatigueProtection(
        iso226: Float,
        fatigue: Float
    ) {
        if (!loaded || !IvannaNativeLib.isLoaded) return

        runCatching {
            IvannaNativeLib.nativeSetFatigueProtection(
                iso226.coerceIn(-12f, 12f),
                fatigue.coerceIn(0f, 1f)
            )
        }
    }

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
