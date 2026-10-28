package com.ivanna.omega.audio

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class AudioTrackBufferPllTest {
    @Test fun producerAheadReducesOutputRateWithBoundedSlew() {
        val pll = AudioTrackBufferPll(48_000)
        var timeNs = 1_000_000_000L
        assertEquals(0.0, pll.observe(8_000, 0, 10_000, timeNs), 0.0)
        repeat(8) {
            timeNs += 100_000_000L
            pll.observe(8_000, 0, 10_000, timeNs)
        }
        assertTrue("full queue must reduce output frame production", pll.correctionPpm < 0.0)
        assertTrue("correction must remain bounded", pll.correctionPpm in -500.0..500.0)
        assertTrue("integrator must not cause a sudden correction", pll.correctionPpm >= -200.0)
        assertTrue(pll.isLocked)
        assertEquals(8_000L, pll.occupancyFrames)
        assertEquals(5_000L, pll.targetOccupancyFrames)
    }

    @Test fun consumerAheadIncreasesOutputRate() {
        val pll = AudioTrackBufferPll(48_000)
        var timeNs = 1_000_000_000L
        pll.observe(2_000, 0, 10_000, timeNs)
        repeat(5) {
            timeNs += 100_000_000L
            pll.observe(2_000, 0, 10_000, timeNs)
        }
        assertTrue("low queue must increase output frame production", pll.correctionPpm > 0.0)
        assertTrue(pll.correctionPpm <= 125.0)
    }

    @Test fun smallBufferErrorInsideDeadbandDoesNotHunt() {
        val pll = AudioTrackBufferPll(48_000)
        var timeNs = 1_000_000_000L
        // Capacity 10,000; target 5,000; 5,100 is only 1% above target.
        var submitted = 5_100L
        var consumed = 0L
        pll.observe(submitted, consumed, 10_000, timeNs)
        repeat(20) {
            timeNs += 100_000_000L
            submitted += 4_800L
            consumed += 4_800L
            val correction = pll.observe(submitted, consumed, 10_000, timeNs)
            assertEquals("deadband suppresses small occupancy noise", 0.0, correction, 0.0)
        }
    }

    @Test fun sustainedSaturationRemainsBounded() {
        val pll = AudioTrackBufferPll(48_000)
        var timeNs = 1_000_000_000L
        pll.observe(9_000, 0, 10_000, timeNs)
        repeat(300) {
            timeNs += 100_000_000L
            assertTrue(pll.observe(9_000, 0, 10_000, timeNs) in -500.0..500.0)
        }
        assertEquals(-500.0, pll.correctionPpm, 0.0)
    }

    @Test fun invalidQueueAndTimestampFailClosed() {
        val pll = AudioTrackBufferPll(48_000)
        pll.observe(8_000, 0, 10_000, 1_000_000_000L)
        pll.observe(8_000, 0, 10_000, 1_100_000_000L)
        assertTrue(pll.correctionPpm < 0.0)

        assertEquals(0.0, pll.observe(10, 20, 10_000, 1_200_000_000L), 0.0)
        assertEquals(0.0, pll.correctionPpm, 0.0)
        assertTrue(!pll.isLocked)
    }

    @Test fun rollingRegressionMeasuresPositiveConsumerClockDriftAndFeedsForward() {
        val pll = AudioTrackBufferPll(48_000)
        var timeNs = 1_000_000_000L
        var consumed = 0L
        repeat(20) {
            // 48,048 frames per second = +1,000 ppm against 48 kHz nominal.
            val submitted = consumed + 5_000L
            pll.observe(submitted, consumed, 10_000, timeNs)
            consumed += 12_012L
            timeNs += 250_000_000L
        }
        assertTrue("regression must lock after paired frame/time samples", pll.isDriftLocked)
        assertEquals(48_048.0, pll.measuredRateHz, 0.5)
        assertEquals(1_000.0, pll.measuredDriftPpm, 10.0)
        assertTrue("faster consumer needs positive resampler production correction", pll.correctionPpm > 0.0)
        assertTrue(pll.correctionPpm <= 500.0)
    }

    @Test fun timestampRegressionResetsClockAndActuatorToUnity() {
        val pll = AudioTrackBufferPll(48_000)
        pll.observe(5_000, 0, 10_000, 2_000_000_000L)
        repeat(6) { i ->
            pll.observe(5_000 + (i + 1) * 12_000L, (i + 1) * 12_000L, 10_000,
                2_000_000_000L + (i + 1) * 250_000_000L)
        }
        assertEquals(0.0, pll.observe(1_000, 100, 10_000, 1_000_000_000L), 0.0)
        assertEquals(0.0, pll.correctionPpm, 0.0)
        assertTrue(!pll.isLocked)
    }

    @Test fun outputCorrectionAlwaysStaysWithinFiveHundredPpm() {
        val pll = AudioTrackBufferPll(48_000)
        var timeNs = 1_000_000_000L
        pll.observe(9_000, 0, 10_000, timeNs)
        repeat(200) {
            timeNs += 100_000_000L
            val value = pll.observe(9_000, 0, 10_000, timeNs)
            assertTrue(value in -500.0..500.0)
        }
        assertEquals(-500.0, pll.correctionPpm, 0.0)
    }
}
