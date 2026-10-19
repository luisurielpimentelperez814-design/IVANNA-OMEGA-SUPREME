package com.ivanna.omega.sensing

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Test

class WifiAcousticAdaptationPolicyTest {
    private fun scene(
        level: UniversalWifiSensingAdapter.DataLevel =
            UniversalWifiSensingAdapter.DataLevel.WIFI_METADATA_ONLY,
        movement: Double? = 0.7,
        confidence: Double = 0.5,
        occupancy: UniversalWifiSensingAdapter.OccupancyHint =
            UniversalWifiSensingAdapter.OccupancyHint.SIGNAL_CHANGED
    ) = UniversalWifiSensingAdapter.SceneFeatures(
        level = level,
        observationCount = 8,
        medianRssiDbm = -54,
        signalVariabilityDb = 4.0,
        movementIndicator = movement,
        occupancy = occupancy,
        confidence = confidence,
        source = "test",
        timestampMs = 1000L,
        limitations = listOf("test")
    )

    @Test
    fun radioChangeRequestsMeasurementButNeverChangesDspDirectly() {
        val result = WifiAcousticAdaptationPolicy.evaluate(
            WifiAcousticAdaptationPolicy.Input(
                scene = scene(),
                acousticCalibrationAvailable = true,
                acousticCalibrationAgeMs = 1000L
            )
        )
        assertEquals(
            WifiAcousticAdaptationPolicy.Action.REQUEST_ACOUSTIC_REMEASUREMENT,
            result.action
        )
        assertFalse(result.dspMayChange)
    }

    @Test
    fun weakOrUnknownRadioEvidenceDoesNotTriggerRecalibration() {
        val weak = WifiAcousticAdaptationPolicy.evaluate(
            WifiAcousticAdaptationPolicy.Input(
                scene = scene(movement = 0.1),
                acousticCalibrationAvailable = true,
                acousticCalibrationAgeMs = 1000L
            )
        )
        val unknown = WifiAcousticAdaptationPolicy.evaluate(
            WifiAcousticAdaptationPolicy.Input(
                scene = scene(movement = null, occupancy = UniversalWifiSensingAdapter.OccupancyHint.UNKNOWN),
                acousticCalibrationAvailable = true,
                acousticCalibrationAgeMs = 1000L
            )
        )
        assertEquals(WifiAcousticAdaptationPolicy.Action.NO_ACTION, weak.action)
        assertEquals(WifiAcousticAdaptationPolicy.Action.WAIT_FOR_MORE_RADIO_DATA, unknown.action)
    }

    @Test
    fun noWifiKeepsCurrentAudioState() {
        val result = WifiAcousticAdaptationPolicy.evaluate(
            WifiAcousticAdaptationPolicy.Input(
                scene = scene(
                    level = UniversalWifiSensingAdapter.DataLevel.NO_WIFI,
                    movement = null,
                    occupancy = UniversalWifiSensingAdapter.OccupancyHint.UNKNOWN
                ),
                acousticCalibrationAvailable = false,
                acousticCalibrationAgeMs = null
            )
        )
        assertEquals(WifiAcousticAdaptationPolicy.Action.NO_ACTION, result.action)
    }

    @Test
    fun invalidConfidenceOrMovementFailsClosed() {
        val badConfidence = WifiAcousticAdaptationPolicy.evaluate(
            WifiAcousticAdaptationPolicy.Input(
                scene = scene(confidence = Double.NaN),
                acousticCalibrationAvailable = false,
                acousticCalibrationAgeMs = null
            )
        )
        val badMovement = WifiAcousticAdaptationPolicy.evaluate(
            WifiAcousticAdaptationPolicy.Input(
                scene = scene(movement = Double.NaN),
                acousticCalibrationAvailable = false,
                acousticCalibrationAgeMs = null
            )
        )
        assertEquals(WifiAcousticAdaptationPolicy.Action.WAIT_FOR_MORE_RADIO_DATA, badConfidence.action)
        assertEquals(WifiAcousticAdaptationPolicy.Action.WAIT_FOR_MORE_RADIO_DATA, badMovement.action)
        assertFalse(badConfidence.dspMayChange)
        assertFalse(badMovement.dspMayChange)
    }
}
