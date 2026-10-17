package com.ivanna.omega.fotoholo

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class FotoHoloMathTest {

    private fun bands(f: (Int) -> Float) = FloatArray(64) { f(it) }
    private val lowHeavy = bands { if (it < 16) 0.9f else 0.05f }
    private val highHeavy = bands { if (it >= 40) 0.9f else 0.05f }
    private val flat = bands { 0.5f }

    @Test fun `silencio o bandas cortas no generan rasgos validos`() {
        assertFalse(FotoHoloMath.features(FloatArray(64), null).valid)
        assertFalse(FotoHoloMath.features(FloatArray(10) { 1f }, null).valid)
    }

    @Test fun `NaN e Infinito se tratan como cero y no envenenan los rasgos`() {
        val b = bands { if (it % 2 == 0) Float.NaN else Float.POSITIVE_INFINITY }
        assertFalse(FotoHoloMath.features(b, null).valid)
        val mixed = bands { if (it == 3) Float.NaN else 0.4f }
        val f = FotoHoloMath.features(mixed, null)
        assertTrue(f.valid)
        assertTrue(f.energy.isFinite() && f.centroid.isFinite() && f.lowShare.isFinite())
    }

    @Test fun `graves dominantes reparten energia a graves y agudos dominantes a agudos`() {
        val l = FotoHoloMath.features(lowHeavy, null)
        val h = FotoHoloMath.features(highHeavy, null)
        assertTrue(l.lowShare > 0.6f && l.highShare < 0.1f)
        assertTrue(h.highShare > 0.6f && h.lowShare < 0.1f)
        assertTrue(h.centroid > l.centroid)
    }

    @Test fun `flux detecta transitorio y es cero sin subida`() {
        val quiet = bands { 0.1f }
        val hit = bands { 0.6f }
        assertTrue(FotoHoloMath.features(hit, quiet).flux > 0.3f)
        assertEquals(0f, FotoHoloMath.features(quiet, hit).flux, 1e-6f)
    }

    @Test fun `intensidad cero deja el ancho del usuario intacto`() {
        for (spec in listOf(lowHeavy, highHeavy, flat))
            assertEquals(0.68f, FotoHoloMath.targetWidth(0.68f, 0f, FotoHoloMath.features(spec, null)), 1e-6f)
    }

    @Test fun `agudos abren y graves cierran el ancho dentro de +-MAX_OFFSET`() {
        val up = FotoHoloMath.targetWidth(0.7f, 1f, FotoHoloMath.features(highHeavy, null))
        val down = FotoHoloMath.targetWidth(0.7f, 1f, FotoHoloMath.features(lowHeavy, null))
        assertTrue(up > 0.7f && down < 0.7f)
        assertTrue(up - 0.7f <= FotoHoloMath.MAX_OFFSET + 1e-6f)
        assertTrue(0.7f - down <= FotoHoloMath.MAX_OFFSET + 1e-6f)
    }

    @Test fun `sin senal valida el objetivo es exactamente la base`() {
        assertEquals(0.9f, FotoHoloMath.targetWidth(0.9f, 1f, FotoHoloFeatures.NONE), 0f)
    }

    @Test fun `nunca supera el techo ni baja de cero y respeta una base del usuario mayor al techo`() {
        val hi = FotoHoloMath.features(highHeavy, null)
        assertTrue(FotoHoloMath.targetWidth(1.19f, 1f, hi) <= FotoHoloMath.WIDTH_CEIL + 1e-6f)
        assertEquals(1.4f, FotoHoloMath.targetWidth(1.4f, 1f, hi), 1e-6f)   // no recorta lo que eligió el usuario
        assertTrue(FotoHoloMath.targetWidth(0.0f, 1f, FotoHoloMath.features(lowHeavy, null)) >= 0f)
    }

    @Test fun `entradas no finitas devuelven valores seguros`() {
        assertEquals(1.0f, FotoHoloMath.targetWidth(Float.NaN, 1f, FotoHoloMath.features(flat, null)), 0f)
        val w = FotoHoloMath.targetWidth(0.6f, Float.NaN, FotoHoloMath.features(highHeavy, null))
        assertEquals(0.6f, w, 1e-6f)
    }

    @Test fun `slew limita el paso y converge sin sobrepasar`() {
        var c = 0.5f
        val first = FotoHoloMath.slew(c, 0.9f)
        assertEquals(0.5f + FotoHoloMath.MAX_STEP, first, 1e-6f)
        repeat(100) { c = FotoHoloMath.slew(c, 0.9f) }
        assertEquals(0.9f, c, 1e-6f)
        assertEquals(0.5f, FotoHoloMath.slew(0.5f, Float.NaN), 0f)
    }

    @Test fun `holoPoint es finito y acotado para todas las bandas y rotaciones`() {
        for (i in 0 until 64) for (v in listOf(0f, 0.5f, 1f, Float.NaN, 9f)) for (r in listOf(0f, 3.1f, 100f, Float.NaN)) {
            val p = FotoHoloMath.holoPoint(i, v, r)
            assertTrue(p.all { it.isFinite() })
            assertTrue(p[0] in -1.2f..1.2f && p[1] in -1.2f..1.2f)
            assertTrue(p[2] in 0.5f..1.0f)
        }
    }
}
