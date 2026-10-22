package com.ivanna.omega.audio

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.PI
import kotlin.math.sin
import kotlin.math.sqrt

class StereoAudioResamplerFidelityTest {
    @Test
    fun downsamplingPreservesPassbandAndRejectsAliasSource() {
        val passband = resampleTone(inputRate = 48_000, targetRate = 24_000, frequency = 6_000.0)
        val stopband = resampleTone(inputRate = 48_000, targetRate = 24_000, frequency = 18_000.0)

        assertTrue("6 kHz passband RMS too low: $passband", passband in 0.65..0.76)
        assertTrue("18 kHz alias source leaked into output: RMS=$stopband", stopband < 0.03)
    }

    @Test
    fun downsamplingByTwoPreservesHighAudioBandAndRejectsImage() {
        val passband = resampleTone(inputRate = 96_000, targetRate = 48_000, frequency = 18_000.0)
        val stopband = resampleTone(inputRate = 96_000, targetRate = 48_000, frequency = 36_000.0)

        assertTrue("18 kHz passband RMS too low: $passband", passband in 0.65..0.76)
        assertTrue("36 kHz alias source leaked into output: RMS=$stopband", stopband < 0.03)
    }

    @Test
    fun unityRateRemainsBitTransparent() {
        val input = FloatArray(4096) { i -> sin(i * 0.017).toFloat() }
        val resampler = StereoAudioResampler(48_000)
        resampler.setInputSampleRate(48_000)
        val output = resampler.process(input)
        assertEquals(input.size, output.size)
        for (i in input.indices) assertEquals("sample $i", input[i], output[i], 0.0f)
    }

    private fun resampleTone(inputRate: Int, targetRate: Int, frequency: Double): Double {
        val frames = inputRate / 2
        val input = FloatArray(frames * 2)
        for (i in 0 until frames) {
            val sample = sin(2.0 * PI * frequency * i / inputRate).toFloat()
            input[i * 2] = sample
            input[i * 2 + 1] = sample
        }

        val resampler = StereoAudioResampler(targetRate)
        resampler.setInputSampleRate(inputRate)
        val output = resampler.process(input)
        val outputFrames = output.size / 2
        assertTrue("too few output frames: $outputFrames", outputFrames > targetRate / 4)

        // Ignore FIR startup transient and measure the left channel over the settled region.
        var power = 0.0
        var count = 0
        for (frame in 512 until outputFrames) {
            val sample = output[frame * 2].toDouble()
            power += sample * sample
            count++
        }
        assertTrue("no settled output frames", count > 0)
        return sqrt(power / count)
    }
}
