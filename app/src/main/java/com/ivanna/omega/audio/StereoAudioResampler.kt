package com.ivanna.omega.audio

class StereoAudioResampler(
    private val targetRate: Int = 96000
) {
    private var inputRate = 96000
    private var ratio = 1.0

    fun setInputSampleRate(rate: Int) {
        inputRate = rate
        ratio = targetRate.toDouble() / rate.toDouble()
    }

    fun process(input: FloatArray): FloatArray {
        if (inputRate == targetRate) return input

        val frames = input.size / 2
        val outFrames = (frames * ratio).toInt()
        val out = FloatArray(outFrames * 2)

        for (i in 0 until outFrames) {
            val src = ((i / ratio).toInt() * 2).coerceAtMost(input.size - 2)
            out[i*2] = input[src]
            out[i*2+1] = input[src+1]
        }
        return out
    }
}
