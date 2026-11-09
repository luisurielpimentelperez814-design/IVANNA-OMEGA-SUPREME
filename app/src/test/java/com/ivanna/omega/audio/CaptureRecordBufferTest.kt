package com.ivanna.omega.audio

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class CaptureRecordBufferTest {
    // 320 frames * 2 canales * 4 B = 2560 B por bloque.
    private val blockBytes = 320 * 2 * 4

    // v2.5.374 — REC_BUFFER_BLOCKS 4 → 8 (53.3 ms): el test anterior
    // esperaba 4 bloques y fallaba en CI (esperaba 4, el código da 8).
    @Test
    fun `anillo de captura es al menos 8 bloques aunque getMinBufferSize sea menor`() {
        assertEquals(8 * blockBytes, PlaybackCaptureService.recordBufferBytes(blockBytes))
        assertEquals(8 * blockBytes, PlaybackCaptureService.recordBufferBytes(0))
    }

    @Test
    fun `si el minimo del sistema es mayor se respeta`() {
        val big = 16 * blockBytes
        assertEquals(big, PlaybackCaptureService.recordBufferBytes(big))
    }

    @Test
    fun `absorbe al menos 20 ms de jitter a 48 kHz`() {
        val ms = PlaybackCaptureService.recordBufferBytes(0) / 8f / 48f   // 8 B por frame
        assertTrue("anillo=$ms ms", ms >= 20f)
    }
}
