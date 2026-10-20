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
     * Choose an AudioRecord ring size for the owning capture session.
     * Bigger rings absorb scheduler jitter with few extra bytes; the audio loop
     * still consumes one block at a time, so this is not an added fixed delay.
     */
    fun captureBufferBytes(minBytes: Int, bytesPerBlock: Int, load: Load): Int {
        val blocks = when (load) {
            Load.NORMAL -> 4
            Load.CONSERVATIVE -> 6
            Load.SAFE -> 8
        }
        return maxOf(minBytes, bytesPerBlock.coerceAtLeast(1) * blocks)
    }

    /**
     * Bound route compensation under resource pressure. The base route/codec profile
     * remains authoritative; SAFMUS only reduces extra DSP work and never adds gain.
     */
    fun optimizeRouteProfile(base: RouteProfile, load: Load): RouteProfile = when (load) {
        Load.NORMAL -> base
        Load.CONSERVATIVE -> RouteProfile(
            bassBoostDb = if (base.bassBoostDb > 0f) base.bassBoostDb * 0.5f else base.bassBoostDb,
            dialogBoostDb = (base.dialogBoostDb.coerceAtLeast(0f) * (2f / 3f)).coerceAtMost(3f),
            widenerMult = base.widenerMult.coerceIn(0f, 1f) * 0.85f
        )
        Load.SAFE -> RouteProfile(
            bassBoostDb = base.bassBoostDb.coerceAtMost(0f),
            dialogBoostDb = (base.dialogBoostDb.coerceAtLeast(0f) / 3f).coerceAtMost(1.5f),
            widenerMult = base.widenerMult.coerceIn(0f, 1f) * 0.65f
        )
    }
}
