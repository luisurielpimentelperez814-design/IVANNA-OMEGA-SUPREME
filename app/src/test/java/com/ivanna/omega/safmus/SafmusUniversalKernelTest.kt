package com.ivanna.omega.safmus

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class SafmusUniversalKernelTest {
    @Test fun spatialCaptureGateLabelsEstimatedGeometryAndNeverClaimsAcousticCalibration() {
        val good = SafmusUniversalKernel.assessSpatialCapture(
            pointCount = 500, depthPointCount = 400, sparsePointCount = 100,
            poseCount = 8, durationMs = 12_000L, trackingState = "TRACKING",
            coordinatesFinite = true, posesFinite = true
        )
        assertTrue(good.exportAllowed)
        assertTrue(good.quality == "REVIEWABLE_DEPTH_ESTIMATE")
        assertTrue(good.evidenceKind == "ESTIMATED_AR_GEOMETRY")
        assertFalse(good.acousticCalibrationReady)
    }

    @Test fun spatialCaptureGateFailsClosedOnInvalidCoordinatesAndLabelsWeakCaptureDegraded() {
        val invalid = SafmusUniversalKernel.assessSpatialCapture(
            500, 400, 100, 8, 12_000L, "TRACKING", false, true
        )
        assertFalse(invalid.exportAllowed)
        val weak = SafmusUniversalKernel.assessSpatialCapture(
            12, 0, 12, 1, 500L, "PAUSED", true, true
        )
        assertTrue(weak.exportAllowed)
        assertTrue(weak.quality == "DEGRADED")
        assertFalse(weak.acousticCalibrationReady)
    }

    @Test fun sliderControlValuesRejectNonFiniteAndClampToDeclaredRange() {
        assertTrue(SafmusUniversalKernel.sanitizeControlValue(0.5f, 0f, 1f) == 0.5f)
        assertTrue(SafmusUniversalKernel.sanitizeControlValue(-2f, 0f, 1f) == 0f)
        assertTrue(SafmusUniversalKernel.sanitizeControlValue(2f, 0f, 1f) == 1f)
        assertTrue(SafmusUniversalKernel.sanitizeControlValue(Float.NaN, 0f, 1f) == null)
        assertTrue(SafmusUniversalKernel.sanitizeControlValue(0.5f, Float.NEGATIVE_INFINITY, 1f) == null)
        assertTrue(SafmusUniversalKernel.sanitizeControlValue(0.5f, 2f, 1f) == null)
    }

    @Test fun globalRouteExcludesParallelRouteA() {
        val d = SafmusUniversalKernel.decideRoute(
            SafmusUniversalKernel.RouteEvidence(true, true, true, true, false)
        )
        assertTrue(d.route == SafmusUniversalKernel.Route.ROUTE_B)
        assertFalse(d.allowSpectralAdaptation)
    }

    @Test fun staleSignalAbstainsInsteadOfAuthorizingEvolution() {
        val d = SafmusUniversalKernel.decideRoute(
            SafmusUniversalKernel.RouteEvidence(false, true, false, true, false)
        )
        assertTrue(d.route == SafmusUniversalKernel.Route.NONE)
        assertFalse(d.allowSpectralAdaptation)
    }

    @Test fun hrtfDatasetPreflightIsUsedBeforeActivationGate() {
        assertTrue(SafmusUniversalKernel.gateHrtfDataset(true, true, true).allowed)
        assertFalse(SafmusUniversalKernel.gateHrtfDataset(false, true, true).allowed)
        assertFalse(SafmusUniversalKernel.gateHrtfDataset(true, false, true).allowed)
        assertFalse(SafmusUniversalKernel.gateHrtfDataset(true, true, false).allowed)
    }

    @Test fun hrtfRequiresCompatibleBinauralRouteAndFiniteCoordinates() {
        assertTrue(SafmusUniversalKernel.gateHrtf(true, true, true, true).allowed)
        assertFalse(SafmusUniversalKernel.gateHrtf(false, true, true, true).allowed)
        assertFalse(SafmusUniversalKernel.gateHrtf(true, false, true, true).allowed)
        assertFalse(SafmusUniversalKernel.gateHrtf(true, true, true, false).allowed)
    }

    @Test fun rirConfigurationPreflightChecksDatasetAndSampleRate() {
        assertTrue(SafmusUniversalKernel.gateRirConfiguration(true, true, true).allowed)
        assertFalse(SafmusUniversalKernel.gateRirConfiguration(false, true, true).allowed)
        assertFalse(SafmusUniversalKernel.gateRirConfiguration(true, false, true).allowed)
        assertFalse(SafmusUniversalKernel.gateRirConfiguration(true, true, false).allowed)
    }

    @Test fun rirRequiresExplicitEvidenceMetadataAndBudget() {
        assertFalse(SafmusUniversalKernel.gateRir(false, true, true, true).allowed)
        assertFalse(SafmusUniversalKernel.gateRir(true, false, true, true).allowed)
        assertFalse(SafmusUniversalKernel.gateRir(true, true, false, true).allowed)
        assertFalse(SafmusUniversalKernel.gateRir(true, true, true, false).allowed)
        assertTrue(SafmusUniversalKernel.gateRir(true, true, true, true).allowed)
    }

    @Test fun roomCorrectionCannotInferRoomWithoutMeasurement() {
        assertFalse(SafmusUniversalKernel.gateRoomCorrection(false, true, true, true).allowed)
        assertFalse(SafmusUniversalKernel.gateRoomCorrection(true, false, true, true).allowed)
        assertFalse(SafmusUniversalKernel.gateRoomCorrection(true, true, false, true).allowed)
        assertTrue(SafmusUniversalKernel.gateRoomCorrection(true, true, true, true).allowed)
    }

    @Test fun evolutionUiMayInspectButDoesNotAuthorizeInvalidTelemetry() {
        assertTrue(SafmusUniversalKernel.mayEvaluateEvolutionProposal(
            SafmusUniversalKernel.Route.ROUTE_A, true, 0.8f
        ))
        assertFalse(SafmusUniversalKernel.mayEvaluateEvolutionProposal(
            SafmusUniversalKernel.Route.ROUTE_B, true, 0.8f
        ))
        assertFalse(SafmusUniversalKernel.mayEvaluateEvolutionProposal(
            SafmusUniversalKernel.Route.ROUTE_A, false, 0.8f
        ))
        assertFalse(SafmusUniversalKernel.mayEvaluateEvolutionProposal(
            SafmusUniversalKernel.Route.ROUTE_A, true, Float.NaN
        ))
    }

    @Test fun evolutionRequiresFiniteFitnessHeadroomStabilityAndLatency() {
        val route = SafmusUniversalKernel.Route.ROUTE_A
        assertTrue(SafmusUniversalKernel.mayAcceptEvolutionProposal(route, true, 0.8f, true, true, true))
        // Missing stability/headroom evidence must fail closed.
        assertFalse(SafmusUniversalKernel.mayAcceptEvolutionProposal(route, true, 0.8f, true))
        assertFalse(SafmusUniversalKernel.mayAcceptEvolutionProposal(route, true, Float.NaN, true, true, true))
        assertFalse(SafmusUniversalKernel.mayAcceptEvolutionProposal(route, true, 0.8f, false, true, true))
        assertFalse(SafmusUniversalKernel.mayAcceptEvolutionProposal(route, true, 0.8f, true, false, true))
        assertFalse(SafmusUniversalKernel.mayAcceptEvolutionProposal(route, true, 0.8f, true, true, false))
        assertFalse(SafmusUniversalKernel.mayAcceptEvolutionProposal(SafmusUniversalKernel.Route.ROUTE_B, true, 0.8f, true, true, true))
    }
}
