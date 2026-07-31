package com.ivanna.omega.audio.objects

import com.ivanna.omega.audio.formats.ContainerFormat
import com.ivanna.omega.audio.formats.FormatMetadata

data class Vector3D(
    var x: Float = 0.0f, // -1.0 (left) to +1.0 (right)
    var y: Float = 0.0f, // -1.0 (back) to +1.0 (front)
    var z: Float = 0.0f  // -1.0 (below) to +1.0 (above)
)

data class AudioObject(
    val id: Int,

    var position: Vector3D = Vector3D(),

    var gain: Float = 1.0f,
    var priority: Int = 1,
    var spread: Float = 0.0f,

    val metadata: MutableMap<String, String> = mutableMapOf()
) {

    var positionX: Float
        get() = position.x
        set(value) {
            position.x = value
        }

    var positionY: Float
        get() = position.y
        set(value) {
            position.y = value
        }

    var positionZ: Float
        get() = position.z
        set(value) {
            position.z = value
        }
}

data class ChannelBed(
    val channelLayout: String, // "2.0", "5.1", "7.1.4"
    val channelGains: FloatArray,
    var masterGain: Float = 1.0f
) {
    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (other !is ChannelBed) return false
        return channelLayout == other.channelLayout && channelGains.contentEquals(other.channelGains)
    }

    override fun hashCode(): Int {
        var result = channelLayout.hashCode()
        result = 31 * result + channelGains.contentHashCode()
        return result
    }
}

data class UniversalAudioFrame(
    val frameIndex: Long,
    val timestampNs: Long,
    val channelBed: ChannelBed,
    val objects: List<AudioObject>,
    val formatMetadata: FormatMetadata
)

class UniversalAudioConverter {

    fun convertToUniversalFrame(
        frameIdx: Long,
        timestampNs: Long,
        rawPcmSamples: FloatArray,
        metadata: FormatMetadata
    ): UniversalAudioFrame {
        val objectsList = mutableListOf<AudioObject>()

        when (metadata.format) {
            ContainerFormat.DOLBY_ATMOS_EAC3, ContainerFormat.DTS_X, ContainerFormat.MPEG_H -> {
                // Dynamically synthesize spatial audio objects from extracted metadata
                val numObjs = metadata.numObjects.coerceAtLeast(1)
                for (i in 0 until numObjs) {
                    val angle = (2.0 * Math.PI * i / numObjs).toFloat()
                    val elevation = if (i % 2 == 0) 0.5f else -0.2f
                    objectsList.add(
                        AudioObject(
                            id = i + 1,
                            position = Vector3D(
                                x = Math.sin(angle.toDouble()).toFloat(),
                                y = Math.cos(angle.toDouble()).toFloat(),
                                z = elevation
                            ),
                            gain = 0.85f,
                            priority = if (i == 0) 10 else 5,
                            spread = 0.2f,
                            metadata = mutableMapOf("TYPE" to if (i == 0) "DIALOG" else "EFFECT")
                        )
                    )
                }
            }
            else -> {
                // Stereo / Multi-channel static bed conversion
                objectsList.add(
                    AudioObject(
                        id = 0,
                        position = Vector3D(0.0f, 1.0f, 0.0f),
                        gain = 1.0f,
                        priority = 10,
                        spread = 0.8f,
                        metadata = mutableMapOf("TYPE" to "STEREO_BED")
                    )
                )
            }
        }

        val channelGains = FloatArray(metadata.channels) { 1.0f }
        val bed = ChannelBed(
            channelLayout = if (metadata.channels == 8) "7.1" else "2.0",
            channelGains = channelGains,
            masterGain = 1.0f
        )

        return UniversalAudioFrame(
            frameIndex = frameIdx,
            timestampNs = timestampNs,
            channelBed = bed,
            objects = objectsList,
            formatMetadata = metadata
        )
    }
}
