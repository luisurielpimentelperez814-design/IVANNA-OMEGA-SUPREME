package com.ivanna.omega.audio.engine

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class LoopLoadGovernorTest {
    private val period = 6_666_666L
    private fun feed(g: LoopLoadGovernor, frac: Double, n: Int) = repeat(n) { g.update((period * frac).toLong(), period) }

    @Test fun `con carga baja no decima nada`() {
        val g = LoopLoadGovernor(); feed(g, 0.30, 200)
        assertEquals(1, g.stride)
        for (b in 0 until 8) assertTrue(g.runAnalysis(b))
    }

    @Test fun `carga media pide un analisis cada 2 bloques`() {
        val g = LoopLoadGovernor(); feed(g, 0.65, 200)
        assertEquals(2, g.stride)
        assertTrue(g.runAnalysis(0)); assertFalse(g.runAnalysis(1)); assertTrue(g.runAnalysis(2))
    }

    @Test fun `carga alta pide uno cada 4`() {
        val g = LoopLoadGovernor(); feed(g, 0.90, 200)
        assertEquals(4, g.stride)
        assertEquals(2, (0 until 8).count { g.runAnalysis(it) })
    }

    @Test fun `la histeresis evita oscilar justo en el umbral`() {
        val g = LoopLoadGovernor(); feed(g, 0.60, 200)
        assertEquals(2, g.stride)
        feed(g, 0.52, 200)           // por debajo de UP_2 pero dentro de la histeresis
        assertEquals(2, g.stride)
        feed(g, 0.40, 200)           // claramente por debajo
        assertEquals(1, g.stride)
    }

    @Test fun `baja de 4 a 2 y luego a 1 al recuperarse`() {
        val g = LoopLoadGovernor(); feed(g, 0.95, 200)
        assertEquals(4, g.stride)
        feed(g, 0.60, 300); assertEquals(2, g.stride)
        feed(g, 0.20, 300); assertEquals(1, g.stride)
    }

    @Test fun `entradas invalidas no alteran el estado`() {
        val g = LoopLoadGovernor(); feed(g, 0.9, 100)
        val s = g.stride; val l = g.load
        g.update(-1L, period); g.update(1000L, 0L)
        assertEquals(s, g.stride); assertEquals(l, g.load, 0f)
    }

    @Test fun `un pico aislado no cambia el paso`() {
        val g = LoopLoadGovernor(); feed(g, 0.30, 100)
        g.update(period * 3, period)   // un bloque de 3x el periodo (p. ej. GC)
        assertEquals(1, g.stride)
    }

    @Test fun `reset vuelve a estado nominal`() {
        val g = LoopLoadGovernor(); feed(g, 0.95, 100); g.reset()
        assertEquals(1, g.stride); assertEquals(0f, g.load, 0f)
    }
}
