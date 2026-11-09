package com.ivanna.omega.safmus

import com.ivanna.omega.audio.RouteProfile
import org.junit.Assert.assertEquals
import org.junit.Test

class SafmusAdapterPolicyTest {
    @Test fun captureBufferArithmeticNeverWrapsToASmallerAllocation() {
        for (load in SafmusAdapterPolicy.Load.entries) {
            assertEquals(Int.MAX_VALUE, SafmusAdapterPolicy.captureBufferBytes(1024, Int.MAX_VALUE, load))
            assertEquals(1024, SafmusAdapterPolicy.captureBufferBytes(1024, -1, load))
        }
    }

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
        // Nivel 1 (4 bloques) para aislar el escalado por carga: el nivel
        // fija un minimo de bloques y en Nivel 6 (default, 8 bloques) el
        // piso del nivel domina a NORMAL/CONSERVATIVE (ver
        // levelSixFloorNeverShrinksBelowEightBlocks).
        val level1 = SafmusUniversalKernel.PowerLevel.LEVEL_1
        assertEquals(8192, SafmusAdapterPolicy.captureBufferBytes(min, block, SafmusAdapterPolicy.Load.NORMAL, level1))
        assertEquals(12288, SafmusAdapterPolicy.captureBufferBytes(min, block, SafmusAdapterPolicy.Load.CONSERVATIVE, level1))
        assertEquals(16384, SafmusAdapterPolicy.captureBufferBytes(min, block, SafmusAdapterPolicy.Load.SAFE, level1))
        assertEquals(20_000, SafmusAdapterPolicy.captureBufferBytes(20_000, block, SafmusAdapterPolicy.Load.NORMAL, level1))
    }

    @Test fun levelSixFloorNeverShrinksBelowEightBlocks() {
        val min = 1024
        val block = 2048
        val level6 = SafmusUniversalKernel.PowerLevel.LEVEL_6
        // En Nivel 6 el piso es 8 bloques: la presion de recursos solo
        // puede aumentarlo, nunca reducirlo por debajo del nivel.
        assertEquals(16384, SafmusAdapterPolicy.captureBufferBytes(min, block, SafmusAdapterPolicy.Load.NORMAL, level6))
        assertEquals(16384, SafmusAdapterPolicy.captureBufferBytes(min, block, SafmusAdapterPolicy.Load.CONSERVATIVE, level6))
        assertEquals(16384, SafmusAdapterPolicy.captureBufferBytes(min, block, SafmusAdapterPolicy.Load.SAFE, level6))
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
