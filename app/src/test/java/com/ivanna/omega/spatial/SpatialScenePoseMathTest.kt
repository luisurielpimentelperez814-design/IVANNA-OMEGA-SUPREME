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
            assertEquals(base.gain, actual.gain, 1e-5f)
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
        assertEquals(1f / kotlin.math.sqrt(2f), actual.gain, 1e-5f)
    }

    @Test
    fun stemFramesAreConvertedToObjectMajorPlanarStereoWithoutChannelMixing() {
        val frames = 3
        val source = FloatArray(frames * 8) { it.toFloat() }
        val planar = FloatArray(frames * 8)
        for (stem in 0 until 4) {
            for (frame in 0 until frames) {
                for (channel in 0 until 2) {
                    val src = SpatialStemBufferLayout.interleavedStemIndex(frame, stem, channel)
                    val dst = SpatialStemBufferLayout.planarObjectIndex(stem, frame, channel, frames)
                    planar[dst] = source[src]
                    assertEquals(source[src], planar[dst], 0f)
                }
            }
        }

        // Stem 2, right channel, frames 0..2 must land in its own planar run.
        assertEquals(5f, planar[2 * 2 * frames + frames], 0f)
        assertEquals(13f, planar[2 * 2 * frames + frames + 1], 0f)
        assertEquals(21f, planar[2 * 2 * frames + frames + 2], 0f)
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
