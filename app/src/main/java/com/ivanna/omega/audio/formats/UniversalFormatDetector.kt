package com.ivanna.omega.audio.formats

import java.nio.ByteBuffer
import java.nio.ByteOrder

enum class ContainerFormat {
    PCM_RAW,
    WAV,
    FLAC,
    AAC,
    ALAC,
    OPUS,
    DOLBY_ATMOS_EAC3,
    DTS_X,
    MPEG_H,
    UNKNOWN
}

data class FormatMetadata(
    val format: ContainerFormat,
    val sampleRate: Int,
    val bitDepth: Int,
    val channels: Int,
    val isObjectBased: Boolean,
    val numObjects: Int,
    val bitRateKbps: Int,
    val extraMetadata: Map<String, String> = emptyMap()
)

class UniversalFormatDetector {

    interface FormatParserExtension {
        fun canParse(header: ByteArray): Boolean
        fun parse(header: ByteArray, streamLength: Long): FormatMetadata?
    }

    private val extensions = mutableListOf<FormatParserExtension>()

    fun registerExtension(extension: FormatParserExtension) {
        extensions.add(extension)
    }

    fun detect(header: ByteArray, streamLength: Long = -1L): FormatMetadata {
        if (header.size < 16) {
            return FormatMetadata(ContainerFormat.UNKNOWN, 44100, 16, 2, false, 0, 0)
        }

        // Custom extensions check
        for (ext in extensions) {
            if (ext.canParse(header)) {
                ext.parse(header, streamLength)?.let { return it }
            }
        }

        val buffer = ByteBuffer.wrap(header).order(ByteOrder.BIG_ENDIAN)
        val magic32 = buffer.getInt(0)

        // WAV check: 'RIFF' ... 'WAVE'
        if (header.size >= 12 &&
            header[0] == 'R'.toByte() && header[1] == 'I'.toByte() &&
            header[2] == 'F'.toByte() && header[3] == 'F'.toByte() &&
            header[8] == 'W'.toByte() && header[9] == 'A'.toByte() &&
            header[10] == 'V'.toByte() && header[11] == 'E'.toByte()
        ) {
            val leBuf = ByteBuffer.wrap(header).order(ByteOrder.LITTLE_ENDIAN)
            val channels = leBuf.getShort(22).toInt().coerceAtLeast(1)
            val sampleRate = leBuf.getInt(24).coerceAtLeast(8000)
            val bitDepth = leBuf.getShort(34).toInt().coerceAtLeast(16)
            return FormatMetadata(
                format = ContainerFormat.WAV,
                sampleRate = sampleRate,
                bitDepth = bitDepth,
                channels = channels,
                isObjectBased = false,
                numObjects = 0,
                bitRateKbps = (sampleRate * channels * bitDepth) / 1000
            )
        }

        // FLAC check: 'fLaC'
        if (header[0] == 'f'.toByte() && header[1] == 'L'.toByte() &&
            header[2] == 'a'.toByte() && header[3] == 'C'.toByte()
        ) {
            return FormatMetadata(
                format = ContainerFormat.FLAC,
                sampleRate = 96000,
                bitDepth = 24,
                channels = 2,
                isObjectBased = false,
                numObjects = 0,
                bitRateKbps = 1411
            )
        }

        // Opus check: 'OggS'
        if (header[0] == 'O'.toByte() && header[1] == 'g'.toByte() &&
            header[2] == 'g'.toByte() && header[3] == 'S'.toByte()
        ) {
            return FormatMetadata(
                format = ContainerFormat.OPUS,
                sampleRate = 48000,
                bitDepth = 24,
                channels = 2,
                isObjectBased = false,
                numObjects = 0,
                bitRateKbps = 320
            )
        }

        // Dolby Atmos (E-AC3 with JOC metadata sync frame check 0x0B77)
        val syncWord = (header[0].toInt() and 0xFF shl 8) or (header[1].toInt() and 0xFF)
        if (syncWord == 0x0B77) {
            val hasJocMetadata = (header.size > 10 && (header[8].toInt() and 0x20) != 0)
            return FormatMetadata(
                format = ContainerFormat.DOLBY_ATMOS_EAC3,
                sampleRate = 48000,
                bitDepth = 24,
                channels = 8,
                isObjectBased = true,
                numObjects = if (hasJocMetadata) 12 else 6,
                bitRateKbps = 768,
                extraMetadata = mapOf("JOC_3D_METADATA" to "ACTIVE", "BED_LAYOUT" to "7.1.4")
            )
        }

        // DTS:X Sync (0x7FFE8001 or 0x64582025)
        if (magic32 == 0x7FFE8001 || magic32 == 0x64582025) {
            return FormatMetadata(
                format = ContainerFormat.DTS_X,
                sampleRate = 48000,
                bitDepth = 24,
                channels = 8,
                isObjectBased = true,
                numObjects = 8,
                bitRateKbps = 1536,
                extraMetadata = mapOf("PROFILE" to "DTS_X_NEURAL")
            )
        }

        // MPEG-H 3D Audio Sync (0x4D483341 -> 'MH3A')
        if (header[0] == 'M'.toByte() && header[1] == 'H'.toByte() &&
            header[2] == '3'.toByte() && header[3] == 'A'.toByte()
        ) {
            return FormatMetadata(
                format = ContainerFormat.MPEG_H,
                sampleRate = 48000,
                bitDepth = 24,
                channels = 12,
                isObjectBased = true,
                numObjects = 16,
                bitRateKbps = 1280,
                extraMetadata = mapOf("MPEGH_PROFILE" to "3D_AUDIO_LEVEL_4")
            )
        }

        // Fallback: Raw PCM stereo 48kHz / 24-bit
        return FormatMetadata(
            format = ContainerFormat.PCM_RAW,
            sampleRate = 48000,
            bitDepth = 24,
            channels = 2,
            isObjectBased = false,
            numObjects = 0,
            bitRateKbps = 2304
        )
    }
}
