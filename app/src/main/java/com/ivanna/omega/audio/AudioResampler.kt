package com.ivanna.omega.audio

import android.util.Log
import kotlin.math.PI
import kotlin.math.sin

/**
 * Resampler de alta calidad para procesamiento a 32-bit float / 192kHz
 * Implementa interpolación sinc con ventana de Kaiser para mínima distorsión
 */
class AudioResampler(
    private val targetSampleRate: Int = 192000,
    private val targetBitDepth: Int = 32
) {
    companion object {
        private const val TAG = "AudioResampler"
        private const val SINC_TAPS = 64
    }

    private var inputSampleRate: Int = 48000
    private var ratio: Double = 1.0
    private val sincTable = FloatArray(SINC_TAPS * 256)

    init {
        precomputeSincTable()
        Log.i(TAG, "Resampler inicializado: ${targetSampleRate}Hz @ ${targetBitDepth}-bit float")
    }

    fun setInputSampleRate(sampleRate: Int) {
        inputSampleRate = sampleRate
        ratio = targetSampleRate.toDouble() / sampleRate
        Log.d(TAG, "Ratio de resampling: $ratio (input: ${sampleRate}Hz -> output: ${targetSampleRate}Hz)")
    }

    fun upsample(input: FloatArray): FloatArray {
        if (ratio <= 1.0) return input
        
        val outputSize = (input.size * ratio).toInt()
        val output = FloatArray(outputSize)
        
        for (i in output.indices) {
            val srcPos = i / ratio
            val srcIndex = srcPos.toInt()
            val frac = srcPos - srcIndex
            
            var sum = 0.0f
            var weightSum = 0.0f
            
            for (tap in -SINC_TAPS/2 until SINC_TAPS/2) {
                val idx = srcIndex + tap
                if (idx >= 0 && idx < input.size) {
                    val weight = sinc((tap - frac).toFloat()) * kaiserWindow(tap.toFloat() / SINC_TAPS)
                    sum += input[idx] * weight
                    weightSum += weight
                }
            }
            
            output[i] = if (weightSum > 0) sum / weightSum else 0f
        }
        
        return output
    }

    fun downsample(input: FloatArray, targetRate: Int): FloatArray {
        val downRatio = targetRate.toDouble() / targetSampleRate
        if (downRatio >= 1.0) return input
        
        val outputSize = (input.size * downRatio).toInt()
        val output = FloatArray(outputSize)
        
        val filtered = lowPassFilter(input, targetRate.toFloat() / 2)
        
        for (i in output.indices) {
            val srcPos = i / downRatio
            val srcIndex = srcPos.toInt()
            output[i] = if (srcIndex < filtered.size) filtered[srcIndex] else 0f
        }
        
        return output
    }

    private fun sinc(x: Float): Float {
        return if (x == 0f) 1f else (sin(PI * x) / (PI * x)).toFloat()
    }

    private fun kaiserWindow(x: Float): Float {
        val beta = 8.6f
        val arg = beta * Math.sqrt(1.0 - x * x).toFloat()
        return (bessel0(arg) / bessel0(beta)).toFloat()
    }

    private fun bessel0(x: Float): Float {
        var sum = 1.0f
        var term = 1.0f
        for (i in 1..20) {
            term *= (x / (2 * i)) * (x / (2 * i))
            sum += term
        }
        return sum
    }

    private fun lowPassFilter(input: FloatArray, cutoff: Float): FloatArray {
        val output = FloatArray(input.size)
        val nyquist = targetSampleRate.toFloat() / 2
        val normalizedCutoff = cutoff / nyquist
        
        for (i in input.indices) {
            var sum = 0f
            for (j in -16..16) {
                val idx = i + j
                if (idx >= 0 && idx < input.size) {
                    val coeff = sinc(j * normalizedCutoff) * normalizedCutoff
                    sum += input[idx] * coeff
                }
            }
            output[i] = sum
        }
        
        return output
    }

    private fun precomputeSincTable() {
        for (i in sincTable.indices) {
            val x = (i - sincTable.size / 2) / 256f
            sincTable[i] = sinc(x)
        }
    }
}
