package com.ivanna.omega.core

/**
 * JNI bindings for evolutionary kernel, phase oracle, and spatial engine.
 * Compiled into libivanna_omega.so — same lib as DSPBridge/PiLstmBridge.
 */
object IvannaNativeLib {
    init {
        try { System.loadLibrary("ivanna_omega") }
        catch (e: UnsatisfiedLinkError) { android.util.Log.e("IvannaNativeLib", "Failed", e) }
    }

    // Evolutionary kernel
    external fun nativeInitializeEvolution(populationSize: Int, generations: Int): Boolean
    external fun nativeGetBestFitness(): Double
    external fun nativeGetGeneration(): Int
    external fun nativeEvolveStep(): Boolean

    // Phase oracle
    external fun nativePredictSamples(audioBuffer: FloatArray, sampleCount: Int): FloatArray
    external fun nativeGetPhaseState(): Float
    external fun nativeSetPhaseParameters(alpha: Float, beta: Float, gamma: Float): Boolean

    // Spatial engine
    external fun nativeInitSpatialEngine(sampleRate: Int, bufferSize: Int): Boolean
    external fun nativeRenderSpatialBlock(
        inputBuffer: FloatArray, outL: FloatArray, outR: FloatArray,
        posX: Int, posY: Int, posZ: Int, mu: Int
    ): Int
    external fun nativeReleaseSpatialEngine(): Boolean
    external fun nativeGetSpatialState(): String
    external fun nativeSetSpatialParams(params: String): Boolean
}
