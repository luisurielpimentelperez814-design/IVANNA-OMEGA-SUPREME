package com.ivanna.omega.audio

/** A USB descriptor cannot tell headphones from an amplifier's analog input. */
object ExternalStereoPolicy {
    fun usesExternalSpeakers(route: OutputRoute, declaredLineOut: Boolean): Boolean =
        declaredLineOut && (route == OutputRoute.USB || route == OutputRoute.WIRED_AUX)

    fun allowHeadphoneHrtf(route: OutputRoute, declaredLineOut: Boolean, requested: Boolean): Boolean =
        requested && route != OutputRoute.SPEAKER && route != OutputRoute.UNKNOWN &&
            !usesExternalSpeakers(route, declaredLineOut)
}
