package com.ivanna.omega.audio.objects

import java.nio.ByteBuffer

/**
 * Interfaz universal para extracción de objetos inmersivos desde streams de audio.
 */
interface ObjectExtractor {
    val formatName: String
    fun canParse(streamHeader: ByteArray): Boolean
    fun extractScene(audioData: ByteBuffer, byteCount: Int): AudioScene
}
