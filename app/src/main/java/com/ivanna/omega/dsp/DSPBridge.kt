package com.ivanna.omega.dsp

import android.content.Context
import android.content.SharedPreferences
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

    // FIX (persistencia 2026-09-01): los parámetros DSP se pierden al matar la app.
    // Se agrega SharedPreferences para guardar/restaurar estado entre sesiones.
    private const val PREFS_NAME = "ivanna_dsp_state_v1"
    private var prefs: SharedPreferences? = null

    fun initPreferences(context: Context) {
        prefs = context.applicationContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
    }

    fun saveState(drive: Float, wet: Float, mix: Float, master: Float, stereoWidth: Float) {
        prefs?.edit()?.apply {
            putFloat("drive", drive)
            putFloat("wet", wet)
            putFloat("mix", mix)
            putFloat("master", master)
            putFloat("stereoWidth", stereoWidth)
            apply()
        }
    }

    fun loadState(): Map<String, Float> {
        return mapOf(
            "drive" to (prefs?.getFloat("drive", 0.5f) ?: 0.5f),
            "wet" to (prefs?.getFloat("wet", 0.5f) ?: 0.5f),
            "mix" to (prefs?.getFloat("mix", 0.5f) ?: 0.5f),
            "master" to (prefs?.getFloat("master", 1.0f) ?: 1.0f),
            "stereoWidth" to (prefs?.getFloat("stereoWidth", 0.5f) ?: 0.5f)
        )
    }

    // FIX (auditoría 2026-08-24): el default 96000 era una trampa latente —
    // un call-site futuro que lo invocara sin argumento reintroduciría el bug
    // de SR fijo (filtros desafinados) ya reparado en IVANNAApplication y
    // BootRestoreReceiver. Sin default: el compilador obliga a pasar SR real.
    fun init(sampleRate: Int) {
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
        if (IvannaNativeLib.isLoaded) {
            runCatching {
                IvannaNativeLib.nativeSetParams(
                    floatArrayOf(drive, wet, mix, alpha, beta, gamma, freq, resonance, low, mid, high, presence, master)
                )
            }
        }
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

    /**
     * Estado REAL del efecto omega_effect.so en audioserver (Ruta B), leido del beacon que el
     * propio efecto mantiene. A diferencia de MagiskBridge.isDaemonRunning (solo prueba que el
     * proceso daemon vive), esto prueba que el efecto esta insertado/procesando.
     */
    enum class EffectState { UNAVAILABLE, NO_EFFECT, ENABLED_IDLE, PROCESSING }

    /** UNAVAILABLE => el beacon no se puede leer (DAC/SELinux o libreria no cargada): el llamador debe conservar su comportamiento previo. */
    fun effectState(idleMs: Int = 2000): EffectState {
        if (!loaded) return EffectState.UNAVAILABLE
        return when (runCatching { nativeEffectBeaconState(idleMs) }.getOrDefault(-1)) {
            0 -> EffectState.NO_EFFECT
            1 -> EffectState.ENABLED_IDLE
            2 -> EffectState.PROCESSING
            else -> EffectState.UNAVAILABLE
        }
    }

    // ── Reinyeccion diferencial de Ruta A (v2.5.0) ──────────────────────────
    // Output = Dry + g_banda * Delta; aqui solo se calcula el termino Delta*g (el Dry ya suena por la via original).
    fun reinjectPrepare(sampleRate: Int, maxFrames: Int) { if (loaded) nativeReinjectPrepare(sampleRate, maxFrames) }
    fun reinjectReset() { if (loaded) nativeReinjectReset() }
    /** Intensidad perceptual 0..1 (slider / rampa de arranque). */
    fun reinjectSetIntensity(v: Float) { if (loaded) nativeReinjectSetIntensity(v) }
    /** Latencia real que ve el oyente entre el original y lo reinyectado (ms). */
    fun reinjectSetLatencyMs(ms: Float) { if (loaded) nativeReinjectSetLatencyMs(ms) }
    /** Techos por banda (graves/medios/agudos), 0..1. */
    fun reinjectSetBandCaps(low: Float, mid: Float, high: Float) { if (loaded) nativeReinjectSetBandCaps(low, mid, high) }
    /** false => el motor nativo no esta listo; el llamador usa el delta simple. */
    fun reinjectProcess(dry: FloatArray, wet: FloatArray, out: FloatArray, numFrames: Int): Boolean =
        loaded && nativeReinjectProcess(dry, wet, out, numFrames)
    /** Telemetria real: [latMs, dspLag, lagConf, coh x3, delta/seco x3, ganancia x3, riesgoPeine, inmersion, techo, 0]. */
    fun reinjectTelemetry(out: FloatArray): Boolean = loaded && out.size >= 16 && nativeReinjectTelemetry(out) > 0

    private external fun nativeReinjectPrepare(sampleRate: Int, maxFrames: Int)
    private external fun nativeReinjectReset()
    private external fun nativeReinjectSetIntensity(v: Float)
    private external fun nativeReinjectSetLatencyMs(ms: Float)
    private external fun nativeReinjectSetBandCaps(lo: Float, mid: Float, hi: Float)
    private external fun nativeReinjectProcess(dry: FloatArray, wet: FloatArray, out: FloatArray, nFrames: Int): Boolean
    private external fun nativeReinjectTelemetry(out: FloatArray): Int

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
    private external fun nativeEffectBeaconState(idleMs: Int): Int
}
