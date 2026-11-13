package com.ivanna.omega.audio

import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * DspLoadTierPolicy — tier de HOLOS por CARGA REAL del DSP (v2.5.429).
 * El disparador es la carga (emaUs / presupuesto, 1.0 = al 100%), NO el
 * shed: medido en vivo (23:43, v2.5.428), con HOLOS p99 13-25 ms el shed
 * se quedo en 1.3-1.4% y el tier en 0. Umbrales: carga > 0.85 -> tier 1
 * (HOLOS 12 bandas); carga > 1.00 (sobre presupuesto) -> tier 2.
 */
class DspLoadTierPolicyTest {

    @Test fun `empieza en 0 y no sube con carga baja`() {
        val p = DspLoadTierPolicy()
        repeat(10) { assertEquals(0, p.update(0.50f)) }
    }

    @Test fun `sube a 1 con carga mayor que 0_85 sostenida 2 ticks`() {
        val p = DspLoadTierPolicy()
        assertEquals(0, p.update(0.90f))
        assertEquals(1, p.update(0.90f))
    }

    @Test fun `sube a 2 con carga mayor que 1_00 sostenida`() {
        val p = DspLoadTierPolicy()
        assertEquals(0, p.update(1.20f))
        assertEquals(1, p.update(1.20f))
        assertEquals(1, p.update(1.20f))
        assertEquals(2, p.update(1.20f))
    }

    @Test fun `un solo tick alto no sube el tier`() {
        val p = DspLoadTierPolicy()
        assertEquals(0, p.update(1.20f))
        assertEquals(0, p.update(0.0f))
        assertEquals(0, p.update(1.20f))
        assertEquals(0, p.update(0.0f))
    }

    @Test fun `baja a 0 solo con carga baja sostenida 5 ticks`() {
        val p = DspLoadTierPolicy()
        repeat(2) { p.update(0.90f) }
        assertEquals(1, p.tier)
        repeat(4) { assertEquals(1, p.update(0.0f)) }
        assertEquals(0, p.update(0.0f))
    }

    @Test fun `baja de 2 a 1 con carga moderada sostenida 4 ticks`() {
        val p = DspLoadTierPolicy()
        repeat(4) { p.update(1.20f) }
        assertEquals(2, p.tier)
        repeat(3) { assertEquals(2, p.update(0.90f)) }
        assertEquals(1, p.update(0.90f))
    }

    @Test fun `sube a 3 bypass con carga mayor que 1_50 sostenida`() {
        val p = DspLoadTierPolicy()
        assertEquals(0, p.update(1.80f))
        assertEquals(1, p.update(1.80f))
        assertEquals(1, p.update(1.80f))
        assertEquals(2, p.update(1.80f))
        assertEquals(2, p.update(1.80f))
        assertEquals(3, p.update(1.80f))
    }

    @Test fun `baja de 3 a 2 con carga moderada sostenida 4 ticks`() {
        val p = DspLoadTierPolicy()
        repeat(6) { p.update(1.80f) }
        assertEquals(3, p.tier)
        repeat(3) { assertEquals(3, p.update(0.90f)) }
        assertEquals(2, p.update(0.90f))
    }

    @Test fun `NaN se trata como carga 0`() {
        val p = DspLoadTierPolicy()
        assertEquals(0, p.update(Float.NaN))
    }
}
