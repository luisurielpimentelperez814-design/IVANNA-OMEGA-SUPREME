package com.ivanna.omega.safmus

import com.ivanna.omega.audio.RouteProfile

/**
 * SAFMUS policy applied to the adapters themselves, not only to the DSP.
 * Pure deterministic policy: no Android calls, no invented device capabilities.
 */
object SafmusAdapterPolicy {
    enum class Load { NORMAL, CONSERVATIVE, SAFE }

    data class Cadence(
        val intervalMs: Long,
        val probeDisplayAndSensors: Boolean,
        val probeCameraInventory: Boolean
    )

    fun cadence(load: Load): Cadence = when (load) {
        Load.NORMAL -> Cadence(5_000L, true, true)
        // Under pressure, avoid repeated low-value inventory scans while preserving
        // the thermal/memory/battery control loop at a useful cadence.
        Load.CONSERVATIVE -> Cadence(8_000L, false, false)
        Load.SAFE -> Cadence(12_000L, false, false)
    }

    /**
     * Bound route compensation under resource pressure. The base route/codec profile
     * remains authoritative; SAFMUS only reduces extra DSP work and never adds gain.
     */
    fun optimizeRouteProfile(base: RouteProfile, load: Load): RouteProfile = when (load) {
        Load.NORMAL -> base
        Load.CONSERVATIVE -> RouteProfile(
            bassBoostDb = base.bassBoostDb.coerceIn(-6f, 6f),
            dialogBoostDb = base.dialogBoostDb.coerceIn(0f, 3f),
            widenerMult = base.widenerMult.coerceIn(0.65f, 1f)
        )
        Load.SAFE -> RouteProfile(
            bassBoostDb = base.bassBoostDb.coerceIn(-3f, 3f),
            dialogBoostDb = base.dialogBoostDb.coerceIn(0f, 1.5f),
            widenerMult = base.widenerMult.coerceIn(0.85f, 1f)
        )
    }
}
