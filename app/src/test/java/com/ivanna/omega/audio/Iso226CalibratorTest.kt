package com.ivanna.omega.audio

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

/**
 * ISO 226 loudness — signo de la compensación (v2.5.441).
 *
 * La compensación de loudness debe REFORZAR graves y agudos cuando se escucha
 * bajito: a 62 phon el oído percibe el grave ~10 dB más débil (relativo a
 * 1 kHz) que a 82 phon, así que la curva debe ser un BOOST en los extremos.
 * La fórmula anterior (lp(ref) - lp(listen)) daba el signo contrario
 * (-9.4 dB @31.5 Hz, -3.2 dB @12.5 kHz): cortaba graves y agudos y el sonido
 * salía "bofo" (apagado, sin cuerpo ni aire). Verificado numéricamente contra
 * ISO 226:2003 §A.1.
 */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [33], manifest = Config.NONE)
class Iso226CalibratorTest {

    @Test
    fun compensation62to82BoostsLowsAndHighs() {
        val gains = Iso226Calibrator.computeCompensation(62f, 82f)
        assertEquals(10, gains.size)
        // Graves: boost claro (31.5 Hz es el extremo más deficiente a nivel bajo).
        assertTrue("31.5 Hz debe ser boost, fue ${gains[0]}", gains[0] > 5f)
        assertTrue("63 Hz debe ser boost, fue ${gains[1]}", gains[1] > 3f)
        assertTrue("125 Hz debe ser boost, fue ${gains[2]}", gains[2] > 1f)
        // 4 kHz es la referencia de máxima sensibilidad: ~0 dB.
        assertEquals(0f, gains[7], 1.0f)
        // Agudos: boost (12.5 kHz también se percibe débil a nivel bajo).
        assertTrue("12.5 kHz debe ser boost, fue ${gains[9]}", gains[9] > 1f)
        // Ninguna banda debe cortar más de 1 dB en la compensación 62→82.
        assertTrue("ninguna banda debe cortar, min=${gains.min()}", gains.min() > -1f)
    }

    @Test
    fun compensationAtSameLevelIsFlat() {
        val gains = Iso226Calibrator.computeCompensation(70f, 70f)
        for (g in gains) assertEquals(0f, g, 0.001f)
    }

    @Test
    fun compensationIsAntisymmetric() {
        // Escuchar MÁS alto que la referencia invierte la curva.
        val up = Iso226Calibrator.computeCompensation(62f, 82f)
        val down = Iso226Calibrator.computeCompensation(82f, 62f)
        for (i in up.indices) assertEquals(-up[i], down[i], 0.001f)
    }
}
