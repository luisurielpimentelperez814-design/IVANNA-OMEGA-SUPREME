package com.ivanna.omega.audio

/**
 * Decides whether a change in an AudioTrack's routed-device id, observed
 * between two polls of the SAME track, must be treated as an acoustic-path
 * latency discontinuity rather than clock drift.
 *
 * Android can silently reroute a playing AudioTrack to a different output
 * device (a Bluetooth A2DP sink connecting/disconnecting, an output-policy
 * change) without the app recreating the track. That reroute changes the
 * path's buffering/latency in one step; it carries no information about the
 * sink clock's actual frequency. Feeding the frame/timestamp pair spanning
 * that step into [AudioTrackBufferPll]'s rolling regression would be
 * misread as a sudden drift event. This guard is the single place that
 * decides "is this sample pair safe to use for drift estimation", so the
 * clock servo and anything else that polls routedDevice stay consistent.
 */
object AudioTrackRerouteGuard {
    /**
     * @param previousRoutedDeviceId last confirmed `AudioDeviceInfo.id` for this
     *   track, or null if no route has been confirmed yet (first poll after
     *   track creation/start).
     * @param currentRoutedDeviceId this poll's `track.routedDevice?.id`, or
     *   null if Android has not resolved a route yet.
     * @return true only when both ids are known and differ — never on the
     *   first observation (nothing to compare against) and never while
     *   Android has not yet resolved a route (that is handled by the
     *   existing getTimestamp()-unavailable fail-closed path, not here).
     */
    fun isReroute(previousRoutedDeviceId: Int?, currentRoutedDeviceId: Int?): Boolean =
        previousRoutedDeviceId != null &&
            currentRoutedDeviceId != null &&
            currentRoutedDeviceId != previousRoutedDeviceId
}
