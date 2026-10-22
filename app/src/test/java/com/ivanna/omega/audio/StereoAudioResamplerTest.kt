package com.ivanna.omega.audio

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.sqrt
import kotlin.math.sin
import kotlin.math.cos

class StereoAudioResamplerTest {
    @Test fun unityRateWithoutClockCorrectionIsBitTransparent() {
        val input = FloatArray(4096) { i -> if (i % 2 == 0) sin(i * 0.017).toFloat() else cos(i * 0.013).toFloat() }
        val resampler = StereoAudioResampler(48_000)
        resampler.setInputSampleRate(48_000)
        val output = resampler.process(input)
        assertEquals(input.size, output.size)
        for (i in input.indices) assertEquals("sample $i", input[i], output[i], 0.0f)
    }

    @Test fun dcGainRemainsNearUnityAfterFilterSettles() {
        val resampler = StereoAudioResampler(96_000)
        resampler.setInputSampleRate(48_000)
        val input = FloatArray(48_000 * 2) { 0.75f }
        val output = resampler.process(input)
        assertTrue("output has enough settled samples", output.size > 20_000)
        var sum = 0.0
        var count = 0
        for (i in 512 * 2 until output.size step 2) {
            sum += output[i]
            count++
        }
        val mean = sum / count
        assertTrue("DC gain drifted: $mean", abs(mean - 0.75) < 0.01)
    }

    @Test fun downsamplingSuppressesToneAboveOutputNyquist() {
        val inputRate = 48_000
        val targetRate = 24_000
        val resampler = StereoAudioResampler(targetRate)
        resampler.setInputSampleRate(inputRate)
        val frames = 12_000
        val frequency = 18_000.0
        val input = FloatArray(frames * 2)
        for (i in 0 until frames) {
            val sample = sin(2.0 * PI * frequency * i / inputRate).toFloat()
            input[i * 2] = sample
            input[i * 2 + 1] = sample
        }
        val output = resampler.process(input)
        assertTrue("resampler returned too few frames: ${output.size / 2}", output.size / 2 > 4_000)
        var power = 0.0
        var count = 0
        for (i in 512 * 2 until output.size step 2) {
            power += output[i].toDouble() * output[i].toDouble()
            count++
        }
        val rms = sqrt(power / count)
        assertTrue("18 kHz alias was not sufficiently attenuated: rms=$rms", rms < 0.08)
    }

    @Test fun upsamplingPreservesHighAudioBandBelowInputNyquist() {
        val inputRate = 48_000
        val targetRate = 96_000
        val frequency = 18_000.0
        val frames = 24_000
        val resampler = StereoAudioResampler(targetRate)
        resampler.setInputSampleRate(inputRate)
        val input = FloatArray(frames * 2)
        for (i in 0 until frames) {
            val sample = sin(2.0 * PI * frequency * i / inputRate).toFloat()
            input[i * 2] = sample
            input[i * 2 + 1] = sample
        }
        val output = resampler.process(input)
        var power = 0.0
        var count = 0
        for (i in 2_000 until output.size step 2) {
            power += output[i].toDouble() * output[i].toDouble()
            count++
        }
        val rms = sqrt(power / count)
        assertTrue("18 kHz passband was attenuated during upsampling: rms=$rms", rms in 0.55..0.75)
    }

    @Test fun downsamplingPreservesHighBandBelowOutputNyquist() {
        val inputRate = 96_000
        val targetRate = 48_000
        val frequency = 18_000.0
        val frames = 48_000
        val resampler = StereoAudioResampler(targetRate)
        resampler.setInputSampleRate(inputRate)
        val input = FloatArray(frames * 2)
        for (i in 0 until frames) {
            val sample = (0.5 * sin(2.0 * PI * frequency * i / inputRate)).toFloat()
            input[i * 2] = sample
            input[i * 2 + 1] = sample
        }
        val output = resampler.process(input)
        var power = 0.0
        var count = 0
        for (i in 2_000 until output.size step 2) {
            power += output[i].toDouble() * output[i].toDouble()
            count++
        }
        val rms = sqrt(power / count)
        assertTrue("18 kHz passband was attenuated during 96→48 kHz downsampling: rms=$rms", rms in 0.30..0.40)
    }

    @Test fun chunkedProcessingPreservesPhaseAndOutputAgainstSingleBlock() {
        val inputRate = 44_100
        val targetRate = 96_000
        val frames = 8_192
        val input = FloatArray(frames * 2)
        for (i in 0 until frames) {
            val sample = (0.7 * sin(2.0 * PI * 997.0 * i / inputRate)).toFloat()
            input[2 * i] = sample
            input[2 * i + 1] = sample
        }

        val oneBlock = StereoAudioResampler(targetRate).apply {
            setInputSampleRate(inputRate)
        }.process(input)

        val chunkedResampler = StereoAudioResampler(targetRate).apply {
            setInputSampleRate(inputRate)
        }
        val chunks = ArrayList<FloatArray>()
        var offset = 0
        while (offset < frames) {
            val count = minOf(257, frames - offset)
            chunks += chunkedResampler.process(input.copyOfRange(offset * 2, (offset + count) * 2))
            offset += count
        }
        val chunked = FloatArray(chunks.sumOf { it.size })
        var write = 0
        for (chunk in chunks) {
            chunk.copyInto(chunked, write)
            write += chunk.size
        }

        assertEquals("output frame count changed across chunk boundaries", oneBlock.size, chunked.size)
        var maxError = 0.0
        for (i in oneBlock.indices) {
            maxError = maxOf(maxError, abs(oneBlock[i].toDouble() - chunked[i].toDouble()))
        }
        assertTrue("chunk boundary caused phase/discontinuity error: $maxError", maxError < 1.0e-4)
    }

    @Test fun unityRatePassThroughFallsBackWhenSamplesAreNonFinite() {
        val resampler = StereoAudioResampler(48_000)
        resampler.setInputSampleRate(48_000)
        val input = FloatArray(2048) { 0.1f }
        input[100] = Float.NaN
        val output = resampler.process(input)
        assertTrue("unity-rate fallback must sanitize non-finite samples", output.all { it.isFinite() })
    }

    @Test fun nonFiniteInputSamplesDoNotPoisonFilterHistory() {
        val resampler = StereoAudioResampler(96_000)
        resampler.setInputSampleRate(44_100)
        val input = FloatArray(4096) { i -> if (i % 2 == 0) 0.2f else -0.2f }
        input[128] = Float.NaN
        input[513] = Float.POSITIVE_INFINITY
        input[1024] = Float.NEGATIVE_INFINITY
        val output = resampler.process(input)
        assertTrue("output must remain finite after corrupt input samples", output.all { it.isFinite() })
        val nextBlock = resampler.process(FloatArray(4096) { 0.1f })
        assertTrue("bad samples must not poison retained FIR history", nextBlock.all { it.isFinite() })
    }

    @Test fun clockCorrectionIsFiniteAndBounded() {
        val resampler = StereoAudioResampler(96_000)
        resampler.setInputSampleRate(44_100)
        resampler.setClockCorrectionPpm(10_000.0)
        val input = FloatArray(8192) { i -> sin(i * 0.1).toFloat() }
        val output = resampler.process(input)
        assertTrue(output.isNotEmpty())
        assertTrue(output.all { it.isFinite() })
        resampler.setClockCorrectionPpm(Double.NaN)
        val resetOutput = resampler.process(input)
        assertTrue(resetOutput.all { it.isFinite() })
    }
}
