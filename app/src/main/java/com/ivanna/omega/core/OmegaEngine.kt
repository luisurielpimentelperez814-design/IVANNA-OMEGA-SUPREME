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
 *   0 = DSP only            (FUSION-PRO chain: EQ+Comp+Exciter+Widener)
 *   1 = DSP + NHO           (Nonlinear Harmonic Oscillator + BiquadEnvelopeBank)
 *   2 = DSP + NHO + Spatial (Cue-Based ITD+ILD)
 *   3 = DSP + NHO + HRTF    (v3.1 SUPREME — convolución binaural con HRTF LUT)
 *
 * FIX v3.1 (PAR 2 tuning):
 *   - Modo 3 (SUPREME/HRTF) ya es un valor de primera clase en Kotlin. El
 *     núcleo nativo sigue aceptando sólo 0..2 en nativeSetMode (esto no se
 *     tocó para mantener compatibilidad binaria); modo 3 se traduce a
 *     modo 2 en el nativo + activación explícita del switch HRTF, todo
 *     centralizado aquí en vez de duplicar la lógica en MainActivity.
 *   - loaded/isLoaded se expone públicamente (antes era private) para que
 *     los callers puedan hacer guard sin depender de UnsatisfiedLinkError.
 *   - getModeName() devuelve etiqueta humana consistente con la UI.
 *   - Rangos de coerción unificados 0..3.
 */
object OmegaEngine {
    private const val TAG = "IVANNA_OMEGA"

    @Volatile private var loaded = false
    @Volatile private var lastMode: Int = 0

    /** Estado de carga de la lib nativa — expuesto para guards externos. */
    val isLoaded: Boolean get() = loaded

    /** Último modo lógico solicitado (0..3), preservando modo 3 aunque en
     *  nativo se traduzca a 2 + flag HRTF. */
    val currentMode: Int get() = lastMode

    init {
        try {
            System.loadLibrary("ivanna_omega")
            loaded = true
        } catch (e: UnsatisfiedLinkError) {
            Log.e(TAG, "Native lib unavailable for OmegaEngine: ${e.message}")
        }
    }

    fun init(context: Context) {
        val am = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
        val sr = am.getProperty(AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE)?.toIntOrNull() ?: 48000
        DSPBridge.init(sr)
        Log.i(TAG, "DSP initialized at ${sr}Hz")
        // NHO+CueBank+Spatial auto-initialize via PDEngine.init()
    }

    /**
     * 0 = DSP · 1 = DSP+NHO · 2 = DSP+NHO+Spatial · 3 = DSP+NHO+HRTF (SUPREME)
     *
     * El nativo actualmente soporta sólo 0..2 en nativeSetMode; para modo 3 se
     * envía 2 al nativo y se activa HRTF vía IvannaNativeLib.nativeSetHRTFEnabled.
     */
    fun setMode(mode: Int) {
        val safeMode = mode.coerceIn(0, 3)
        lastMode = safeMode
        if (!loaded) return
        try {
            val nativeMode = if (safeMode >= 3) 2 else safeMode
            nativeSetMode(nativeMode)
            // Modo 3 = SUPREME: HRTF ON. Modos 0..2 = HRTF OFF explícito para
            // que no quede colgado activo al bajar de modo.
            if (IvannaNativeLib.isLoaded) {
                IvannaNativeLib.nativeSetHRTFEnabled(safeMode >= 3)
            }
            Log.i(TAG, "setMode logical=$safeMode native=$nativeMode hrtf=${safeMode >= 3}")
        } catch (e: UnsatisfiedLinkError) {
            Log.e(TAG, "nativeSetMode unavailable", e)
        }
    }

    /** Devuelve el modo real reportado por el nativo (0..2). Para el modo
     *  lógico completo 0..3 usar [currentMode]. */
    fun getMode(): Int = if (!loaded) 0 else try {
        nativeGetMode()
    } catch (e: UnsatisfiedLinkError) {
        Log.e(TAG, "nativeGetMode unavailable", e)
        0
    }

    /** Etiqueta humana consistente con la UI (MOTOR OPE card). */
    fun getModeName(): String = when (lastMode) {
        1 -> "DSP + NHO"
        2 -> "DSP + NHO + Spatial"
        3 -> "DSP + NHO + HRTF (SUPREME)"
        else -> "DSP core"
    }

    private external fun nativeSetMode(mode: Int)
    private external fun nativeGetMode(): Int
}
