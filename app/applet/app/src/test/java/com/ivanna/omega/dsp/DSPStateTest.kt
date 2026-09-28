package com.ivanna.omega.dsp

import org.junit.Test
import org.junit.Assert.*

class DSPStateTest {
    @Test
    fun `slider to db round trip`() {
        val db = DSPState.sliderToDb(0.5f)
        val back = DSPState.dbToSlider(db)
        assertEquals(0.5f, back, 0.01f)
    }

    @Test
    fun `default state values in safe range`() {
        val s = DSPState()
        assertTrue(s.drive in 0f..1f)
        assertTrue(s.alpha in 0f..1f)
        assertTrue(s.beta in 0f..1f)
        assertTrue(s.stereoWidth in 0f..2f)
    }

    @Test
    fun `omega metrics defaults are sane`() {
        val m = com.ivanna.omega.audio.OmegaMetrics()
        assertTrue(m.latencyMs > 0f)
        assertTrue(m.sampleRate == 48000)
        assertFalse(m.dspActive)
    }
}
