package com.ivanna.omega.core

import android.util.Log

/**
 * JNI bindings for IVANNA OMEGA SUPREME native library.
 * Compiled into libivanna_omega.so — unified entry point for DSP, PI-LSTM,
 * evolutionary kernel, phase oracle, and spatial engine.
 */
object IvannaNativeLib {
    init {
        try {
            System.loadLibrary("ivanna_omega")
            Log.i("IvannaNativeLib", "Native library loaded successfully")
        } catch (e: UnsatisfiedLinkError) {
            Log.e("IvannaNativeLib", "Failed to load native library", e)
        }
    }

    // ═══════════════════════════════════════════════════════════════════════
    //  DSP Core (ivanna_omega_jni.cpp)
    // ═══════════════════════════════════════════════════════════════════════
    external fun nativeInitDSP(sampleRate: Int): Boolean
    external fun nativeProcessBlock(
        inL: FloatArray, inR: FloatArray,
        outL: FloatArray, outR: FloatArray,
        frames: Int
    )
    external fun nativeSetParams(params: FloatArray)
    external fun nativeResetDSP()

    // ═══════════════════════════════════════════════════════════════════════
    //  PI-LSTM Milenio (ivanna_omega_jni.cpp)
    // ═══════════════════════════════════════════════════════════════════════
    external fun nativeInitPILSTM()
    external fun nativeSetAlpha(v: Float)
    external fun nativeSetBeta(v: Float)
    external fun nativeSetGamma(v: Float)
    external fun nativeSetDelta(v: Float)
    external fun nativeSetEta(v: Float)
    external fun nativeSetHarmonicGain(v: Float)
    external fun nativeSetHRTFEnabled(en: Boolean)
    external fun nativeSetAdaptEnabled(en: Boolean)
    external fun nativeSetNPMax(v: Float)
    external fun nativeSetReflectionGain(i: Int, g: Float)
    external fun nativeSetReflectionDelay(i: Int, d: Float)

    // ═══════════════════════════════════════════════════════════════════════
    //  Evolutionary Kernel (evolutionary_kernel.cpp)
    // ═══════════════════════════════════════════════════════════════════════
    external fun nativeInitializeEvolution(populationSize: Int, generations: Int): Boolean
    external fun nativeGetBestFitness(): Double
    external fun nativeGetGeneration(): Int
    external fun nativeEvolveStep(): Boolean
    external fun nativeSetMutationRate(rate: Float)
    external fun nativeGetMutationRate(): Float

    // ═══════════════════════════════════════════════════════════════════════
    //  Phase Oracle (phase_oracle.cpp)
    // ═══════════════════════════════════════════════════════════════════════
    external fun nativePredictSamples(audioBuffer: FloatArray, sampleCount: Int): FloatArray
    external fun nativeGetPhaseState(): Float
    external fun nativeSetPhaseParameters(alpha: Float, beta: Float, gamma: Float): Boolean

    // ═══════════════════════════════════════════════════════════════════════
    //  Spatial Engine (spatial_jni.cpp)
    // ═══════════════════════════════════════════════════════════════════════
    external fun nativeInitSpatialEngine(sampleRate: Int, bufferSize: Int): Boolean
    external fun nativeRenderSpatialBlock(
        inputBuffer: FloatArray,
        outL: FloatArray, outR: FloatArray,
        posX: Int, posY: Int, posZ: Int, mu: Int
    ): Int
    external fun nativeReleaseSpatialEngine(): Boolean
    external fun nativeGetSpatialState(): String
    external fun nativeSetSpatialParams(params: String): Boolean
}
