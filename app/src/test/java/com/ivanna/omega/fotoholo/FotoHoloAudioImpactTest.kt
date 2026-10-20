package com.ivanna.omega.fotoholo

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class FotoHoloAudioImpactTest {
    @Test
    fun depthEndpointsMapToTheSafeDistanceRange() {
        assertEquals(0.5f, FotoHoloAudioImpact.distanceMetersFromDepth(20f)!!, 1e-6f)
        assertEquals(2.0f, FotoHoloAudioImpact.distanceMetersFromDepth(110f)!!, 1e-6f)
        assertEquals(1.25f, FotoHoloAudioImpact.distanceMetersFromDepth(65f)!!, 1e-6f)
    }

    @Test
    fun depthOutOfRangeIsClampedNotExtrapolated() {
        assertEquals(0.5f, FotoHoloAudioImpact.distanceMetersFromDepth(-500f)!!, 1e-6f)
        assertEquals(2.0f, FotoHoloAudioImpact.distanceMetersFromDepth(9_999f)!!, 1e-6f)
    }

    @Test
    fun distanceIsMonotonicInDepth() {
        var prev = -1f
        for (d in 20..110 step 5) {
            val v = FotoHoloAudioImpact.distanceMetersFromDepth(d.toFloat())!!
            assertTrue(v > prev); prev = v
        }
    }

    @Test
    fun nonFiniteInputsNeverReachTheEngine() {
        assertNull(FotoHoloAudioImpact.distanceMetersFromDepth(Float.NaN))
        assertNull(FotoHoloAudioImpact.distanceMetersFromDepth(Float.POSITIVE_INFINITY))
        assertNull(FotoHoloAudioImpact.elevationRadFromDeg(Float.NaN))
    }

    @Test
    fun elevationIsConvertedToRadiansAndBoundedToFortyFiveDegrees() {
        assertEquals(0f, FotoHoloAudioImpact.elevationRadFromDeg(0f)!!, 1e-6f)
        assertEquals(Math.PI.toFloat() / 4f, FotoHoloAudioImpact.elevationRadFromDeg(45f)!!, 1e-5f)
        assertEquals(Math.PI.toFloat() / 4f, FotoHoloAudioImpact.elevationRadFromDeg(90f)!!, 1e-5f)
        assertEquals(-Math.PI.toFloat() / 4f, FotoHoloAudioImpact.elevationRadFromDeg(-200f)!!, 1e-5f)
        assertNotNull(FotoHoloAudioImpact.elevationRadFromDeg(10f))
    }
}
