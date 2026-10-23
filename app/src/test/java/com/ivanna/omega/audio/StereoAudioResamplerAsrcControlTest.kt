package com.ivanna.omega.audio

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.abs
import kotlin.math.sin

/** Deterministic software-ASRC contract tests; not physical DAC/HAL certification. */
class StereoAudioResamplerAsrcControlTest {
    @Test fun extremeCorrectionIsClampedToTheDocumentedBounds() {
        fun outputFrames(ppm: Double): Int {
            val r = StereoAudioResampler(48_000)
            r.setInputSampleRate(48_000)
            r.setClockCorrectionPpm(ppm)
            return r.process(tone(12_000, 997.0, 48_000)).size / 2
        }
        assertEquals(outputFrames(500.0), outputFrames(1.0e9))
        assertEquals(outputFrames(-500.0), outputFrames(-1.0e9))
    }

    @Test fun nonFiniteCorrectionFallsBackToNominalUnity() {
        val input = tone(4096, 997.0, 48_000)
        val invalid = StereoAudioResampler(48_000).apply {
            setInputSampleRate(48_000)
            setClockCorrectionPpm(Double.NaN)
        }.process(input)
        val unity = StereoAudioResampler(48_000).apply {
            setInputSampleRate(48_000)
        }.process(input)
        assertEquals(unity.size, invalid.size)
        for (i in unity.indices) assertEquals(unity[i], invalid[i], 0.0f)
    }

    @Test fun positiveCorrectionProducesMoreFramesThanNegativeCorrection() {
        val input = tone(24_000, 1000.0, 48_000)
        fun frames(ppm: Double): Int = StereoAudioResampler(48_000).run {
            setInputSampleRate(48_000)
            setClockCorrectionPpm(ppm)
            process(input).size / 2
        }
        assertTrue("positive ppm must increase output frame count", frames(500.0) > frames(-500.0))
    }

    @Test fun irregularChunksKeepFiniteOutputAndCumulativeFrameAccounting() {
        val inputRate = 44_100
        val outputRate = 48_000
        val totalFrames = 44_100
        val input = tone(totalFrames, 440.0, inputRate)
        val resampler = StereoAudioResampler(outputRate).apply { setInputSampleRate(inputRate) }
        val chunks = intArrayOf(127, 1024, 333, 2048, 511, 777, 19)
        var offset = 0
        var chunk = 0
        var produced = 0
        while (offset < totalFrames) {
            val count = minOf(chunks[chunk % chunks.size], totalFrames - offset)
            val output = resampler.process(input.copyOfRange(offset * 2, (offset + count) * 2))
            assertTrue("non-finite output at input frame $offset", output.all { it.isFinite() })
            produced += output.size / 2
            offset += count
            chunk++
        }
        val expected = totalFrames.toDouble() * outputRate / inputRate
        assertTrue("unexpected cumulative frame count: $produced vs $expected", abs(produced - expected) < 80.0)
    }

    @Test fun nonFiniteInputDoesNotPoisonFollowingBlocks() {
        val resampler = StereoAudioResampler(96_000).apply { setInputSampleRate(48_000) }
        val damaged = tone(4096, 1000.0, 48_000)
        damaged[200] = Float.NaN
        damaged[201] = Float.POSITIVE_INFINITY
        assertTrue(resampler.process(damaged).all { it.isFinite() })
        assertTrue(resampler.process(tone(4096, 1000.0, 48_000)).all { it.isFinite() })
    }

    @Test fun stereoChannelsRemainIndependent() {
        val resampler = StereoAudioResampler(96_000).apply { setInputSampleRate(48_000) }
        val input = FloatArray(4096 * 2)
        for (frame in 0 until 4096) input[frame * 2] = sin(frame * 0.03).toFloat()
        val output = resampler.process(input)
        assertTrue(output.all { it.isFinite() })
        for (i in output.indices step 2) assertTrue("left channel should remain zero", abs(output[i + 1]) < 1.0e-6f)
    }

    private fun tone(frames: Int, frequencyHz: Double, sampleRateHz: Int): FloatArray {
        val output = FloatArray(frames * 2)
        for (frame in 0 until frames) {
            val sample = sin(2.0 * Math.PI * frequencyHz * frame / sampleRateHz).toFloat()
            output[frame * 2] = sample
            output[frame * 2 + 1] = sample
        }
        return output
    }
}