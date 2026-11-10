package com.ivanna.omega.audio.engine

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger

class CaptureBlockRingTest {

    private fun block(marker: Float, samples: Int = 640): FloatArray =
        FloatArray(samples) { marker }

    @Test fun `fifo en orden y sin perdidas con carga normal`() {
        val ring = CaptureBlockRing(slots = 16)
        for (i in 1..10) assertTrue(ring.offer(block(i.toFloat()), 320))
        assertEquals(10, ring.pending())
        for (i in 1..10) {
            val slot = ring.beginRead()
            assertNotNull(slot)
            assertEquals(320, slot!!.frames)
            assertEquals(i.toFloat(), slot.data[0], 0f)
            assertEquals(i.toFloat(), slot.data[639], 0f)
            ring.endRead()
        }
        assertNull(ring.beginRead())
        assertEquals(0, ring.pending())
        assertEquals(10L, ring.offeredBlocks)
        assertEquals(10L, ring.polledBlocks)
        assertEquals(0L, ring.droppedBlocks)
    }

    @Test fun `anillo lleno rechaza sin bloquear y cuenta drops`() {
        val ring = CaptureBlockRing(slots = 4)
        for (i in 1..4) assertTrue(ring.offer(block(i.toFloat()), 320))
        assertFalse(ring.offer(block(5f), 320)) // lleno: drop, no bloqueo
        assertEquals(1L, ring.droppedBlocks)
        assertEquals(4, ring.pending())
        val slot = ring.beginRead()
        assertNotNull(slot)
        assertEquals(1f, slot!!.data[0], 0f)
        ring.endRead()
        assertTrue(ring.offer(block(5f), 320)) // al liberar un slot vuelve a entrar
        assertEquals(4, ring.pending())
    }

    @Test fun `wrap-around de muchas vueltas mantiene el orden`() {
        val ring = CaptureBlockRing(slots = 4)
        var next = 1
        repeat(250) {
            assertTrue(ring.offer(block(next.toFloat()), 320))
            val slot = ring.beginRead()
            assertNotNull(slot)
            assertEquals(next.toFloat(), slot!!.data[0], 0f)
            ring.endRead()
            next++
        }
        assertEquals(250L, ring.offeredBlocks)
        assertEquals(250L, ring.polledBlocks)
        assertEquals(0L, ring.droppedBlocks)
    }

    @Test fun `ofertas invalidas se rechazan sin corromper el anillo`() {
        val ring = CaptureBlockRing(slots = 4)
        assertFalse(ring.offer(block(1f), 0))
        assertFalse(ring.offer(block(1f), -3))
        assertFalse(ring.offer(block(1f), 321)) // 642 muestras > 640
        assertEquals(0, ring.pending())
        assertTrue(ring.offer(block(7f), 320))
        val slot = ring.beginRead()
        assertNotNull(slot)
        assertEquals(7f, slot!!.data[0], 0f)
        ring.endRead()
    }

    @Test fun `reset limpia contadores y secuencias`() {
        val ring = CaptureBlockRing(slots = 4)
        repeat(4) { ring.offer(block(1f), 320) }
        ring.offer(block(9f), 320) // un drop
        ring.reset()
        assertEquals(0, ring.pending())
        assertEquals(0L, ring.offeredBlocks)
        assertEquals(0L, ring.droppedBlocks)
        assertEquals(0L, ring.polledBlocks)
        assertNull(ring.beginRead())
        assertTrue(ring.offer(block(3f), 320))
    }

    @Test fun `productor-consumidor concurrente sin datos rotos ni duplicados`() {
        val ring = CaptureBlockRing(slots = 16)
        val total = 2000
        val stop = AtomicBoolean(false)
        val errors = AtomicInteger(0)
        val consumed = AtomicInteger(0)
        var lastSeen = 0
        val done = CountDownLatch(1)
        val consumer = Thread {
            try {
                while (!stop.get() || ring.pending() > 0) {
                    val slot = ring.beginRead()
                    if (slot == null) {
                        Thread.sleep(0, 100_000) // 0.1 ms
                        continue
                    }
                    val marker = slot.data[0]
                    // Integridad: el bloque entero debe ser el mismo marcador
                    // (una escritura rota mezclaría dos marcadores).
                    for (i in 1 until 640) {
                        if (slot.data[i] != marker) { errors.incrementAndGet(); break }
                    }
                    // Orden estricto, sin duplicados: el marcador es la secuencia.
                    val seq = marker.toInt()
                    if (seq <= lastSeen) errors.incrementAndGet()
                    lastSeen = seq
                    consumed.incrementAndGet()
                    ring.endRead()
                }
            } finally {
                done.countDown()
            }
        }
        consumer.start()
        for (i in 1..total) {
            // Si el anillo se llena el bloque se descarta (contado): el test
            // no exige cero drops bajo carrera, exige cero corrupcion.
            ring.offer(block(i.toFloat()), 320)
        }
        stop.set(true)
        assertTrue(done.await(10, TimeUnit.SECONDS))
        assertEquals(0, errors.get())
        assertTrue(consumed.get() > 0)
        // Invariante contable: ofrecidos = consumidos + pendientes; drops aparte.
        assertEquals(ring.offeredBlocks, consumed.get().toLong() + ring.pending())
    }
}
