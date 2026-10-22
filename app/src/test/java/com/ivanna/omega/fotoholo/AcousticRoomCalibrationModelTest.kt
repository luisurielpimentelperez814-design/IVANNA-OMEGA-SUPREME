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
        assertTrue(positive.itdMicroseconds in 0.0..700.0)
        assertTrue(negative.itdMicroseconds in 0.0..700.0)
        assertEquals(700.0, positive.itdMicroseconds, 0.0001)
        assertEquals(700.0, negative.itdMicroseconds, 0.0001)
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
