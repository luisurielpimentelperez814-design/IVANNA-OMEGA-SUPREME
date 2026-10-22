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

    @Test fun invalidQueueAndTimestampFailClosed() {
        val pll = AudioTrackBufferPll(48_000)
        pll.observe(8_000, 0, 10_000, 1_000_000_000L)
        pll.observe(8_000, 0, 10_000, 1_100_000_000L)
        assertTrue(pll.correctionPpm < 0.0)

        assertEquals(0.0, pll.observe(10, 20, 10_000, 1_200_000_000L), 0.0)
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
