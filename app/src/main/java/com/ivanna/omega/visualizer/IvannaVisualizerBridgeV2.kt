package com.ivanna.omega.visualizer

import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer
import java.util.concurrent.atomic.AtomicLong

/**
 * IvannaVisualizerBridgeV2 — como IvannaVisualizerBridge (v1), pero:
 *   - processBlockFromNPE() espera el mono downmix YA procesado por
 *     IvannaNpeEngine (el mismo buffer `mono` que PlaybackCaptureService ya
 *     calcula para v1, calculado después de processInterleavedStereo()).
 *   - sample() devuelve 13 valores (bandas crudas), no 3 agregados.
 *
 * Lock-free igual que v1: GLUniformBridgeV2 en C++ usa atomics
 * relaxed/acquire-release; acá solo se evita usar el handle antes de
 * crearse o después de destruirse.
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

    /** Llamado desde el hilo de audio con el mono downmix post-NPE. */
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

    /** Llamado desde el hilo GL en cada frame — 13 bandas crudas grave→agudo. */
    fun sample(): FloatArray {
        val h = handle.get()
        return if (h != 0L) IvannaVisualizerNativeV2.nativeVisV2Sample(h) else FloatArray(BAND_COUNT)
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
