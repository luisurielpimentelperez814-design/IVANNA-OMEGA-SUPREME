package com.ivanna.omega.visualizer

/**
 * IvannaVisualizerNativeV2 — declaración JNI del bridge de 13 bandas
 * (gammatone lattice) para wallpaper_v2.glsl. Coexiste con
 * IvannaVisualizerNative (v1); mismo .so consolidado (ivanna_omega),
 * no requiere loadLibrary adicional pero se llama por claridad/orden
 * de inicialización explícito si esta clase se carga antes que v1.
 */
object IvannaVisualizerNativeV2 {
    init {
        System.loadLibrary("ivanna_omega")
    }

    external fun nativeVisV2Create(sampleRate: Float): Long
    external fun nativeVisV2Destroy(handle: Long)
    external fun nativeVisV2Reset(handle: Long)
    external fun nativeVisV2ProcessBlockFromNPE(handle: Long, monoBuffer: java.nio.FloatBuffer, numFrames: Int)
    external fun nativeVisV2Sample(handle: Long): FloatArray
    external fun nativeVisV2SetDeviceLatency(handle: Long, latencyMs: Float)
}
