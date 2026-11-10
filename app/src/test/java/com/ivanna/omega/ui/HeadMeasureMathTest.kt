package com.ivanna.omega.ui

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/**
 * HeadMeasureMathTest — matemática pura de la medición de cabeza (v2.5.389).
 * Sin Android, sin cámara: solo la regla de la tarjeta (8.56 cm) y el radio.
 */
class HeadMeasureMathTest {

    @Test
    fun `card rule gives head width`() {
        // Cabeza 2× la tarjeta en píxeles → 17.12 cm.
        assertEquals(17.12f, HeadMeasureMath.headWidthCmFromSpans(400f, 200f)!!, 0.01f)
        // KEMAR: ancho 17.5 cm → radio 8.75 cm.
        assertEquals(0.0875f, HeadMeasureMath.headRadiusMFromWidthCm(17.5f), 1e-6f)
        assertEquals(0.0875f, HeadMeasureMath.headRadiusMFromSpans(350f, 171.2f)!!, 0.001f)
    }

    @Test
    fun `radius clamps to human range`() {
        assertEquals(0.05f, HeadMeasureMath.headRadiusMFromWidthCm(8f), 1e-6f)
        assertEquals(0.15f, HeadMeasureMath.headRadiusMFromWidthCm(40f), 1e-6f)
        assertEquals(0.0875f, HeadMeasureMath.headRadiusMFromWidthCm(Float.NaN), 1e-6f)
    }

    @Test
    fun `invalid measurement returns null, never invents`() {
        assertNull(HeadMeasureMath.headWidthCmFromSpans(0f, 200f))
        assertNull(HeadMeasureMath.headWidthCmFromSpans(400f, 0f))
        assertNull(HeadMeasureMath.headWidthCmFromSpans(Float.NaN, 200f))
        // Ancho imposible (5 cm o 30 cm): foto o marcadores mal puestos.
        assertNull(HeadMeasureMath.headWidthCmFromSpans(100f, 200f))
        assertNull(HeadMeasureMath.headWidthCmFromSpans(900f, 200f))
        assertNull(HeadMeasureMath.headRadiusMFromSpans(100f, 200f))
    }
}
