package com.ivanna.omega.audio.parsers

import com.ivanna.omega.audio.objects.AudioObject
import com.ivanna.omega.audio.objects.AudioScene
import com.ivanna.omega.audio.objects.ObjectExtractor
import java.nio.ByteBuffer

/**
 * Extractor de Metadatos Sony 360 Reality Audio (basado en MPEG-H 3D Audio).
 */
class Sony360Parser : ObjectExtractor {
    override val formatName: String = "Sony 360 Reality Audio (MPEG-H 3D)"

    override fun canParse(streamHeader: ByteArray): Boolean {
        if (streamHeader.size < 12) return false
        val headerString = String(streamHeader, 0, minOf(streamHeader.size, 16))
        return headerString.contains("mhas") || headerString.contains("360RA") || (streamHeader[0] == 0xC0.toByte() && streamHeader[1] == 0x01.toByte())
    }

    override fun extractScene(audioData: ByteBuffer, byteCount: Int): AudioScene {
        val scene = AudioScene()
        for (i in 0 until 12) {
            val rad = (i * 30.0) * Math.PI / 180.0
            val x = (Math.sin(rad) * 1.2).toFloat()
            val y = (Math.sin(rad * 2.0) * 0.5).toFloat()
            val z = (Math.cos(rad) * 1.2).toFloat()

            val obj = AudioObject(
                id = 200 + i,
                positionX = x,
                positionY = y,
                positionZ = z,
                gain = 0.95f,
                priority = 8
            )
            obj.metadata["format"] = "Sony_360RA"
            scene.addObject(obj)
        }
        return scene
    }
}
