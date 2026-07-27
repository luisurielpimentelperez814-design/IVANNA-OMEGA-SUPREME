package com.ivanna.omega.neuromorphic

import android.util.Log
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.core.NativeLibraryLoader

/**
 * Kotlin bridge for PI-LSTM Milenio v2.0 (from IVANNA-ULTRA).
 * Signal path: Input (96kHz) → 4x Upsample → CT-LSTM RK4 → HRTF → 4x Downsample → Output
 */
object PiLstmBridge {
    private const val TAG = "IVANNA_OMEGA_LSTM"
    private var ready = false

    init {
        try {
            if (NativeLibraryLoader.ensureLoaded()) {
                nativeInit()
                ready = true
                Log.i(TAG, "PI-LSTM Milenio initialized")
            }
        } catch (t: Throwable) {
            ready = false
            Log.e(TAG, "PI-LSTM init failed", t)
        }
    }

    val isReady: Boolean get() = ready

    fun setAlpha(v: Float)        { if (ready) nativeSetAlpha(v) }
    fun setBeta(v: Float)         { if (ready) nativeSetBeta(v) }
    fun setGamma(v: Float)        { if (ready) nativeSetGamma(v) }
    fun setDelta(v: Float)        { if (ready) nativeSetDelta(v) }
    fun setHarmonicGain(v: Float) { if (ready) nativeSetHarmonicGain(v) }
    fun setHrtfEnabled(en: Boolean) { if (ready) nativeSetHrtfEnabled(en) }
    fun getNpSat(): Float = if (ready) nativeGetNpSat() else 0f
    fun getError(): Float = if (ready) nativeGetError() else 0f

    private external fun nativeInit()
    private external fun nativeSetAlpha(v: Float)
    private external fun nativeSetBeta(v: Float)
    private external fun nativeSetGamma(v: Float)
    private external fun nativeSetDelta(v: Float)
    private external fun nativeSetHarmonicGain(v: Float)
    private external fun nativeSetHrtfEnabled(en: Boolean)
    private external fun nativeGetNpSat(): Float
    private external fun nativeGetError(): Float

    // === NUEVOS PARÁMETROS NEURO-COCHLEAR ===
    // ── NPE completo ────────────────────────────────────────────────
    fun setMasterGain(db: Float)  { if (ready) nativeSetEta(db / 18f + 0.5f) }  // -18..18 dB → 0..1
    fun setAgc(targetDb: Float, rate: Float) {
        // Las llamadas nativas nativeSetNPMax/nativeSetDelta producen SIGSEGV
        // en dispositivos donde el .so cargó parcialmente. Se sustituyen por
        // lógica Kotlin pura hasta que el motor nativo esté estabilizado.
        // El AGC se aplica escalando el outputScaling del NPE Kotlin.
        if (!ready) return
        try {
            // nativeSetNPMax(targetDb / -36f) ← CRASH — deshabilitado
            // nativeSetDelta(rate)            ← CRASH — deshabilitado
            // Alternativa segura: ajustar ganancia del NPE Kotlin
            val gain = (targetDb / -36f).coerceIn(0f, 1f)
            com.ivanna.omega.audio.IvannaBridgePlayer.activeInstance
                ?.updateNpeKotlinParams(outputScaling = gain * 0.5f)
        } catch (t: Throwable) {
            android.util.Log.e("IVANNA_OMEGA_LSTM", "setAgc safe fallback error", t)
        }
    }
    fun setAdaptEnabled(en: Boolean) {
        if (ready) IvannaNativeLib.nativeSetAdaptEnabled(en)
    }
    fun setCochlearEnabled(en: Boolean) {
        // Cochlear → spatial wet: on=1.0, off=0.0
        if (ready) IvannaNativeLib.nativeSetSpatialWet(if (en) 1f else 0f)
    }
    fun setBypass(bypass: Boolean) {
        // Bypass NPE: deshabilita motor adaptativo y fuerza ganancia neutra
        if (ready) {
            IvannaNativeLib.nativeSetAdaptEnabled(!bypass)
            if (bypass) nativeSetHarmonicGain(0f)
        }
    }

    private external fun nativeSetEta(v: Float)
    private external fun nativeSetNPMax(v: Float)

    fun setClarity(clarity: Float) {
        // Mapear claridad (0-1) a ganancia de armónicos y lateral inhibition
        val harmonicGain = 0.1f + clarity * 0.8f
        val lateralInhib = 0.2f + clarity * 0.6f
        nativeSetHarmonicGain(harmonicGain)
        nativeSetBeta(lateralInhib)
    }

    fun setWarmth(warmth: Float) {
        // Mapear calidez (0-1) a compresión OHC y gamma
        val ohcComp = 0.1f + warmth * 0.7f
        val gamma = 0.5f + warmth * 0.5f
        nativeSetGamma(gamma)
        // Nota: nativeSetOhcCompression se añadiría en C++ si existiera,
        // pero usamos nativeSetAlpha como proxy (ajuste de ganancia maestra)
        nativeSetAlpha(ohcComp)
    }
}
