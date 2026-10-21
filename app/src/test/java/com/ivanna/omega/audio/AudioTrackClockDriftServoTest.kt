package com.ivanna.omega.audio

import org.junit.Assert.assertEquals
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test

class AudioTrackClockDriftServoTest {
    @Test fun waitsForThreeValidLongWindowSamplesAndBoundsCorrection() {
        val servo = AudioTrackClockDriftServo(48_000)
        var frames = 0L
        var timeNs = 1_000_000_000L
        assertEquals(0.0, servo.observe(frames, timeNs), 0.0)
        repeat(4) {
            frames += 48_002L
            timeNs += 1_000_000_000L
            servo.observe(frames, timeNs)
        }
        assertTrue(servo.correctionPpm > 0.0)
        assertTrue(servo.correctionPpm <= 75.0)
        assertTrue(servo.correctionPpm <= 500.0)
    }

    @Test fun timestampRegressionResetsAndFailsClosedToUnity() {
        val servo = AudioTrackClockDriftServo(48_000)
        servo.observe(0L, 1_000_000_000L)
        servo.observe(48_002L, 2_000_000_000L)
        servo.observe(96_004L, 3_000_000_000L)
        servo.observe(144_006L, 4_000_000_000L)
        assertTrue(servo.correctionPpm > 0.0)
        assertEquals(0.0, servo.observe(1L, 3_500_000_000L), 0.0)
        assertEquals(0.0, servo.correctionPpm, 0.0)
    }

    @Test fun resamplerUsesCorrectionAndUnityPassThroughStaysBitTransparent() {
        val resampler = StereoAudioResampler(48_000)
        resampler.setInputSampleRate(48_000)
        val input = FloatArray(48_000 * 2) { if (it % 2 == 0) 0.25f else -0.25f }
        assertSame(input, resampler.process(input))
        resampler.setClockCorrectionPpm(100.0)
        val fasterClock = resampler.process(input)
        assertTrue(fasterClock.all { it.isFinite() })

        val slowerResampler = StereoAudioResampler(48_000)
        slowerResampler.setInputSampleRate(48_000)
        slowerResampler.setClockCorrectionPpm(-100.0)
        val slowerClock = slowerResampler.process(input)
        assertTrue(fasterClock.size > slowerClock.size)

        resampler.setClockCorrectionPpm(Double.NaN)
        assertSame(input, resampler.process(input))
    }
}
