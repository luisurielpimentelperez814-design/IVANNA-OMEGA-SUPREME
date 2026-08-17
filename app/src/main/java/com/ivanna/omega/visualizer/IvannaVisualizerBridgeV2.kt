package com.ivanna.omega.visualizer

import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer
import java.util.concurrent.atomic.AtomicLong

/**
 * IvannaVisualizerBridgeV2 — Bridge optimizado con zero-allocation sampling.
 *
 * [FIX-FREEZE-5.1] Nuevos métodos:
 *   - sampleInto(dst: FloatArray): rellena array existente, sin alloc.
 *   - sample(): mantiene compatibilidad pero delega a sampleInto.
 */
object IvannaVisualizerBridgeV2 {
    const val BAND_COUNT = 13

    private val handle = AtomicLong(0L)
    private var monoBuf: FloatBuffer? = null
    private var maxFrames = 0

    val isReady: Boolean get() = handle.get() != 0L

    fun init(sampleRate: Int, maxBlockFrames: Int) {
        if (handle.get() != 0L) return
        val h = IvannaVisualizerNativeV2.nativeVisV2Create(sampleRate.toFloat())
        if (h == 0L) return
        maxFrames = maxBlockFrames
        monoBuf = ByteBuffer.allocateDirect(maxBlockFrames * Float.SIZE_BYTES)
            .order(ByteOrder.nativeOrder())
            .asFloatBuffer()
        handle.set(h)
    }

    fun processBlockFromNPE(mono: FloatArray, numFrames: Int) {
        val h = handle.get()
        if (h == 0L || numFrames <= 0 || numFrames > maxFrames) return
        val buf = monoBuf ?: return
        buf.clear()
        buf.put(mono, 0, numFrames)
        buf.flip()
        IvannaVisualizerNativeV2.nativeVisV2ProcessBlockFromNPE(h, buf, numFrames)
    }

    fun setDeviceLatencyMs(latencyMs: Float) {
        val h = handle.get()
        if (h != 0L) IvannaVisualizerNativeV2.nativeVisV2SetDeviceLatency(h, latencyMs)
    }

    /**
     * [FIX-FREEZE-5.1] Zero-alloc sampling: rellena dst in-place.
     * dst debe tener al menos BAND_COUNT elementos.
     */
    fun sampleInto(dst: FloatArray) {
        val h = handle.get()
        if (h == 0L) {
            java.util.Arrays.fill(dst, 0, BAND_COUNT, 0f)
            return
        }
        IvannaVisualizerNativeV2.nativeVisV2SampleInto(h, dst)
    }

    /** Compatibilidad: devuelve nuevo array (legacy, preferir sampleInto). */
    fun sample(): FloatArray {
        val arr = FloatArray(BAND_COUNT)
        sampleInto(arr)
        return arr
    }

    fun reset() {
        val h = handle.get()
        if (h != 0L) IvannaVisualizerNativeV2.nativeVisV2Reset(h)
    }

    fun release() {
        val h = handle.getAndSet(0L)
        if (h != 0L) IvannaVisualizerNativeV2.nativeVisV2Destroy(h)
        monoBuf = null
    }
}
