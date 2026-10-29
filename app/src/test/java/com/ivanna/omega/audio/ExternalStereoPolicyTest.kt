package com.ivanna.omega.audio

import org.junit.Assert.*
import org.junit.Test

class ExternalStereoPolicyTest {
    @Test fun usbAndAuxDoNotImplyHeadphones() {
        for (route in listOf(OutputRoute.USB, OutputRoute.WIRED_AUX)) {
            assertFalse(ExternalStereoPolicy.allowHeadphoneHrtf(route, true, true))
            assertTrue(ExternalStereoPolicy.allowHeadphoneHrtf(route, false, true))
            assertTrue(ExternalStereoPolicy.usesExternalSpeakers(route, true))
        }
    }
    @Test fun declarationDoesNotChangeBluetoothOrAuthorizeUnknownRoute() {
        assertTrue(ExternalStereoPolicy.allowHeadphoneHrtf(OutputRoute.BLUETOOTH, true, true))
        assertFalse(ExternalStereoPolicy.usesExternalSpeakers(OutputRoute.BLUETOOTH, true))
        assertFalse(ExternalStereoPolicy.allowHeadphoneHrtf(OutputRoute.UNKNOWN, false, true))
        assertFalse(ExternalStereoPolicy.allowHeadphoneHrtf(OutputRoute.SPEAKER, false, true))
        assertFalse(ExternalStereoPolicy.allowHeadphoneHrtf(OutputRoute.USB, false, false))
    }
}
