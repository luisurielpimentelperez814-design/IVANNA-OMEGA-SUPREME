package com.ivanna.omega.visualizer

import com.ivanna.omega.core.NativeLibraryLoader

/**
 * IvannaVisualizerNativeV2 — Declaración JNI con método sampleInto zero-alloc.
 *
 * [FIX-FREEZE-5.1] nativeVisV2SampleInto: rellena array pre-allocado en C++,
 * evitando la creación de jfloatArray en cada frame.
 */
object IvannaVisualizerNativeV2 {
    val isLoaded: Boolean = NativeLibraryLoader.ensureLoaded()

    external fun nativeVisV2Create(sampleRate: Float): Long
    external fun nativeVisV2Destroy(handle: Long)
    external fun nativeVisV2Reset(handle: Long)
    external fun nativeVisV2ProcessBlockFromNPE(handle: Long, monoBuffer: java.nio.FloatBuffer, numFrames: Int)

    /** [FIX-FREEZE-5.1] Zero-alloc: rellena dst in-place. */
    external fun nativeVisV2SampleInto(handle: Long, dst: FloatArray)

    /** Legacy: devuelve nuevo jfloatArray (menos eficiente). */
    external fun nativeVisV2Sample(handle: Long): FloatArray

    external fun nativeVisV2SetDeviceLatency(handle: Long, latencyMs: Float)
}
