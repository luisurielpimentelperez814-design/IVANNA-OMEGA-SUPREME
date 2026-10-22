package com.ivanna.omega.spatial

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.sin

class IvannaSpatialEngineSampleRateTest {
    @Test fun itdAndCrossfeedDelayScaleWithSelectedOutputClock() {
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

    @Test fun spatialFallbackRemainsFiniteAfterRateChange() {
        val engine = IvannaSpatialEngine.shared
        val inL = FloatArray(1_900) { i -> sin(i * 0.03).toFloat() }
        val inR = FloatArray(1_900) { i -> sin(i * 0.031).toFloat() }
        val outL = FloatArray(inL.size)
        val outR = FloatArray(inR.size)
        try {
            engine.configureSampleRate(96_000)
            engine.processStereoInput(inL, inR, outL, outR, inL.size)
            assertTrue(outL.all { it.isFinite() })
            assertTrue(outR.all { it.isFinite() })
        } finally {
            engine.configureSampleRate(44_100)
        }
    }
}
