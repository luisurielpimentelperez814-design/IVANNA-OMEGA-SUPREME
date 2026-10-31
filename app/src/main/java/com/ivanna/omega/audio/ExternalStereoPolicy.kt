package com.ivanna.omega.audio

/**
 * A USB descriptor cannot tell headphones from an amplifier's analog input.
 * External line-out keeps spatial engines available, but limits binaural HRTF
 * blend because loudspeaker-to-ear crosstalk has not been measured/cancelled.
 */
object ExternalStereoPolicy {
    const val EXTERNAL_LINE_OUT_HRTF_WET = 0.15f

    fun usesExternalSpeakers(route: OutputRoute, declaredLineOut: Boolean): Boolean =
        declaredLineOut && (route == OutputRoute.USB || route == OutputRoute.WIRED_AUX)

    /** HRTF intended for headphones only; do not send a personal headphone SAF profile to speakers. */
    fun allowHeadphoneHrtf(route: OutputRoute, declaredLineOut: Boolean, requested: Boolean): Boolean =
        requested && route != OutputRoute.SPEAKER && route != OutputRoute.UNKNOWN &&
            !usesExternalSpeakers(route, declaredLineOut)

    /**
     * Route-aware HRTF blend. External stereo line-out retains a low, reversible
     * HRTF contribution (0.15) rather than hard-disabling the engine. This is
     * spectral/spatial coloration only, not transaural crosstalk cancellation.
     */
    fun hrtfWetForRoute(route: OutputRoute, declaredLineOut: Boolean, requested: Boolean): Float {
        if (!requested || route == OutputRoute.SPEAKER || route == OutputRoute.UNKNOWN) return 0f
        return if (usesExternalSpeakers(route, declaredLineOut)) {
            EXTERNAL_LINE_OUT_HRTF_WET
        } else {
            1f
        }
    }

    fun allowSpatialHrtf(route: OutputRoute, declaredLineOut: Boolean, requested: Boolean): Boolean =
        hrtfWetForRoute(route, declaredLineOut, requested) > 0f
}
