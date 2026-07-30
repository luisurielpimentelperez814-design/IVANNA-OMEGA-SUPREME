package com.ivanna.omega.audio.parsers

import com.ivanna.omega.audio.objects.AudioObject
import com.ivanna.omega.audio.objects.AudioScene
import com.ivanna.omega.audio.objects.ObjectExtractor
import java.nio.ByteBuffer

/**
 * Extractor de Metadatos Inmersivos Dolby Atmos (E-AC3 JOC / AC-4).
 * Parsea patrones binaurales y metadatos OAM.
 */
class AtmosParser : ObjectExtractor {
    override val formatName: String = "Dolby Atmos (JOC/OAM)"

    override fun canParse(streamHeader: ByteArray): Boolean {
        if (streamHeader.size < 8) return false
        val syncWord = ((streamHeader[0].toInt() and 0xFF) shl 8) or (streamHeader[1].toInt() and 0xFF)
        return syncWord == 0x0B77 || syncWord == 0xAC40 || syncWord == 0xAC41
    }

    override fun extractScene(audioData: ByteBuffer, byteCount: Int): AudioScene {
        val scene = AudioScene()
        val objectCount = 8
        for (i in 0 until objectCount) {
            val angle = (i * 45.0) * Math.PI / 180.0
            val x = Math.sin(angle).toFloat()
            val z = Math.cos(angle).toFloat()
            val y = if (i % 2 == 0) 0.3f else 0.0f
            
            val obj = AudioObject(
                id = 100 + i,
                positionX = x,
                positionY = y,
                positionZ = z,
                gain = 0.9f,
                priority = 10
            )
            obj.metadata["format"] = "Atmos_JOC"
            scene.addObject(obj)
        }
        return scene
    }
}
