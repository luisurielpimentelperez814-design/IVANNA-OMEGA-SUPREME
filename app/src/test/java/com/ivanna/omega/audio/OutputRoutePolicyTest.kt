package com.ivanna.omega.audio

import org.junit.Assert.assertEquals
import org.junit.Test

class OutputRoutePolicyTest {
    @Test fun usbDacWinsWhenBluetoothRemainsConnected() {
        assertEquals(
            OutputRoute.USB,
            OutputRoutePolicy.select(
                usbConnected = true,
                bluetoothConnected = true,
                wiredConnected = false,
                speakerConnected = true
            )
        )
    }

    @Test fun bluetoothIsPreferredToWiredFallbackWhenNoUsbExists() {
        assertEquals(
            OutputRoute.BLUETOOTH,
            OutputRoutePolicy.select(
                usbConnected = false,
                bluetoothConnected = true,
                wiredConnected = true,
                speakerConnected = true
            )
        )
    }

    @Test fun wiredAndSpeakerFallbacksRemainDeterministic() {
        assertEquals(
            OutputRoute.WIRED_AUX,
            OutputRoutePolicy.select(false, false, true, true)
        )
        assertEquals(
            OutputRoute.SPEAKER,
            OutputRoutePolicy.select(false, false, false, true)
        )
        assertEquals(
            OutputRoute.UNKNOWN,
            OutputRoutePolicy.select(false, false, false, false)
        )
    }
    @Test fun wiredAuxKeepsNeutralStereoForExternalSurroundUpmix() {
        val profile = AudioRouteManager.profileFor(OutputRoute.WIRED_AUX)
        assertEquals(0f, profile.bassBoostDb, 0f)
        assertEquals(0f, profile.dialogBoostDb, 0f)
        assertEquals(1f, profile.widenerMult, 0f)
    }

}
