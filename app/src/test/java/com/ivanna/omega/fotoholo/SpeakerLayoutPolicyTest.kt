package com.ivanna.omega.fotoholo

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class SpeakerLayoutPolicyTest {

    @Test fun `estereo sin subwoofer no toca el ancho`() {
        assertEquals(1.0f, SpeakerLayoutPolicy.widthFactor(SpeakerLayout.STEREO_NO_SUB), 0f)
        assertEquals(0.82f, SpeakerLayoutPolicy.apply(0.82f, SpeakerLayout.STEREO_NO_SUB), 0f)
    }

    @Test fun `subwoofer activo en estereo abre un 3 por ciento`() {
        val f = SpeakerLayoutPolicy.widthFactor(SpeakerLayout(SpeakerMode.STEREO, 2, true))
        assertEquals(1.03f, f, 1e-6f)
    }

    @Test fun `surround con mas satelites abre mas hasta saturar en 4`() {
        val f4 = SpeakerLayoutPolicy.widthFactor(SpeakerLayout(SpeakerMode.SURROUND, 4, false))
        val f6 = SpeakerLayoutPolicy.widthFactor(SpeakerLayout(SpeakerMode.SURROUND, 6, false))
        val f20 = SpeakerLayoutPolicy.widthFactor(SpeakerLayout(SpeakerMode.SURROUND, 20, false))
        assertEquals(1.04f, f4, 1e-6f)
        assertEquals(f6, f20, 1e-6f)   // satura: 6 declarados y 20 (fuera de rango) dan lo mismo
        assertTrue(f6 > f4)
    }

    @Test fun `surround 5 1 con subwoofer combina ambos bonos`() {
        // 5.1 tipico: 5 satelites + 1 subwoofer
        val f = SpeakerLayoutPolicy.widthFactor(SpeakerLayout(SpeakerMode.SURROUND, 5, true))
        assertEquals(1.0f + 0.02f * 3 + 0.03f, f, 1e-6f)
    }

    @Test fun `nunca sale del rango 0,9 a 1,15`() {
        val f = SpeakerLayoutPolicy.widthFactor(SpeakerLayout(SpeakerMode.SURROUND, 999, true))
        assertTrue(f in 0.9f..1.15f)
    }

    @Test fun `apply respeta el techo real del motor y no pasa de 1,5`() {
        val layout = SpeakerLayout(SpeakerMode.SURROUND, 8, true)
        assertEquals(1.5f, SpeakerLayoutPolicy.apply(1.45f, layout), 0f)
    }

    @Test fun `entrada no finita no se toca`() {
        assertTrue(SpeakerLayoutPolicy.apply(Float.NaN, SpeakerLayout(SpeakerMode.SURROUND, 6, true)).isNaN())
    }

    @Test fun `satelites negativos o cero se tratan como el minimo valido`() {
        val fNeg = SpeakerLayoutPolicy.widthFactor(SpeakerLayout(SpeakerMode.SURROUND, -3, false))
        val fZero = SpeakerLayoutPolicy.widthFactor(SpeakerLayout(SpeakerMode.SURROUND, 0, false))
        assertEquals(fNeg, fZero, 0f)
        assertTrue(fZero <= 1.0f)
    }

    @Test fun `ancho base cero se mantiene en cero`() {
        assertEquals(0f, SpeakerLayoutPolicy.apply(0f, SpeakerLayout(SpeakerMode.SURROUND, 6, true)), 0f)
    }
}
