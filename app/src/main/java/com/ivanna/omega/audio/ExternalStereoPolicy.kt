package com.ivanna.omega.audio

/**
 * A USB descriptor cannot tell headphones from an amplifier's analog input.
 * External line-out keeps a bounded HRTF blend. The separate XTC allow-list
 * is opt-in through the user's line-out declaration and never applies to headphones.
 */
object ExternalStereoPolicy {
    const val EXTERNAL_LINE_OUT_HRTF_WET = 0.15f

    fun usesExternalSpeakers(route: OutputRoute, declaredLineOut: Boolean): Boolean =
        declaredLineOut && (route == OutputRoute.USB || route == OutputRoute.WIRED_AUX)

    /** XTC is requested by the user and valid only on a declared external L/R speaker route. */
    fun allowTransauralXtc(route: OutputRoute, declaredLineOut: Boolean, requested: Boolean): Boolean =
        requested && usesExternalSpeakers(route, declaredLineOut)

    /** HRTF intended for headphones only; do not send a personal headphone SAF profile to speakers. */
    fun allowHeadphoneHrtf(route: OutputRoute, declaredLineOut: Boolean, requested: Boolean): Boolean =
        requested && route != OutputRoute.SPEAKER && route != OutputRoute.UNKNOWN &&
            !usesExternalSpeakers(route, declaredLineOut)

    /**
     * Route-aware HRTF blend. External stereo line-out retains a low, reversible
     * HRTF contribution (0.15) rather than hard-disabling the engine. This is
     * spectral/spatial coloration only, not transaural crosstalk cancellation.
     * v2.5.437 — LA BOCINA TAMBIÉN LLEVA MEZCLA (0.15): la bocina del teléfono
     * es el dispositivo principal de escucha de IVANNA y con wet=0 el motor
     * espacial quedaba apagado en captura (spatial_audio=0) cada vez que la
     * ruta era SPEAKER, así que los efectos prendían y apagaban solos. La
     * mezcla acotada 0.15 (igual que line-out externo) mantiene lo espacial
     * vivo en bocina sin mandar el HRTF completo de auriculares. UNKNOWN
     * sigue en 0 (ruta realmente desconocida → seguro).
     */
    fun hrtfWetForRoute(route: OutputRoute, declaredLineOut: Boolean, requested: Boolean): Float {
        if (!requested || route == OutputRoute.UNKNOWN) return 0f
        return if (usesExternalSpeakers(route, declaredLineOut) || route == OutputRoute.SPEAKER) {
            EXTERNAL_LINE_OUT_HRTF_WET
        } else {
            1f
        }
    }

    fun allowSpatialHrtf(route: OutputRoute, declaredLineOut: Boolean, requested: Boolean): Boolean =
        hrtfWetForRoute(route, declaredLineOut, requested) > 0f
}
