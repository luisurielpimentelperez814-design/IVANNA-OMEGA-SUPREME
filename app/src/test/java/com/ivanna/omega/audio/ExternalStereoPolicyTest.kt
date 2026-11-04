package com.ivanna.omega.audio

import org.junit.Assert.*
import org.junit.Test

class ExternalStereoPolicyTest {
    @Test fun externalLineOutRetainsHrtfAtConservativeWetWithoutPretendingItIsHeadphones() {
        for (route in listOf(OutputRoute.USB, OutputRoute.WIRED_AUX)) {
            assertFalse(ExternalStereoPolicy.allowHeadphoneHrtf(route, true, true))
            assertTrue(ExternalStereoPolicy.allowSpatialHrtf(route, true, true))
            assertEquals(
                ExternalStereoPolicy.EXTERNAL_LINE_OUT_HRTF_WET,
                ExternalStereoPolicy.hrtfWetForRoute(route, true, true),
                0f
            )
            assertTrue(ExternalStereoPolicy.usesExternalSpeakers(route, true))
        }
    }

    @Test fun headphoneRoutesKeepFullHrtfAndUserCanStillDisableIt() {
        assertEquals(1f, ExternalStereoPolicy.hrtfWetForRoute(OutputRoute.WIRED_AUX, false, true), 0f)
        assertEquals(1f, ExternalStereoPolicy.hrtfWetForRoute(OutputRoute.USB, false, true), 0f)
        assertEquals(1f, ExternalStereoPolicy.hrtfWetForRoute(OutputRoute.BLUETOOTH, true, true), 0f)
        assertEquals(0f, ExternalStereoPolicy.hrtfWetForRoute(OutputRoute.WIRED_AUX, true, false), 0f)
    }

    @Test fun transauralXtcIsAllowedOnlyOnDeclaredExternalStereoRoutes() {
        assertTrue(ExternalStereoPolicy.allowTransauralXtc(OutputRoute.USB, true, true))
        assertTrue(ExternalStereoPolicy.allowTransauralXtc(OutputRoute.WIRED_AUX, true, true))
        assertFalse(ExternalStereoPolicy.allowTransauralXtc(OutputRoute.USB, false, true))
        assertFalse(ExternalStereoPolicy.allowTransauralXtc(OutputRoute.WIRED_AUX, true, false))
        assertFalse(ExternalStereoPolicy.allowTransauralXtc(OutputRoute.BLUETOOTH, true, true))
        assertFalse(ExternalStereoPolicy.allowTransauralXtc(OutputRoute.SPEAKER, true, true))
        assertFalse(ExternalStereoPolicy.allowTransauralXtc(OutputRoute.UNKNOWN, true, true))
    }

    @Test fun phoneSpeakerAndUnknownRemainFailClosed() {
        assertEquals(0f, ExternalStereoPolicy.hrtfWetForRoute(OutputRoute.UNKNOWN, false, true), 0f)
        assertEquals(0f, ExternalStereoPolicy.hrtfWetForRoute(OutputRoute.SPEAKER, false, true), 0f)
        assertFalse(ExternalStereoPolicy.allowHeadphoneHrtf(OutputRoute.UNKNOWN, false, true))
        assertFalse(ExternalStereoPolicy.allowHeadphoneHrtf(OutputRoute.SPEAKER, false, true))
    }
}
