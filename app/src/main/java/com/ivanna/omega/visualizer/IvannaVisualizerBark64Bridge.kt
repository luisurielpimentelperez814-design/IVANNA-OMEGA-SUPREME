package com.ivanna.omega.visualizer

import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer
import android.os.SystemClock
import java.util.concurrent.atomic.AtomicLong

object IvannaVisualizerBark64Bridge {
    const val BAND_COUNT = 64
    private val handle = AtomicLong(0L)
    private val lastProcessedAtNs = AtomicLong(0L)
    private var monoBuf: FloatBuffer? = null
    private var maxFrames = 0

    val isReady: Boolean get() = handle.get() != 0L
    /** Monotonic time at which JNI finished consuming the last real PCM block. */
    val lastSampleTimestampMonotonicNs: Long get() = lastProcessedAtNs.get()

    @Synchronized
    fun init(sampleRate: Int, maxBlockFrames: Int) {
        if (sampleRate !in 8_000..192_000 || maxBlockFrames !in 1..8_192) return
        if (handle.get() != 0L) return
        // FIX (desconexión): nativeCreate() se llamaba sin verificar que la
        // librería nativa del visualizador estuviera cargada. Si la carga
        // falló (ABI, lib ausente), UnsatisfiedLinkError tumbaba el hilo UI.
        if (!IvannaVisualizerBark64Native.isLoaded) return
        val h = try {
            IvannaVisualizerBark64Native.nativeCreate(sampleRate.toFloat())
        } catch (t: Throwable) { return }
        if (h == 0L) return
        val buffer = runCatching {
            ByteBuffer.allocateDirect(maxBlockFrames * Float.SIZE_BYTES)
                .order(ByteOrder.nativeOrder())
                .asFloatBuffer()
        }.getOrElse {
            runCatching { IvannaVisualizerBark64Native.nativeDestroy(h) }
            return
        }
        maxFrames = maxBlockFrames
        monoBuf = buffer
        lastProcessedAtNs.set(0L)
        handle.set(h)
    }

    fun processBlock(mono: FloatArray, numFrames: Int) {
        val h = handle.get()
        if (h == 0L || numFrames <= 0 || numFrames > maxFrames || numFrames > mono.size) return
        val buf = monoBuf ?: return
        buf.clear()
        buf.put(mono, 0, numFrames)
        buf.flip()
        val consumed = runCatching {
            IvannaVisualizerBark64Native.nativeProcessBlock(h, buf, numFrames)
        }.isSuccess
        if (consumed) lastProcessedAtNs.set(SystemClock.elapsedRealtimeNanos())
    }

    fun sampleInto(dst: FloatArray, maxAgeMs: Long = 1_000L): Boolean {
        // A complete 64-band frame is required by FotoHoloMath. Clear first so a
        // missing handle or failed JNI call can never make stale bands look live.
        if (dst.size < BAND_COUNT) {
            dst.fill(0f)
            return false
        }
        java.util.Arrays.fill(dst, 0, BAND_COUNT, 0f)
        val h = handle.get()
        if (h == 0L) return false

        val maxAgeNs = maxAgeMs.coerceIn(0L, 10_000L) * 1_000_000L
        val producerTimestamp = lastProcessedAtNs.get()
        if (!com.ivanna.omega.fotoholo.FotoHoloMath.isFreshSample(
                SystemClock.elapsedRealtimeNanos(), producerTimestamp, maxAgeNs
            )) return false

        return try {
            IvannaVisualizerBark64Native.nativeSampleInto(h, dst)
            if (lastProcessedAtNs.get() == producerTimestamp &&
                com.ivanna.omega.fotoholo.FotoHoloMath.isFreshSample(
                    SystemClock.elapsedRealtimeNanos(), producerTimestamp, maxAgeNs
                )) true
            else {
                java.util.Arrays.fill(dst, 0, BAND_COUNT, 0f)
                false
            }
        } catch (_: Throwable) {
            // JNI may have written only part of the frame before failing.
            java.util.Arrays.fill(dst, 0, BAND_COUNT, 0f)
            false
        }
    }

    fun reset() {
        lastProcessedAtNs.set(0L)
        val h = handle.get()
        if (h != 0L) runCatching { IvannaVisualizerBark64Native.nativeReset(h) }
    }

    @Synchronized
    fun release() {
        lastProcessedAtNs.set(0L)
        val h = handle.getAndSet(0L)
        if (h != 0L) runCatching { IvannaVisualizerBark64Native.nativeDestroy(h) }
        monoBuf = null
        maxFrames = 0
    }
}
