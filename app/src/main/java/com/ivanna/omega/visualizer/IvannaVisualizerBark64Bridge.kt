package com.ivanna.omega.visualizer

import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer
import java.util.concurrent.atomic.AtomicLong

object IvannaVisualizerBark64Bridge {
    const val BAND_COUNT = 64
    private val handle = AtomicLong(0L)
    private var monoBuf: FloatBuffer? = null
    private var maxFrames = 0

    val isReady: Boolean get() = handle.get() != 0L

    fun init(sampleRate: Int, maxBlockFrames: Int) {
        if (handle.get() != 0L) return
        // FIX (desconexión): nativeCreate() se llamaba sin verificar que la
        // librería nativa del visualizador estuviera cargada. Si la carga
        // falló (ABI, lib ausente), UnsatisfiedLinkError tumbaba el hilo UI.
        if (!IvannaVisualizerBark64Native.isLoaded) return
        val h = try {
            IvannaVisualizerBark64Native.nativeCreate(sampleRate.toFloat())
        } catch (t: Throwable) { return }
        if (h == 0L) return
        maxFrames = maxBlockFrames
        monoBuf = ByteBuffer.allocateDirect(maxBlockFrames * Float.SIZE_BYTES)
            .order(ByteOrder.nativeOrder())
            .asFloatBuffer()
        handle.set(h)
    }

    fun processBlock(mono: FloatArray, numFrames: Int) {
        val h = handle.get()
        if (h == 0L || numFrames <= 0 || numFrames > maxFrames) return
        val buf = monoBuf ?: return
        buf.clear()
        buf.put(mono, 0, numFrames)
        buf.flip()
        runCatching { IvannaVisualizerBark64Native.nativeProcessBlock(h, buf, numFrames) }
    }

    fun sampleInto(dst: FloatArray) {
        // A complete 64-band frame is required by FotoHoloMath. Clear first so a
        // missing handle or failed JNI call can never make stale bands look live.
        if (dst.size < BAND_COUNT) {
            dst.fill(0f)
            return
        }
        java.util.Arrays.fill(dst, 0, BAND_COUNT, 0f)
        val h = handle.get()
        if (h == 0L) return

        try {
            IvannaVisualizerBark64Native.nativeSampleInto(h, dst)
        } catch (_: Throwable) {
            // JNI may have written only part of the frame before failing.
            java.util.Arrays.fill(dst, 0, BAND_COUNT, 0f)
        }
    }

    fun reset() {
        val h = handle.get()
        if (h != 0L) runCatching { IvannaVisualizerBark64Native.nativeReset(h) }
    }

    fun release() {
        val h = handle.getAndSet(0L)
        if (h != 0L) runCatching { IvannaVisualizerBark64Native.nativeDestroy(h) }
        monoBuf = null
    }
}
