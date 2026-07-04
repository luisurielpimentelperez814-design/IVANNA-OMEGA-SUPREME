package com.ivanna.omega.visualizer

import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer
import java.util.concurrent.atomic.AtomicLong

/**
 * IvannaVisualizerBridge — singleton compartido entre el hilo de audio
 * (PlaybackCaptureService, escribe vía processBlock) y el hilo GL
 * (VisualizerRenderer, lee vía sample()). El propio GLUniformBridge en C++
 * ya es lock-free (atomics con memory_order_relaxed/acquire-release); acá
 * solo se evita usar el handle antes de crearse o después de destruirse.
 */
object IvannaVisualizerBridge {
    private val handle = AtomicLong(0L)
    private var monoBuf: FloatBuffer? = null
    private var maxFrames = 0

    val isReady: Boolean get() = handle.get() != 0L

    fun init(sampleRate: Int, maxBlockFrames: Int) {
        if (handle.get() != 0L) return
        val h = IvannaVisualizerNative.nativeVisCreate(sampleRate.toFloat())
        if (h == 0L) return
        maxFrames = maxBlockFrames
        monoBuf = ByteBuffer.allocateDirect(maxBlockFrames * Float.SIZE_BYTES)
            .order(ByteOrder.nativeOrder())
            .asFloatBuffer()
        handle.set(h)
    }

    /** Llamado desde el hilo de audio con el downmix mono del bloque capturado. */
    fun processBlock(mono: FloatArray, numFrames: Int) {
        val h = handle.get()
        if (h == 0L || numFrames <= 0 || numFrames > maxFrames) return
        val buf = monoBuf ?: return
        buf.clear()
        buf.put(mono, 0, numFrames)
        buf.flip()
        IvannaVisualizerNative.nativeVisProcessBlock(h, buf, numFrames)
    }

    /**
     * Compensación de latencia A/V (v3.0): informa la latencia medida del
     * pipeline de captura (AudioPlaybackCapture/AAudio) para que el motor
     * nativo acelere el ataque de los smoothers y reduzca el lag visual
     * percibido. Llamar tras medir la latencia real (p.ej. al arrancar
     * PlaybackCaptureService).
     */
    fun setDeviceLatencyMs(latencyMs: Float) {
        val h = handle.get()
        if (h != 0L) IvannaVisualizerNative.nativeVisSetDeviceLatency(h, latencyMs)
    }

    /** Llamado desde el hilo GL en cada frame — [bass_pulse, mid_flow, high_flicker]. */
    fun sample(): FloatArray {
        val h = handle.get()
        return if (h != 0L) IvannaVisualizerNative.nativeVisSample(h) else FloatArray(3)
    }

    fun reset() {
        val h = handle.get()
        if (h != 0L) IvannaVisualizerNative.nativeVisReset(h)
    }

    fun release() {
        val h = handle.getAndSet(0L)
        if (h != 0L) IvannaVisualizerNative.nativeVisDestroy(h)
        monoBuf = null
    }
}
