package com.ivanna.omega.fotoholo

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class AcousticRoomCalibrationModelTest {
    @Test fun validRoomProducesBoundedSabineEyringAndSpatialCommands() {
        val result = AcousticRoomCalibrationModel.estimate(5.0, 2.6, 4.0, azimuthDeg = 45.0)
        assertNotNull(result)
        result!!
        assertEquals(52.0, result.volumeM3, 0.0001)
        assertTrue(result.sabineRt60Seconds in 0.05..20.0)
        assertTrue(result.eyringRt60Seconds in 0.05..20.0)
        assertTrue(result.schroederHz.isFinite() && result.schroederHz > 0.0)
        assertTrue(result.spatialWidth in 0.75f..1.5f)
        assertEquals(45.0f, result.spatialAngleDeg, 0.0001f)
        assertTrue(result.itdMicroseconds in 0.0..700.0)
        assertEquals(false, result.measuredRirAvailable)
    }

    @Test fun spatialAngleCommandMatchesNativeNinetyDegreeLimit() {
        val positive = AcousticRoomCalibrationModel.estimate(5.0, 2.6, 4.0, azimuthDeg = 135.0)!!
        val negative = AcousticRoomCalibrationModel.estimate(5.0, 2.6, 4.0, azimuthDeg = -135.0)!!
        assertEquals(90.0f, positive.spatialAngleDeg, 0.0001f)
        assertEquals(-90.0f, negative.spatialAngleDeg, 0.0001f)
        assertTrue(positive.itdMicroseconds > 0.0)
        assertTrue(negative.itdMicroseconds < 0.0)
        assertEquals(positive.itdMicroseconds, -negative.itdMicroseconds, 0.0001)
        assertTrue("rearward azimuth should not exceed side-on ITD", positive.itdMicroseconds < 500.0)
        val rear = AcousticRoomCalibrationModel.estimate(5.0, 2.6, 4.0, azimuthDeg = 180.0)!!
        assertEquals(0.0, rear.itdMicroseconds, 0.0001)
    }

    @Test fun interauralDelayPreservesDirectionAndIsZeroAtFront() {
        val left = AcousticRoomCalibrationModel.estimate(5.0, 2.6, 4.0, azimuthDeg = -45.0)!!
        val front = AcousticRoomCalibrationModel.estimate(5.0, 2.6, 4.0, azimuthDeg = 0.0)!!
        val right = AcousticRoomCalibrationModel.estimate(5.0, 2.6, 4.0, azimuthDeg = 45.0)!!
        assertTrue(left.itdMicroseconds < 0.0)
        assertEquals(0.0, front.itdMicroseconds, 0.0001)
        assertTrue(right.itdMicroseconds > 0.0)
        assertEquals(right.itdMicroseconds, -left.itdMicroseconds, 0.0001)
    }

    @Test fun rejectsNonPhysicalOrUnboundedDimensions() {
        assertNull(AcousticRoomCalibrationModel.estimate(0.0, 2.5, 4.0))
        assertNull(AcousticRoomCalibrationModel.estimate(40.0, 2.5, 4.0))
        assertNull(AcousticRoomCalibrationModel.estimate(Double.NaN, 2.5, 4.0))
    }

    @Test fun moreAbsorptionDoesNotIncreaseSabineRt60() {
        val hard = AcousticRoomCalibrationModel.estimate(
            5.0, 2.6, 4.0, AcousticRoomCalibrationModel.materials[0]
        )!!
        val treated = AcousticRoomCalibrationModel.estimate(
            5.0, 2.6, 4.0, AcousticRoomCalibrationModel.materials[2]
        )!!
        assertTrue(treated.sabineRt60Seconds < hard.sabineRt60Seconds)
    }
}
