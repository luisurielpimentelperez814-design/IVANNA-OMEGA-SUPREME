package com.ivanna.omega.visualizer

object IvannaVisualizerNative {
    init {
        System.loadLibrary("ivanna_visualizer")
    }

    external fun nativeVisCreate(sampleRate: Float): Long
    external fun nativeVisDestroy(handle: Long)
    external fun nativeVisReset(handle: Long)
    external fun nativeVisProcessBlock(handle: Long, monoBuffer: java.nio.FloatBuffer, numFrames: Int)
    external fun nativeVisSample(handle: Long): FloatArray
}
