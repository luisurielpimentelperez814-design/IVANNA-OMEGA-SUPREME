package com.ivanna.omega.audio

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.PI
import kotlin.math.sin

class AudioBandEnergyClassifierTest {
    private fun tone(hz: Double, amplitude: Float = 0.5f): FloatArray =
        FloatArray(15_600) { i ->
            (amplitude * sin(2.0 * PI * hz * i / 16_000.0)).toFloat()
        }

    @Test fun lowToneProducesBassDominantEnergy() {
        val scores = AudioBandEnergyClassifier.analyze(tone(100.0))
        assertTrue("bass=${scores.bass}", scores.bass > 0.65f)
        assertTrue(scores.rms > 0.1f)
    }

    @Test fun midToneProducesMidBandEnergy() {
        val scores = AudioBandEnergyClassifier.analyze(tone(1_000.0))
        assertTrue("speech/mid=${scores.speech}", scores.speech > 0.55f)
    }

    @Test fun highToneProducesHighBandEnergy() {
        val scores = AudioBandEnergyClassifier.analyze(tone(6_000.0))
        assertTrue("music/high contribution=${scores.music}", scores.music > 0.55f)
    }

    @Test fun silenceAndNonFiniteSamplesNeverProduceNonFiniteScores() {
        val silence = AudioBandEnergyClassifier.analyze(FloatArray(128))
        assertEquals(0f, silence.rms, 0f)
        val dirty = AudioBandEnergyClassifier.analyze(floatArrayOf(Float.NaN, Float.POSITIVE_INFINITY, 0.25f))
        assertTrue(dirty.speech.isFinite())
        assertTrue(dirty.music.isFinite())
        assertTrue(dirty.bass.isFinite())
        assertTrue(dirty.rms.isFinite())
    }
}
