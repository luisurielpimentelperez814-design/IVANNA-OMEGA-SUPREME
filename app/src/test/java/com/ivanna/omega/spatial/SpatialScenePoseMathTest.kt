package com.ivanna.omega.spatial

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertNotNull
import org.junit.Test

class SpatialScenePoseMathTest {
    @Test
    fun neutralPosePreservesDefaultStemCoordinates() {
        for (base in SpatialScenePoseMath.defaultStems) {
            val actual = SpatialScenePoseMath.transform(base, 0f, 0f, 1f)
            assertNotNull(actual)
            assertEquals(base.x, actual!!.x, 1e-5f)
            assertEquals(base.y, actual.y, 1e-5f)
            assertEquals(base.z, actual.z, 1e-5f)
            assertEquals(base.width, actual.width, 1e-5f)
        }
    }

    @Test
    fun azimuthRotatesHorizontalPlaneWithoutChangingVerticalCoordinate() {
        val base = SpatialStemPose(1f, 0f, 0.25f, 0.2f)
        val actual = SpatialScenePoseMath.transform(
            base, Math.PI.toFloat() / 2f, 0f, 1f
        )!!
        assertEquals(0f, actual.x, 1e-5f)
        assertEquals(1f, actual.y, 1e-5f)
        assertEquals(0.25f, actual.z, 1e-5f)
    }

    @Test
    fun elevationRotatesSourceTowardVerticalAxis() {
        val base = SpatialStemPose(0f, 0f, 1f, 0.2f)
        val actual = SpatialScenePoseMath.transform(
            base, 0f, Math.PI.toFloat() / 6f, 1f
        )!!
        assertEquals(0.5f, actual.x, 1e-5f)
        assertEquals(0f, actual.y, 1e-5f)
        assertEquals(kotlin.math.sqrt(3f) / 2f, actual.z, 1e-5f)
    }

    @Test
    fun distanceScaleIsAppliedAndCoordinatesRemainBounded() {
        val base = SpatialStemPose(1f, 0.5f, 0.5f, 0.2f)
        val actual = SpatialScenePoseMath.transform(base, 0f, 0f, 2f)!!
        assertEquals(2f, actual.x, 1e-5f)
        assertEquals(1f, actual.y, 1e-5f)
        assertEquals(1f, actual.z, 1e-5f)
        assertEquals(0.2f, actual.width, 1e-5f)
    }

    @Test
    fun nonFiniteAndOutOfRangeInputsAreRejected() {
        val base = SpatialScenePoseMath.defaultStems.first()
        assertNull(SpatialScenePoseMath.transform(base, Float.NaN, 0f, 1f))
        assertNull(SpatialScenePoseMath.transform(base, 0f, Float.POSITIVE_INFINITY, 1f))
        assertNull(SpatialScenePoseMath.transform(base, 0f, 0f, 0.1f))
        assertNull(SpatialScenePoseMath.transform(base, 4f, 0f, 1f))
    }
}
