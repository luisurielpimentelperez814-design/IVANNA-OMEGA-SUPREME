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
        assertTrue(servo.isLocked)
        assertEquals(48_002.0, servo.measuredRateHz, 0.01)
        assertTrue(servo.measuredDriftPpm > 0.0)
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
        assertTrue(!servo.isLocked)
        assertEquals(0.0, servo.measuredRateHz, 0.0)
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
    @Test fun roomCalibrationUsesBoundedEstimatesAndRejectsImpossibleBounds() {
        val valid = com.ivanna.omega.fotoholo.AcousticRoomCalibrationModel.estimate(5.0, 2.6, 4.0, azimuthDeg = 45.0)!!
        assertEquals(52.0, valid.volumeM3, 0.0001)
        assertTrue(valid.sabineRt60Seconds in 0.05..20.0)
        assertTrue(valid.eyringRt60Seconds in 0.05..20.0)
        assertTrue(valid.schroederHz.isFinite() && valid.schroederHz > 0.0)
        assertTrue(valid.spatialWidth in 0.75f..1.5f)
        assertEquals(45.0f, valid.spatialAngleDeg, 0.0001f)
        assertTrue(!valid.measuredRirAvailable)
        assertEquals(null, com.ivanna.omega.fotoholo.AcousticRoomCalibrationModel.estimate(0.0, 2.6, 4.0))
        val hard = com.ivanna.omega.fotoholo.AcousticRoomCalibrationModel.estimate(5.0, 2.6, 4.0, com.ivanna.omega.fotoholo.AcousticRoomCalibrationModel.materials[0])!!
        val treated = com.ivanna.omega.fotoholo.AcousticRoomCalibrationModel.estimate(5.0, 2.6, 4.0, com.ivanna.omega.fotoholo.AcousticRoomCalibrationModel.materials[2])!!
        assertTrue(treated.sabineRt60Seconds < hard.sabineRt60Seconds)
    }

}