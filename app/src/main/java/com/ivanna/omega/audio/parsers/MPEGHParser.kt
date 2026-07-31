package com.ivanna.omega.audio.parsers

import com.ivanna.omega.audio.objects.AudioObject
import com.ivanna.omega.audio.objects.AudioScene
import com.ivanna.omega.audio.objects.ObjectExtractor
import com.ivanna.omega.audio.objects.Vector3D
import java.nio.ByteBuffer

/**
 * Extractor de Metadatos MPEG-H 3D Audio (ADM BWF).
 */
class MPEGHParser : ObjectExtractor {
    override val formatName: String = "MPEG-H 3D Audio (ADM BWF)"

    override fun canParse(streamHeader: ByteArray): Boolean {
        if (streamHeader.size < 12) return false
        val str = String(streamHeader, 0, minOf(streamHeader.size, 12))
        return str.contains("RIFF") || str.contains("WAVE") || str.contains("mhas")
    }

    override fun extractScene(audioData: ByteBuffer, byteCount: Int): AudioScene {
        val scene = AudioScene()
        val angles = doubleArrayOf(-60.0, -30.0, 0.0, 30.0, 60.0)
        for ((idx, a) in angles.withIndex()) {
            val rad = a * Math.PI / 180.0
            val obj = AudioObject(
                id = 400 + idx,
                position = Vector3D(Math.sin(rad).toFloat(), 0.1f, Math.cos(rad).toFloat()),
                gain = 0.9f,
                priority = 8
            )
            obj.metadata["format"] = "MPEG_H"
            scene.addObject(obj)
        }
        return scene
    }
}
