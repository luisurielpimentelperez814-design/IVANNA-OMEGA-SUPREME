package com.ivanna.omega.sensing

import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class WifiSceneReconstructionTest {
    @Test
    fun refusesSceneReconstructionFromUnverifiedWifiMetadata() {
        val result = WifiSceneReconstruction.evaluate(
            WifiSceneReconstruction.CaptureEvidence(
                providerId = null,
                csiVerifiedOnDevice = false,
                calibrationId = null,
                anchors = emptyList(),
                synchronizedLinks = 0,
                validCsiFrames = 0,
                hasCalibratedRangeOrAngleEstimates = false,
                modelVersion = null
            )
        )
        assertFalse(result.ready)
        assertTrue(result.confidenceCeiling == 0.0)
        assertTrue(result.blockers.size >= 6)
    }

    @Test
    fun requiresCalibratedNonCoplanarMultiLinkEvidence() {
        val result = WifiSceneReconstruction.evaluate(
            WifiSceneReconstruction.CaptureEvidence(
                providerId = "test-provider",
                csiVerifiedOnDevice = true,
                calibrationId = "room-calibration-01",
                anchors = listOf(
                    WifiSceneReconstruction.Anchor("a", 0.0, 0.0, 0.0),
                    WifiSceneReconstruction.Anchor("b", 1.0, 0.0, 0.0),
                    WifiSceneReconstruction.Anchor("c", 0.0, 1.0, 0.0),
                    WifiSceneReconstruction.Anchor("d", 0.0, 0.0, 1.0)
                ),
                synchronizedLinks = 4,
                validCsiFrames = 256,
                hasCalibratedRangeOrAngleEstimates = true,
                modelVersion = "scene-model-test-1"
            )
        )
        assertTrue(result.ready)
        assertNull(result.confidenceCeiling)
    }

    @Test
    fun rejectsNonFiniteAnchorCoordinatesEvenWhenOtherEvidenceIsPresent() {
        val result = WifiSceneReconstruction.evaluate(
            WifiSceneReconstruction.CaptureEvidence(
                providerId = "test-provider",
                csiVerifiedOnDevice = true,
                calibrationId = "room-calibration-01",
                anchors = listOf(
                    WifiSceneReconstruction.Anchor("a", 0.0, 0.0, 0.0),
                    WifiSceneReconstruction.Anchor("b", 1.0, 0.0, 0.0),
                    WifiSceneReconstruction.Anchor("c", 0.0, 1.0, 0.0),
                    WifiSceneReconstruction.Anchor("d", 0.0, 0.0, Double.NaN)
                ),
                synchronizedLinks = 4,
                validCsiFrames = 256,
                hasCalibratedRangeOrAngleEstimates = true,
                modelVersion = "scene-model-test-1"
            )
        )
        assertFalse(result.ready)
        assertTrue(result.blockers.any { it.contains("coordenadas") })
        assertTrue(result.confidenceCeiling == 0.0)
    }

    @Test
    fun rejectsDuplicateAnchorIdsEvenWithNonCoplanarCoordinates() {
        val result = WifiSceneReconstruction.evaluate(
            WifiSceneReconstruction.CaptureEvidence(
                providerId = "test-provider",
                csiVerifiedOnDevice = true,
                calibrationId = "room-calibration-01",
                anchors = listOf(
                    WifiSceneReconstruction.Anchor("same", 0.0, 0.0, 0.0),
                    WifiSceneReconstruction.Anchor("same", 1.0, 0.0, 0.0),
                    WifiSceneReconstruction.Anchor("c", 0.0, 1.0, 0.0),
                    WifiSceneReconstruction.Anchor("d", 0.0, 0.0, 1.0)
                ),
                synchronizedLinks = 4,
                validCsiFrames = 256,
                hasCalibratedRangeOrAngleEstimates = true,
                modelVersion = "scene-model-test-1"
            )
        )
        assertFalse(result.ready)
        assertTrue(result.blockers.any { it.contains("identificadores") })
    }

    @Test
    fun rejectsBlankAnchorIds() {
        val result = WifiSceneReconstruction.evaluate(
            WifiSceneReconstruction.CaptureEvidence(
                providerId = "test-provider",
                csiVerifiedOnDevice = true,
                calibrationId = "room-calibration-01",
                anchors = listOf(
                    WifiSceneReconstruction.Anchor("", 0.0, 0.0, 0.0),
                    WifiSceneReconstruction.Anchor("b", 1.0, 0.0, 0.0),
                    WifiSceneReconstruction.Anchor("c", 0.0, 1.0, 0.0),
                    WifiSceneReconstruction.Anchor("d", 0.0, 0.0, 1.0)
                ),
                synchronizedLinks = 4,
                validCsiFrames = 256,
                hasCalibratedRangeOrAngleEstimates = true,
                modelVersion = "scene-model-test-1"
            )
        )
        assertFalse(result.ready)
        assertTrue(result.blockers.any { it.contains("identificador no vacío") })
    }
}
