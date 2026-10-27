package com.ivanna.omega.audio

import android.media.AudioDeviceInfo
import android.media.AudioTrack

/**
 * Read-back of the app-owned AudioTrack route. A requested preferred device is
 * not treated as proof: the UI reports the route Android actually exposes.
 */
data class AudioTrackRouteSnapshot(
    val status: String = "sin reproducción",
    val requestedDevice: String = "automática",
    val actualDevice: String = "sin lectura",
    val sampleRateHz: Int = 0,
    val actualDeviceType: Int? = null
)

object AudioTrackRouteReadback {
    @Volatile private var snapshot = AudioTrackRouteSnapshot()

    fun current(): AudioTrackRouteSnapshot = snapshot

    fun update(track: AudioTrack, expected: AudioDeviceInfo?, sampleRateHz: Int) {
        val actual = runCatching { track.routedDevice }.getOrNull()
        val requestedName = expected?.productName?.toString()?.takeIf { it.isNotBlank() }
            ?: if (expected == null) "política de Android" else "dispositivo preferido"
        val actualName = actual?.productName?.toString()?.takeIf { it.isNotBlank() }
            ?: "sin lectura de Android"
        val status = when {
            actual == null -> "esperando confirmación de Android"
            expected != null && actual.id == expected.id -> "ruta solicitada confirmada"
            expected != null -> "Android eligió una ruta distinta"
            else -> "ruta real detectada"
        }
        snapshot = AudioTrackRouteSnapshot(
            status = status,
            requestedDevice = requestedName,
            actualDevice = actualName,
            sampleRateHz = sampleRateHz,
            actualDeviceType = actual?.type
        )
    }

    fun reset() {
        snapshot = AudioTrackRouteSnapshot()
    }
}
