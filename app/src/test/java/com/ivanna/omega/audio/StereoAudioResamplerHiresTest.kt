package com.ivanna.omega.audio

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.log10
import kotlin.math.max
import kotlin.math.roundToInt
import kotlin.math.sin
import kotlin.math.sqrt

/**
 * Tests de la ruta Hi-Res 192 kHz / Ruta A (v2.5.376, HIRES_192K §4.3).
 *
 * El upsampler 48k -> 192k es [StereoAudioResampler] (sinc Blackman-Harris
 * 64 taps, 1024 fases). El DSP sigue a 48 kHz; solo la reinyección sube
 * de tasa. Estos tests fijan el comportamiento medido del resampleador
 * (simulado en Python antes de escribirlos: ganancia 0.7071 exacta,
 * imágenes a -134 dB, continuidad de fase exacta, 1280 frames por bloque
 * en régimen).
 *
 * HONESTIDAD: el upsampler NO crea información nueva (la fuente llega a
 * 48 kHz). Estos tests verifican que el remuestreo es transparente
 * (ganancia unidad, imágenes suprimidas, sin discontinuidades), no que
 * aparezca contenido nuevo.
 */
class StereoAudioResamplerHiresTest {

    companion object {
        private const val IN_RATE = 48_000
        private const val OUT_RATE = 192_000
        private const val BLOCK_FRAMES = 320
        private const val TONE_HZ = 1_000.0
    }

    /** Tono seno estéreo (misma señal en L y R), intercalado. */
    private fun stereoTone(freqHz: Double, sampleRate: Int, frames: Int): FloatArray {
        val out = FloatArray(frames * 2)
        for (i in 0 until frames) {
            val v = sin(2.0 * PI * freqHz * i / sampleRate).toFloat()
            out[2 * i] = v
            out[2 * i + 1] = v
        }
        return out
    }

    /** Canal izquierdo (índices pares) de un buffer estéreo intercalado. */
    private fun leftChannel(interleaved: FloatArray): FloatArray {
        val out = FloatArray(interleaved.size / 2)
        for (i in out.indices) out[i] = interleaved[2 * i]
        return out
    }

    private fun rms(samples: FloatArray): Double {
        var acc = 0.0
        for (v in samples) acc += v.toDouble() * v.toDouble()
        return sqrt(acc / samples.size)
    }

    /** Magnitud (amplitud pico) de la componente a [freqHz] por Goertzel. */
    private fun goertzelMagnitude(samples: FloatArray, freqHz: Double, sampleRate: Int): Double {
        val n = samples.size
        val k = (freqHz * n / sampleRate).roundToInt()
        val w = 2.0 * PI * k / n
        val coeff = 2.0 * cos(w)
        var s1 = 0.0
        var s2 = 0.0
        for (v in samples) {
            val s0 = v.toDouble() + coeff * s1 - s2
            s2 = s1
            s1 = s0
        }
        val power = s1 * s1 + s2 * s2 - coeff * s1 * s2
        return 2.0 * sqrt(max(power, 0.0)) / n
    }

    /**
     * Remuestrea un tono 48k -> 192k en bloques de 320 frames (como la
     * Ruta A) y devuelve el canal izquierdo completo.
     */
    private fun upsampleTone(frames: Int): FloatArray {
        val tone = stereoTone(TONE_HZ, IN_RATE, frames)
        val resampler = StereoAudioResampler(OUT_RATE)
        resampler.setInputSampleRate(IN_RATE)
        val chunks = mutableListOf<FloatArray>()
        var totalFrames = 0
        var offset = 0
        while (offset < frames) {
            val blockFrames = minOf(BLOCK_FRAMES, frames - offset)
            val block = tone.copyOfRange(offset * 2, (offset + blockFrames) * 2)
            val out = resampler.process(block)
            chunks.add(out)
            totalFrames += out.size / 2
            offset += blockFrames
        }
        val all = FloatArray(totalFrames * 2)
        var pos = 0
        for (c in chunks) {
            c.copyInto(all, pos)
            pos += c.size
        }
        return leftChannel(all)
    }

    @Test
    fun upsample48kTo192k_tone1kHz_passbandGainIsUnity() {
        // 0.5 s de tono 1 kHz (amplitud 1.0). Región asentada: saltar el
        // transitorio de arranque del FIR (~4096 muestras) y medir 0.25 s.
        val left = upsampleTone(IN_RATE / 2)
        val settled = left.copyOfRange(4096, 4096 + OUT_RATE / 4)
        val measured = rms(settled)
        // Ideal 0.7071 (amplitud 1.0). Patrón del test de fidelidad existente.
        assertTrue("RMS 1 kHz = $measured, esperado ~0.7071", measured in 0.65..0.76)
    }

    @Test
    fun upsample48kTo192k_imageAttenuationExceeds90dB() {
        // Las imágenes del upsampling 4x caen en 47 kHz (48k-1k) y 49 kHz
        // (48k+1k). Con 48000 muestras a 192 kHz los bins de Goertzel caen
        // exactos en 1 kHz, 47 kHz y 49 kHz (espaciado 4 Hz).
        val left = upsampleTone(IN_RATE / 2)
        val settled = left.copyOfRange(4096, 4096 + OUT_RATE / 4)
        val mag1k = goertzelMagnitude(settled, 1_000.0, OUT_RATE)
        val mag47k = goertzelMagnitude(settled, 47_000.0, OUT_RATE)
        val mag49k = goertzelMagnitude(settled, 49_000.0, OUT_RATE)
        val db47 = 20.0 * log10(mag47k / mag1k)
        val db49 = 20.0 * log10(mag49k / mag1k)
        // Medido en simulación: ~-134 dB. El umbral es 90 dB (margen amplio).
        assertTrue("Imagen 47 kHz = $db47 dB, esperado < -90 dB", db47 < -90.0)
        assertTrue("Imagen 49 kHz = $db49 dB, esperado < -90 dB", db49 < -90.0)
    }

    @Test
    fun upsample48kTo192k_phaseContinuousAcrossBlocks() {
        // Mismo tono procesado en una sola llamada vs en bloques de 320
        // frames: la salida debe ser idéntica (el historial y la posición
        // fraccionaria se conservan entre llamadas).
        val frames = IN_RATE / 2
        val tone = stereoTone(TONE_HZ, IN_RATE, frames)

        val whole = StereoAudioResampler(OUT_RATE).also { it.setInputSampleRate(IN_RATE) }
            .process(tone)
        val wholeLeft = leftChannel(whole)

        val chunked = StereoAudioResampler(OUT_RATE).also { it.setInputSampleRate(IN_RATE) }
        val chunks = mutableListOf<FloatArray>()
        var offset = 0
        while (offset < frames) {
            val blockFrames = minOf(BLOCK_FRAMES, frames - offset)
            chunks.add(chunked.process(tone.copyOfRange(offset * 2, (offset + blockFrames) * 2)))
            offset += blockFrames
        }
        val totalSamples = chunks.sumOf { it.size }
        val chunkedAll = FloatArray(totalSamples)
        var pos = 0
        for (c in chunks) {
            c.copyInto(chunkedAll, pos)
            pos += c.size
        }
        val chunkedLeft = leftChannel(chunkedAll)

        assertEquals("El número de frames debe coincidir", wholeLeft.size, chunkedLeft.size)
        var maxDiff = 0.0
        for (i in wholeLeft.indices) {
            maxDiff = max(maxDiff, abs(wholeLeft[i].toDouble() - chunkedLeft[i].toDouble()))
        }
        assertTrue("Discontinuidad de fase entre bloques: max|diff| = $maxDiff", maxDiff < 1e-5)
    }

    @Test
    fun upsample48kTo192k_frameCountIsFourTimesPerBlock() {
        // 6 bloques de 320 frames. El primer bloque da menos (transitorio
        // de arranque del FIR, ~1151 frames); en régimen cada bloque de
        // 320 frames produce exactamente 1280 frames (4x).
        val resampler = StereoAudioResampler(OUT_RATE)
        resampler.setInputSampleRate(IN_RATE)
        val tone = stereoTone(TONE_HZ, IN_RATE, BLOCK_FRAMES * 6)
        val counts = IntArray(6)
        for (b in 0 until 6) {
            val block = tone.copyOfRange(b * BLOCK_FRAMES * 2, (b + 1) * BLOCK_FRAMES * 2)
            counts[b] = resampler.process(block).size / 2
        }
        assertTrue("Primer bloque (arranque) = ${counts[0]}, esperado < 1280 y > 1000",
            counts[0] in 1001 until 1280)
        for (b in 1 until 6) {
            assertTrue("Bloque $b = ${counts[b]} frames, esperado 1280 ± 1",
                counts[b] in 1279..1281)
        }
    }

    // ------------------------------------------------------------------
    // Selección de tasa de reinyección (HiResAudioManager, pura).
    // ------------------------------------------------------------------

    @Test
    fun selectReinjectRate_dacAnnounces192k_returns192k() {
        assertEquals(192_000,
            HiResAudioManager.selectReinjectRate(intArrayOf(44_100, 48_000, 96_000, 192_000), true))
    }

    @Test
    fun selectReinjectRate_dacMaxIs176k_returns176k() {
        assertEquals(176_400,
            HiResAudioManager.selectReinjectRate(intArrayOf(48_000, 96_000, 176_400), true))
    }

    @Test
    fun selectReinjectRate_dacDoesNotAnnounceAbove48k_fallsBackTo48k() {
        assertEquals(48_000,
            HiResAudioManager.selectReinjectRate(intArrayOf(44_100, 48_000, 96_000), true))
    }

    @Test
    fun selectReinjectRate_hiresOff_returns48k() {
        assertEquals(48_000,
            HiResAudioManager.selectReinjectRate(intArrayOf(44_100, 48_000, 96_000, 192_000), false))
    }

    @Test
    fun selectReinjectRate_dacAnnounces384k_filtersAbove192k() {
        // 384 kHz > máximo de reinyección (192 kHz): se descarta.
        assertEquals(48_000,
            HiResAudioManager.selectReinjectRate(intArrayOf(384_000), true))
    }

    @Test
    fun selectReinjectRate_noRates_fallsBackTo48k() {
        assertEquals(48_000,
            HiResAudioManager.selectReinjectRate(intArrayOf(), true))
    }
}
