package com.ivanna.omega.audio

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class CaptureRecordBufferTest {
    // 320 frames * 2 canales * 4 B = 2560 B por bloque.
    private val blockBytes = 320 * 2 * 4

    @Test
    fun `anillo de captura es al menos 4 bloques aunque getMinBufferSize sea menor`() {
        assertEquals(4 * blockBytes, PlaybackCaptureService.recordBufferBytes(blockBytes))
        assertEquals(4 * blockBytes, PlaybackCaptureService.recordBufferBytes(0))
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
