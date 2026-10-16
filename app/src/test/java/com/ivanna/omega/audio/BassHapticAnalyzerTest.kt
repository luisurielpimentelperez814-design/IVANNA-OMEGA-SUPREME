package com.ivanna.omega.audio

import kotlin.math.PI
import kotlin.math.exp
import kotlin.math.sin
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class BassHapticAnalyzerTest {
    private val sr = 48_000
    private val block = 320

    private fun run(signal: FloatArray, strength: Float = 1f): List<Pair<Float, Int>> {
        val a = BassHapticAnalyzer(sr)
        val out = ArrayList<Pair<Float, Int>>()
        val buf = FloatArray(block * 2)
        var s = 0
        while (s + block <= signal.size) {
            for (i in 0 until block) { buf[2 * i] = signal[s + i]; buf[2 * i + 1] = signal[s + i] }
            val amp = a.process(buf, block, strength)
            if (amp > 0) out.add(s.toFloat() / sr to amp)
            s += block
        }
        return out
    }

    private fun kicks(seconds: Int, every: Double): FloatArray {
        val x = FloatArray(sr * seconds)
        var k = 0.2
        while (k < seconds) {
            val i0 = (k * sr).toInt()
            for (i in i0 until minOf(x.size, i0 + sr / 4)) {
                val t = (i - i0).toDouble() / sr
                x[i] += (0.7 * sin(2 * PI * 60 * t) * exp(-t * 18)).toFloat()
            }
            k += every
        }
        return x
    }

    @Test fun oneTriggerPerKick() {
        val hits = run(kicks(4, 0.5))
        assertEquals("8 golpes → 8 pulsos", 8, hits.size)
    }

    @Test fun sustainedSubTriggersOnlyOnce() {
        val x = FloatArray(sr * 4) { (0.5 * sin(2 * PI * 60 * it / sr)).toFloat() }
        assertEquals(1, run(x).size)
    }

    @Test fun midrangeAndQuietSignalsDoNotTrigger() {
        val voice = FloatArray(sr * 4) { (0.5 * sin(2 * PI * 1000 * it / sr)).toFloat() }
        val quiet = FloatArray(sr * 4) { (0.01 * sin(2 * PI * 60 * it / sr)).toFloat() }
        assertEquals(0, run(voice).size)
        assertEquals(0, run(quiet).size)
    }

    @Test fun strengthScalesAmplitudeAndZeroDisables() {
        val k = kicks(2, 0.5)
        val full = run(k, 1f).first().second
        val half = run(k, 0.5f).first().second
        assertTrue("más fuerza → más amplitud", full > half)
        assertTrue(half >= BassHapticAnalyzer.MIN_AMPLITUDE && full <= 255)
        assertEquals(0, run(k, 0f).size)
    }
}
