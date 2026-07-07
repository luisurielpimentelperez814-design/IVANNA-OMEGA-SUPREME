package com.ivanna.omega.core

import android.content.Context
import android.media.AudioManager
import android.util.Log
import com.ivanna.omega.dsp.DSPBridge
import com.ivanna.omega.neuromorphic.PiLstmBridge

/**
 * IVANNA-OMEGA-SUPREME — Central engine façade.
 *
 * Processing modes:
 *   0 = DSP only           (FUSION-PRO chain: EQ+Comp+Exciter+Widener)
 *   1 = DSP + NHO           (Nonlinear Harmonic Oscillator + BiquadEnvelopeBank)
 *   2 = DSP + NHO + Spatial  (Cue-Based ITD+ILD)
 */
object OmegaEngine {
    private const val TAG = "IVANNA_OMEGA"

    // NOTA (audit v1.8.1): la carga se delegó a NativeLibraryLoader para
    // evitar tener 4-5 System.loadLibrary distintos en el proyecto.
    // ensureLoaded() es idempotente y thread-safe.
    private val loaded: Boolean = NativeLibraryLoader.ensureLoaded()

    fun init(context: Context) {
        val am = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
        val sr = am.getProperty(AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE)?.toIntOrNull() ?: 48000
        DSPBridge.init(sr)
        Log.i(TAG, "DSP initialized at ${sr}Hz")
        // NHO+CueBank+Spatial auto-initialize via PDEngine.init()
    }

    /** 0 = DSP, 1 = DSP+LSTM, 2 = DSP+LSTM+Spatial */
    fun setMode(mode: Int) {
        if (!loaded) return
        if (mode in 0..3) {
            try {
                nativeSetMode(mode)
            } catch (e: UnsatisfiedLinkError) {
                Log.e(TAG, "nativeSetMode unavailable", e)
            }
        }
    }

    fun getMode(): Int = if (!loaded) 0 else try {
        nativeGetMode()
    } catch (e: UnsatisfiedLinkError) {
        Log.e(TAG, "nativeGetMode unavailable", e)
        0
    }

    private external fun nativeSetMode(mode: Int)
    private external fun nativeGetMode(): Int
}
