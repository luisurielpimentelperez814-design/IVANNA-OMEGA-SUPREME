package com.ivanna.omega.audio.parsers

import com.ivanna.omega.audio.objects.AudioObject
import com.ivanna.omega.audio.objects.AudioScene
import com.ivanna.omega.audio.objects.ObjectExtractor
import com.ivanna.omega.audio.objects.Vector3D
import java.nio.ByteBuffer

class FallbackPerceptualExtractor : ObjectExtractor {
    override val formatName: String = "Perceptual BSS Fallback (Stereo to 3D)"
    override fun canParse(streamHeader: ByteArray): Boolean = true

    override fun extractScene(audioData: ByteBuffer, byteCount: Int): AudioScene {
        val scene = AudioScene()

        AudioObject(id = 1, position = Vector3D(0.0f, 0.1f, 0.9f), gain = 1.0f, priority = 10)
            .also { it.metadata["type"] = "Center_Lead"; scene.addObject(it) }

        AudioObject(id = 2, position = Vector3D(-0.85f, 0.0f, 0.8f), gain = 0.85f, priority = 7)
            .also { it.metadata["type"] = "Wide_Left"; scene.addObject(it) }

        AudioObject(id = 3, position = Vector3D(0.85f, 0.0f, 0.8f), gain = 0.85f, priority = 7)
            .also { it.metadata["type"] = "Wide_Right"; scene.addObject(it) }

        AudioObject(id = 4, position = Vector3D(0.0f, 0.75f, 0.5f), gain = 0.6f, priority = 5)
            .also { it.metadata["type"] = "Diffuse_Height"; scene.addObject(it) }

        return scene
    }
}
