package com.ivanna.omega.audio

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class ThermalTierTest {

    @Test fun `sin respuesta del HAL no se inventa un nivel`() {
        assertNull(ThermalTier.of(Float.NaN))
    }

    @Test fun `los umbrales de headroom eligen el nivel esperado`() {
        assertEquals(ThermalTier.FULL, ThermalTier.of(0.0f))
        assertEquals(ThermalTier.FULL, ThermalTier.of(0.29f))
        assertEquals(ThermalTier.REDUCED, ThermalTier.of(0.3f))
        assertEquals(ThermalTier.LIMITED, ThermalTier.of(0.6f))
        assertEquals(ThermalTier.PROTECTED, ThermalTier.of(0.8f))
        assertEquals(ThermalTier.PROTECTED, ThermalTier.of(1.5f))
    }

    @Test fun `los recortes crecen de forma monotona con el nivel y nunca tocan la ganancia`() {
        val tiers = ThermalTier.values()
        for (i in 1 until tiers.size) {
            assertTrue(tiers[i].exciterReduction >= tiers[i - 1].exciterReduction)
            assertTrue(tiers[i].spatialCut >= tiers[i - 1].spatialCut)
            assertTrue(tiers[i].compressorCut >= tiers[i - 1].compressorCut)
        }
        for (t in tiers) assertTrue(t.spatialCut in 0f..1f && t.compressorCut in 0f..1f && t.exciterReduction in 0f..1f)
    }

    @Test fun `la descripcion dice solo lo que el gobernador hace`() {
        assertEquals("sin recortes", ThermalTier.FULL.describeCuts())
        assertTrue(ThermalTier.LIMITED.describeCuts().contains("ancho −25 %"))
        assertTrue(!ThermalTier.PROTECTED.describeCuts().contains("bypass", ignoreCase = true))
    }
}
