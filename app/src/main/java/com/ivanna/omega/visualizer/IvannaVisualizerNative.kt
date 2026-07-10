package com.ivanna.omega.visualizer

import com.ivanna.omega.core.NativeLibraryLoader

object IvannaVisualizerNative {
    val isLoaded: Boolean = NativeLibraryLoader.ensureLoaded()

    external fun nativeVisCreate(sampleRate: Float): Long
    external fun nativeVisDestroy(handle: Long)
    external fun nativeVisReset(handle: Long)
    external fun nativeVisProcessBlock(handle: Long, monoBuffer: java.nio.FloatBuffer, numFrames: Int)
    external fun nativeVisSample(handle: Long): FloatArray

    /** Compensación de latencia AV (v3.0): informar latencia medida del pipeline de captura. */
    external fun nativeVisSetDeviceLatency(handle: Long, latencyMs: Float)
}
