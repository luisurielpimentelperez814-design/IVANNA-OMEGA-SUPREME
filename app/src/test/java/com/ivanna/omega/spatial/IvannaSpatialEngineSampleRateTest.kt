package com.ivanna.omega.spatial

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.sin

class IvannaSpatialEngineSampleRateTest {
    @Test fun delayLinesFollowConfiguredSampleRate() {
        val engine = IvannaSpatialEngine.createIsolated()
        try {
            engine.configureSampleRate(96_000)
            assertEquals(96_000, engine.configuredSampleRateHz)
            assertEquals(144, engine.itdDelayCapacitySamples)
            assertEquals(96, engine.crossfeedDelayCapacitySamples)
            engine.configureSampleRate(48_000)
            assertEquals(48_000, engine.configuredSampleRateHz)
            assertEquals(72, engine.itdDelayCapacitySamples)
            assertEquals(48, engine.crossfeedDelayCapacitySamples)
        } finally {
            engine.configureSampleRate(44_100)
        }
    }

    @Test fun processingRemainsFiniteAfterRateChange() {
        val engine = IvannaSpatialEngine.createIsolated()
        val left = FloatArray(1_900) { i -> sin(i * 0.03).toFloat() }
        val right = FloatArray(1_900) { i -> sin(i * 0.031).toFloat() }
        val outL = FloatArray(left.size)
        val outR = FloatArray(right.size)
        try {
            engine.configureSampleRate(96_000)
            engine.processStereoInput(left, right, outL, outR, left.size)
            assertTrue(outL.all { it.isFinite() })
            assertTrue(outR.all { it.isFinite() })
        } finally {
            engine.configureSampleRate(44_100)
        }
    }
}
