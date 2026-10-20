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

class FotoHoloRoomCatalogTest {
    @org.junit.Test fun `parsea el formato nativo de sala`() {
        val r = FotoHoloAudioImpact.parseRoomInfo(7, "rir_0007.wav|0.535|172.9|2.33|6.0x3.1x9.5")!!
        org.junit.Assert.assertEquals(7, r.idx)
        org.junit.Assert.assertEquals(0.535f, r.rt60S, 1e-4f)
        org.junit.Assert.assertEquals(9.5f, r.depthM, 1e-4f)
    }

    @org.junit.Test fun `una sala dudosa no entra al catalogo`() {
        org.junit.Assert.assertNull(FotoHoloAudioImpact.parseRoomInfo(0, null))
        org.junit.Assert.assertNull(FotoHoloAudioImpact.parseRoomInfo(0, "x|nan|10|1|1x1x1"))
        org.junit.Assert.assertNull(FotoHoloAudioImpact.parseRoomInfo(0, "x|0.5|10|1|1x1"))
        org.junit.Assert.assertNull(FotoHoloAudioImpact.parseRoomInfo(0, "x|0.5|10|1|1x0x1"))
        org.junit.Assert.assertNull(FotoHoloAudioImpact.parseRoomInfo(0, "corto"))
    }

    @org.junit.Test fun `el json es compacto y estable`() {
        val j = FotoHoloAudioImpact.catalogJson(listOf(FotoHoloAudioImpact.CatalogRoom(3, 0.72f, 120f, 8.4f, 4.2f, 10.7f)))
        org.junit.Assert.assertEquals("[[3,0.720,120.0,8.40,4.20,10.70]]", j)
    }
}
