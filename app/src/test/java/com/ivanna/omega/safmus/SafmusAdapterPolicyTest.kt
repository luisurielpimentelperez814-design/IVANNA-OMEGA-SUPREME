package com.ivanna.omega.safmus

import com.ivanna.omega.audio.RouteProfile
import org.junit.Assert.assertEquals
import org.junit.Test

class SafmusAdapterPolicyTest {
    @Test fun pressureReducesAdapterInventoryCadenceAndOptionalProbes() {
        val normal = SafmusAdapterPolicy.cadence(SafmusAdapterPolicy.Load.NORMAL)
        val conservative = SafmusAdapterPolicy.cadence(SafmusAdapterPolicy.Load.CONSERVATIVE)
        val safe = SafmusAdapterPolicy.cadence(SafmusAdapterPolicy.Load.SAFE)

        assertEquals(5_000L, normal.intervalMs)
        assertEquals(true, normal.probeCameraInventory)
        assertEquals(8_000L, conservative.intervalMs)
        assertEquals(false, conservative.probeDisplayAndSensors)
        assertEquals(12_000L, safe.intervalMs)
        assertEquals(false, safe.probeCameraInventory)
    }

    @Test fun microphoneRingAbsorbsMoreSchedulerJitterUnderPressure() {
        val min = 1024
        val block = 2048
        assertEquals(8192, SafmusAdapterPolicy.captureBufferBytes(min, block, SafmusAdapterPolicy.Load.NORMAL))
        assertEquals(12288, SafmusAdapterPolicy.captureBufferBytes(min, block, SafmusAdapterPolicy.Load.CONSERVATIVE))
        assertEquals(16384, SafmusAdapterPolicy.captureBufferBytes(min, block, SafmusAdapterPolicy.Load.SAFE))
        assertEquals(20_000, SafmusAdapterPolicy.captureBufferBytes(20_000, block, SafmusAdapterPolicy.Load.NORMAL))
    }

    @Test fun resourcePressureBoundsRouteEnhancementWithoutAddingGain() {
        val base = RouteProfile(bassBoostDb = 0f, dialogBoostDb = 4.5f, widenerMult = 0.5f)
        val normal = SafmusAdapterPolicy.optimizeRouteProfile(base, SafmusAdapterPolicy.Load.NORMAL)
        val conservative = SafmusAdapterPolicy.optimizeRouteProfile(base, SafmusAdapterPolicy.Load.CONSERVATIVE)
        val safe = SafmusAdapterPolicy.optimizeRouteProfile(base, SafmusAdapterPolicy.Load.SAFE)

        assertEquals(4.5f, normal.dialogBoostDb, 0f)
        assertEquals(3f, conservative.dialogBoostDb, 0f)
        assertEquals(1.5f, safe.dialogBoostDb, 0f)
        assertEquals(0.425f, conservative.widenerMult, 0f)
        assertEquals(0.325f, safe.widenerMult, 0f)
    }
}
