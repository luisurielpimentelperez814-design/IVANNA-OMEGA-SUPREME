package com.ivanna.omega.visualizer

import com.ivanna.omega.core.NativeLibraryLoader

object IvannaVisualizerBark64Native {
    val isLoaded: Boolean = NativeLibraryLoader.ensureLoaded()
    external fun nativeCreate(sampleRate: Float): Long
    external fun nativeDestroy(handle: Long)
    external fun nativeReset(handle: Long)
    external fun nativeProcessBlock(handle: Long, monoBuffer: java.nio.FloatBuffer, numFrames: Int)
    external fun nativeSampleInto(handle: Long, dst: FloatArray)
}
