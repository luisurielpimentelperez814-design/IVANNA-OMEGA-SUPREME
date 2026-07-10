package com.ivanna.omega.dsp

import kotlin.math.*

/**
 * Concert Mode — Simulación de reverberación con delay y feedback ajustable.
 * Crea sensación de sala en vivo.
 */
class ConcertMode(private val sampleRate: Float = 48000f) {
    private val delayBuffer = FloatArray((sampleRate * 0.5f).toInt()) // 500ms max
    private var writePos = 0
    private var readPos = 0
    var delayMs: Float = 80f      // 80ms = sala pequeña
    var feedback: Float = 0.4f    // 40% feedback
    var mix: Float = 0.3f         // 30% mezcla

    fun process(buffer: FloatArray) {
        val delaySamples = (delayMs / 1000f * sampleRate).toInt().coerceIn(1, delayBuffer.size - 1)
        for (i in buffer.indices) {
            readPos = (writePos - delaySamples + delayBuffer.size) % delayBuffer.size
            val delayed = delayBuffer[readPos]
            val output = buffer[i] + mix * delayed
            delayBuffer[writePos] = buffer[i] + feedback * delayed
            buffer[i] = output.coerceIn(-1f, 1f)
            writePos = (writePos + 1) % delayBuffer.size
        }
    }

    fun setRoomSize(size: Float) { // 0 = small, 1 = large
        delayMs = 40f + size * 160f
        feedback = 0.2f + size * 0.5f
        mix = 0.1f + size * 0.4f
    }
}
