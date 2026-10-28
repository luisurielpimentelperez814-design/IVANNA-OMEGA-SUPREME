package com.ivanna.omega.audio

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Regression for the Bluetooth-reroute-vs-clock-drift ambiguity: a route
 * change on the SAME AudioTrack must be recognized so the caller resets
 * AudioTrackBufferPll instead of feeding the discontinuity into its
 * rolling-regression drift estimate.
 */
class AudioTrackRerouteGuardTest {
    @Test fun firstObservationIsNeverAReroute() {
        // No prior confirmed route to compare against -- nothing to detect yet.
        assertFalse(AudioTrackRerouteGuard.isReroute(null, 7))
    }

    @Test fun unresolvedRouteIsNeverAReroute() {
        // Android has not resolved routedDevice on this poll; the existing
        // getTimestamp()-unavailable path handles that, not this guard.
        assertFalse(AudioTrackRerouteGuard.isReroute(5, null))
    }

    @Test fun bothUnknownIsNeverAReroute() {
        assertFalse(AudioTrackRerouteGuard.isReroute(null, null))
    }

    @Test fun sameDeviceIdIsNeverAReroute() {
        assertFalse(AudioTrackRerouteGuard.isReroute(3, 3))
    }

    @Test fun differentConfirmedIdsIsAReroute() {
        // e.g. speaker (id=2) -> Bluetooth A2DP sink (id=9) mid-playback.
        assertTrue(AudioTrackRerouteGuard.isReroute(2, 9))
    }

    @Test fun rerouteDetectionIsSymmetricInDirection() {
        // A2DP disconnect falling back to speaker must also be caught, not
        // just the connect direction.
        assertTrue(AudioTrackRerouteGuard.isReroute(9, 2))
    }
}
