package com.ivanna.omega.dsp

import android.util.Log
import com.ivanna.omega.core.NativeLibraryLoader

/**
 * IVANNA-OMEGA-SUPREME — DSP Bridge
 * Wraps libivanna_omega.so, providing the full DSP chain:
 *   GainStage → HarmonicExciter → Compressor → ParametricEQ → StereoWidener → GainStage(out)
 *
 * Source lineage: IVANNA-FUSION-PRO (all FIX patches applied)
 *
 * NOTA (audit v1.8.1): la carga de libivanna_omega.so se centralizó en
 * NativeLibraryLoader — antes cada bridge (DSPBridge, OmegaEngine,
 * IvannaNativeLib, AudioEngine) llamaba a System.loadLibrary por su
 * cuenta, generando warnings de re-carga y trabajo redundante en cold
 * start. Ahora el primer bridge que se toque paga el dlopen; los
 * siguientes solo consultan el flag idempotente.
 */
object DSPBridge {

    private const val TAG = "IVANNA_OMEGA_DSP"
    private val loaded: Boolean = NativeLibraryLoader.ensureLoaded().also { ok ->
        if (ok) Log.i(TAG, "libivanna_omega ready — ${nativeVersionSafe()}")
    }

    private fun nativeVersionSafe(): String = try {
        nativeVersion()
    } catch (e: UnsatisfiedLinkError) {
        "native version unavailable"
    }

    val isLoaded: Boolean get() = loaded

    // audit v1.8.1 (fix #5): idempotencia de init().
    // IVANNAApplication.onCreate() llama DSPBridge.init(48000) desde el
    // scope IO, PlaybackCaptureService.onCreate() lo llama otra vez, y
    // AudioForegroundService.onCreate() (que puede reiniciarse
    // independientemente con START_STICKY) lo llama una tercera vez.
    // El nativeInit del C++ no era idempotente sobre reallocaciones
    // internas (biquad states, envelope banks, gammatone). Hacer el
    // init efectivo solo la primera vez elimina reasignaciones + posibles
    // clicks/glitches en el bloque siguiente. Un reset explícito sigue
    // disponible vía reset().
    @Volatile private var initialized: Boolean = false
    @Volatile private var initSampleRate: Int = 0

    fun init(sampleRate: Int = 48000) {
        if (!loaded) return
        synchronized(this) {
            if (initialized && initSampleRate == sampleRate) return
            nativeInit(sampleRate)
            initialized = true
            initSampleRate = sampleRate
            Log.i(TAG, "nativeInit(${sampleRate}Hz) ejecutado (previo=$initSampleRate)")
        }
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
    private external fun nativeProcess(buf: FloatArray, numFrames: Int)
    private external fun nativeReset()
    private external fun nativeVersion(): String
}
