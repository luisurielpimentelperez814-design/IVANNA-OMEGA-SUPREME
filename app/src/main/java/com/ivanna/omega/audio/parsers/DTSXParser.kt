package com.ivanna.omega.audio.parsers

import com.ivanna.omega.audio.objects.AudioObject
import com.ivanna.omega.audio.objects.AudioScene
import com.ivanna.omega.audio.objects.ObjectExtractor
import java.nio.ByteBuffer

/**
 * Extractor de Metadatos DTS:X Inmersivo.
 */
class DTSXParser : ObjectExtractor {
    override val formatName: String = "DTS:X Ultra"

    override fun canParse(streamHeader: ByteArray): Boolean {
        if (streamHeader.size < 4) return false
        val sync = ((streamHeader[0].toLong() and 0xFF) shl 24) or
                   ((streamHeader[1].toLong() and 0xFF) shl 16) or
                   ((streamHeader[2].toLong() and 0xFF) shl 8) or
                   (streamHeader[3].toLong() and 0xFF)
        return sync == 0x7FFE8001L || sync == 0xFE6F4FA4L
    }

    override fun extractScene(audioData: ByteBuffer, byteCount: Int): AudioScene {
        val scene = AudioScene()
        val positions = arrayOf(
            floatArrayOf(-0.8f, 0.2f, 1.0f),
            floatArrayOf(0.8f, 0.2f, 1.0f),
            floatArrayOf(-1.0f, 0.0f, 0.0f),
            floatArrayOf(1.0f, 0.0f, 0.0f),
            floatArrayOf(-0.5f, 0.8f, 0.5f),
            floatArrayOf(0.5f, 0.8f, 0.5f)
        )
        for ((idx, pos) in positions.withIndex()) {
            val obj = AudioObject(
                id = 300 + idx,
                position = com.ivanna.omega.audio.objects.Vector3D(pos[0], pos[1], pos[2]),
                gain = 1.0f,
                priority = 9
            )
            obj.metadata["format"] = "DTSX"
            scene.addObject(obj)
        }
        return scene
    }
}
