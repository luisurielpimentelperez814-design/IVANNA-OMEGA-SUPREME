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
 *   1 = DSP + PI-LSTM      (adds CT-LSTM RK4 + HRTF binaural)
 *   2 = DSP + PI-LSTM + Spatial  (full triadic equilibrium)
 */
object OmegaEngine {
    private const val TAG = "IVANNA_OMEGA"

    fun init(context: Context) {
        val am = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
        val sr = am.getProperty(AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE)?.toIntOrNull() ?: 48000
        DSPBridge.init(sr)
        Log.i(TAG, "DSP initialized at ${sr}Hz")
        // PI-LSTM auto-initializes via its init block
    }

    /** 0 = DSP, 1 = DSP+LSTM, 2 = DSP+LSTM+Spatial */
    fun setMode(mode: Int) {
        if (mode in 0..2) nativeSetMode(mode)
    }

    fun getMode(): Int = nativeGetMode()

    private external fun nativeSetMode(mode: Int)
    private external fun nativeGetMode(): Int
}
