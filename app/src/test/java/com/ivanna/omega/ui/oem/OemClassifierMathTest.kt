package com.ivanna.omega.ui.oem

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class OemClassifierMathTest {

    // Vector nativo real: [rms, peak, percussiveness, tonality, reverb, dynRange, centroidHz, spread]
    private fun chars(perc: Float, tonal: Float, centroid: Float) =
        floatArrayOf(0.22f, 0.67f, perc, tonal, 0.1f, 0.9f, centroid, 900f)

    @Test fun `lee el centroide del indice 6 y no la percusividad como antes`() {
        val r = OemClassifierMath.read(chars(0.3f, 0.6f, 2400f), rms = 0.22f)
        assertEquals(2400f, r.centroidHz, 0f)          // antes salia ~0 Hz (leia indice 2)
        assertEquals(0.3f, r.percussiveness, 0f)
        assertEquals(0.6f, r.tonality, 0f)
        assertTrue(r.hasCharacteristics)
    }

    @Test fun `rms y pico del vector ya no se confunden con percusividad ni tonalidad`() {
        val r = OemClassifierMath.read(floatArrayOf(0.9f, 0.95f, 0.1f, 0.2f, 0f, 0f, 1000f, 0f), rms = 0.5f)
        assertEquals(0.1f, r.percussiveness, 0f)
        assertEquals(0.2f, r.tonality, 0f)
    }

    @Test fun `musica tonal y sostenida domina como MUSICA`() {
        val r = OemClassifierMath.read(chars(0.05f, 0.9f, 3000f), rms = 0.2f)
        assertEquals(1, r.dominantClass)
        assertTrue(r.probMusic > r.probVoice && r.probMusic > r.probBass)
    }

    @Test fun `percusivo con centroide bajo domina como BAJOS`() {
        val r = OemClassifierMath.read(chars(0.95f, 0.5f, 100f), rms = 0.2f)
        assertEquals(2, r.dominantClass)
    }

    @Test fun `ni tonal ni percusivo es VOZ`() {
        val r = OemClassifierMath.read(chars(0.05f, 0.05f, 2500f), rms = 0.2f)
        assertEquals(0, r.dominantClass)
    }

    @Test fun `sin senal es SILENCIO`() {
        val r = OemClassifierMath.read(chars(0.5f, 0.5f, 1000f), rms = 0f)
        assertEquals(3, r.dominantClass)
        assertEquals(1f, r.probSilence, 0f)
    }

    @Test fun `con senal pero sin vector nativo no inventa clase`() {
        val r = OemClassifierMath.read(FloatArray(0), rms = 0.3f)
        assertFalse(r.hasCharacteristics)
        assertEquals(-1, r.dominantClass)
        assertEquals(0f, r.probSilence, 0f)
        assertEquals(0f, r.probVoice + r.probMusic + r.probBass, 0f)
    }

    @Test fun `NaN e Infinito no producen NaN`() {
        val r = OemClassifierMath.read(floatArrayOf(0f, 0f, Float.NaN, Float.POSITIVE_INFINITY, 0f, 0f, Float.NaN, 0f), rms = 0.2f)
        assertTrue(r.centroidHz.isFinite() && r.probVoice.isFinite() && r.probMusic.isFinite() && r.probBass.isFinite())
    }

    @Test fun `vector corto se trata como no disponible`() {
        assertFalse(OemClassifierMath.read(floatArrayOf(1f, 2f, 3f), rms = 0.3f).hasCharacteristics)
    }
}
