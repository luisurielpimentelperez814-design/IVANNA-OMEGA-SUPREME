package com.ivanna.omega.audio

import org.junit.Assert.assertEquals
import org.junit.Test

class DspLoadTierPolicyTest {

    @Test fun `empieza en 0 y no sube con shed bajo`() {
        val p = DspLoadTierPolicy()
        repeat(10) { assertEquals(0, p.update(0.02f)) }
    }

    @Test fun `sube a 1 con shed mayor que 0_10 sostenido 2 ticks`() {
        val p = DspLoadTierPolicy()
        assertEquals(0, p.update(0.35f))
        assertEquals(1, p.update(0.35f))
    }

    @Test fun `sube a 2 con shed mayor que 0_25 sostenido`() {
        val p = DspLoadTierPolicy()
        assertEquals(0, p.update(0.35f))
        assertEquals(1, p.update(0.35f))
        assertEquals(1, p.update(0.35f))
        assertEquals(2, p.update(0.35f))
    }

    @Test fun `un solo tick alto no sube el tier`() {
        val p = DspLoadTierPolicy()
        assertEquals(0, p.update(0.35f))
        assertEquals(0, p.update(0.0f))
        assertEquals(0, p.update(0.35f))
        assertEquals(0, p.update(0.0f))
    }

    @Test fun `baja a 0 solo con shed bajo sostenido 5 ticks`() {
        val p = DspLoadTierPolicy()
        repeat(2) { p.update(0.35f) }
        assertEquals(1, p.tier)
        repeat(4) { assertEquals(1, p.update(0.0f)) }
        assertEquals(0, p.update(0.0f))
    }

    @Test fun `baja de 2 a 1 con shed moderado sostenido 4 ticks`() {
        val p = DspLoadTierPolicy()
        repeat(4) { p.update(0.35f) }
        assertEquals(2, p.tier)
        repeat(3) { assertEquals(2, p.update(0.15f)) }
        assertEquals(1, p.update(0.15f))
    }

    @Test fun `NaN se trata como shed 0`() {
        val p = DspLoadTierPolicy()
        assertEquals(0, p.update(Float.NaN))
    }
}
