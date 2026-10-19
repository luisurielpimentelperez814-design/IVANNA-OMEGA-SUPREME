package com.ivanna.omega.perceptual

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class PerceptualSceneRuntimeTest {
    private fun evidence() = PerceptualRuntimeEvidence(
        capturedAtEpochMs = 1_800_000_000_000L,
        outputRoute = OutputRoute.AUX,
        poseConfirmed = true,
        requestedAzimuthRad = 0.2f,
        effectiveAzimuthRad = 0.2f,
        requestedElevationRad = 0.1f,
        effectiveElevationRad = 0.1f,
        requestedDistanceScale = 1f,
        effectiveDistanceScale = 1f,
        poseUpdatedAtEpochMs = 1_800_000_000_000L,
        movementActive = true,
        movementConfirmed = true,
        movementUpdatedAtEpochMs = 1_800_000_000_000L,
        headRotationFresh = true,
        hrtfConfirmed = true,
        hrtfSubject = "kemar",
        reverbRequestedLevel = 0.4f,
        reverbEffectiveLevel = 0.4f,
        reverbConfirmed = true,
        measuredRirActive = true,
        measuredRt60Seconds = 0.45f,
        heuristicUpmixerEnabled = true,
        hapticsEnabled = true,
        hapticDelayMs = 15,
        hapticLatencyMeasured = true,
        hapticRouteDescription = "AUX"
    )

    @Test
    fun snapshotContainsExactlyTenAxesAndDoesNotPretendPartialAxesAreApplied() {
        val snapshot = PerceptualSceneRuntime.assemble(evidence())
        assertEquals(PerceptualAxis.entries.toSet(), snapshot.axes.map { it.axis }.toSet())
        assertEquals(10, snapshot.axes.size)
        assertTrue(snapshot.validate().isEmpty())

        assertEquals(AxisState.APPLIED, snapshot.axes.single { it.axis == PerceptualAxis.AZIMUTH }.state)
        assertEquals(AxisState.APPLIED, snapshot.axes.single { it.axis == PerceptualAxis.MOVEMENT }.state)
        assertEquals(AxisState.APPLIED, snapshot.axes.single { it.axis == PerceptualAxis.REVERBERANT_FIELD }.state)
        assertEquals(AxisState.DEGRADED, snapshot.axes.single { it.axis == PerceptualAxis.ELEVATION }.state)
        assertEquals(AxisState.DEGRADED, snapshot.axes.single { it.axis == PerceptualAxis.DISTANCE }.state)
        assertEquals(AxisState.DEGRADED, snapshot.axes.single { it.axis == PerceptualAxis.HEAD_ROTATION }.state)
        assertEquals(AxisState.DEGRADED, snapshot.axes.single { it.axis == PerceptualAxis.HRTF_PERSONALIZATION }.state)
        assertEquals(AxisState.UNAVAILABLE, snapshot.axes.single { it.axis == PerceptualAxis.ROOM_GEOMETRY }.state)
        assertEquals(AxisState.DEGRADED, snapshot.axes.single { it.axis == PerceptualAxis.SOURCE_SEPARATION }.state)
        assertEquals(AxisState.DEGRADED, snapshot.axes.single { it.axis == PerceptualAxis.AV_HAPTIC_SYNC }.state)
    }

    @Test
    fun missingReadbacksNeverBecomeApplied() {
        val snapshot = PerceptualSceneRuntime.assemble(
            evidence().copy(
                poseConfirmed = false,
                effectiveAzimuthRad = null,
                effectiveElevationRad = null,
                effectiveDistanceScale = null,
                movementActive = false,
                movementConfirmed = false,
                headRotationFresh = false,
                hrtfConfirmed = false,
                hrtfSubject = "none",
                reverbConfirmed = false,
                reverbEffectiveLevel = null,
                measuredRirActive = false,
                measuredRt60Seconds = null,
                heuristicUpmixerEnabled = false,
                hapticsEnabled = false
            )
        )

        assertFalse(snapshot.axes.any { it.engineConfirmed && it.state != AxisState.APPLIED })
        assertEquals(AxisState.UNAVAILABLE, snapshot.axes.single { it.axis == PerceptualAxis.AZIMUTH }.state)
        assertEquals(AxisState.UNAVAILABLE, snapshot.axes.single { it.axis == PerceptualAxis.HRTF_PERSONALIZATION }.state)
        assertEquals(AxisState.UNAVAILABLE, snapshot.axes.single { it.axis == PerceptualAxis.AV_HAPTIC_SYNC }.state)
        assertTrue(snapshot.validate().isEmpty())
    }

    @Test
    fun unknownOutputRouteRemainsAnExplicitValidationFailure() {
        val snapshot = PerceptualSceneRuntime.assemble(evidence().copy(outputRoute = OutputRoute.UNKNOWN))
        assertTrue(snapshot.validate().any { it.contains("outputRoute is unknown") })
    }
}
