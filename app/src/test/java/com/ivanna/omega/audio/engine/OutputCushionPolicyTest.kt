package com.ivanna.omega.audio.engine

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class OutputCushionPolicyTest {
    private val block = 320

    @Test fun `el colchon es el headroom objetivo cuando cabe en la pista`() {
        // 20 ms @ 48 kHz = 960 frames; pista de 4096 frames deja de sobra.
        assertEquals(960, OutputCushionPolicy.cushionFrames(960, 4096, block))
    }

    @Test fun `nunca llena la pista y deja 2 bloques libres`() {
        // pista de 1280 frames (26,7 ms): maximo 1280 - 640 = 640.
        assertEquals(640, OutputCushionPolicy.cushionFrames(960, 1280, block))
        assertTrue(OutputCushionPolicy.cushionFrames(5000, 1280, block) <= 1280 - 2 * block)
    }

    @Test fun `entradas invalidas dan cero y nunca negativo`() {
        assertEquals(0, OutputCushionPolicy.cushionFrames(960, 0, block))
        assertEquals(0, OutputCushionPolicy.cushionFrames(960, 1280, 0))
        assertEquals(0, OutputCushionPolicy.cushionFrames(-5, 4096, block))
        assertEquals(0, OutputCushionPolicy.cushionFrames(960, 500, block)) // pista menor que 2 bloques
    }

    @Test fun `sin underrun nuevo no re-llena aunque la cola este baja`() {
        assertEquals(0, OutputCushionPolicy.reprimeFrames(0, 0, 960, 10_000))
    }

    @Test fun `con underrun y cola vacia repone el colchon completo`() {
        assertEquals(960, OutputCushionPolicy.reprimeFrames(3, 0, 960, 10_000))
    }

    @Test fun `solo escribe lo que falta`() {
        assertEquals(960 - 200, OutputCushionPolicy.reprimeFrames(1, 200, 960, 10_000))
    }

    @Test fun `si la cola ya tiene la mitad del colchon no toca nada`() {
        assertEquals(0, OutputCushionPolicy.reprimeFrames(5, 480, 960, 10_000))
        assertEquals(0, OutputCushionPolicy.reprimeFrames(5, 2000, 960, 10_000))
    }

    @Test fun `el cooldown impide inflar la latencia con una racha de underruns`() {
        assertEquals(0, OutputCushionPolicy.reprimeFrames(9, 0, 960, OutputCushionPolicy.REPRIME_COOLDOWN_MS - 1))
        assertTrue(OutputCushionPolicy.reprimeFrames(9, 0, 960, OutputCushionPolicy.REPRIME_COOLDOWN_MS) > 0)
    }

    @Test fun `sin colchon configurado no escribe`() {
        assertEquals(0, OutputCushionPolicy.reprimeFrames(4, 0, 0, 10_000))
    }

    @Test fun `cola negativa se trata como vacia`() {
        assertEquals(960, OutputCushionPolicy.reprimeFrames(1, -50, 960, 10_000))
    }
}
