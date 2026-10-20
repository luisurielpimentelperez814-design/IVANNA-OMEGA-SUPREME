package com.ivanna.omega.safmus

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class SafmusUniversalKernelTest {

    @Test fun hardwareOptimizationRequiresPermissionFreshnessBudgetAndRollback() {
        val resource = SafmusUniversalKernel.HardwareResource.CAMERA
        fun evidence(
            capability: Boolean = true,
            permission: Boolean = true,
            owner: Boolean = true,
            fresh: Boolean = true,
            supported: Boolean = true,
            budget: Boolean = true,
            rollback: Boolean = true
        ) = SafmusUniversalKernel.HardwareOptimizationEvidence(
            capabilityDetected = capability,
            permissionGranted = permission,
            owningSessionConfirmed = owner,
            telemetryFresh = fresh,
            configurationSupported = supported,
            resourceBudgetValid = budget,
            rollbackAvailable = rollback
        )

        assertTrue(SafmusUniversalKernel.gateHardwareOptimization(resource, evidence()).allowed)
        assertFalse(SafmusUniversalKernel.gateHardwareOptimization(resource, evidence(capability = false)).allowed)
        assertFalse(SafmusUniversalKernel.gateHardwareOptimization(resource, evidence(permission = false)).allowed)
        assertFalse(SafmusUniversalKernel.gateHardwareOptimization(resource, evidence(owner = false)).allowed)
        assertFalse(SafmusUniversalKernel.gateHardwareOptimization(resource, evidence(fresh = false)).allowed)
        assertFalse(SafmusUniversalKernel.gateHardwareOptimization(resource, evidence(supported = false)).allowed)
        assertFalse(SafmusUniversalKernel.gateHardwareOptimization(resource, evidence(budget = false)).allowed)
        assertFalse(SafmusUniversalKernel.gateHardwareOptimization(resource, evidence(rollback = false)).allowed)
    }

    @Test fun hardwareOptimizationReadbackMustMatchBeforeSuccessIsReported() {
        val resource = SafmusUniversalKernel.HardwareResource.AUDIO_OUTPUT
        val ready = SafmusUniversalKernel.gateHardwareOptimization(
            resource,
            SafmusUniversalKernel.HardwareOptimizationEvidence(
                capabilityDetected = true,
                permissionGranted = true,
                owningSessionConfirmed = true,
                telemetryFresh = true,
                configurationSupported = true,
                resourceBudgetValid = true,
                rollbackAvailable = true
            )
        )
        assertTrue(SafmusUniversalKernel.confirmHardwareOptimization(ready, true).allowed)
        assertFalse(SafmusUniversalKernel.confirmHardwareOptimization(ready, false).allowed)
        val denied = SafmusUniversalKernel.gateHardwareOptimization(
            SafmusUniversalKernel.HardwareResource.MICROPHONE,
            SafmusUniversalKernel.HardwareOptimizationEvidence(
                capabilityDetected = true,
                permissionGranted = false,
                owningSessionConfirmed = true,
                telemetryFresh = true,
                configurationSupported = true,
                resourceBudgetValid = true,
                rollbackAvailable = true
            )
        )
        assertFalse(SafmusUniversalKernel.confirmHardwareOptimization(denied, true).allowed)
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
