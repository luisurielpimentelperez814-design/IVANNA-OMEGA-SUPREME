package com.ivanna.omega.audio

import android.media.AudioAttributes
import android.media.AudioFocusRequest
import android.media.AudioManager
import android.os.Build
import android.util.Log

class AudioCallbackManager(private val audioManager: AudioManager) {
    companion object { private const val TAG = "AudioCallbackManager" }

    private var audioFocusRequest: AudioFocusRequest? = null
    private var isAudioFocusOwned = false

    fun requestAudioFocus(): Boolean {
        return try {
            val attrs = AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_MEDIA)
                .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                .build()
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                audioFocusRequest = AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
                    .setAudioAttributes(attrs)
                    .setOnAudioFocusChangeListener { onAudioFocusChange(it) }
                    .build()
                val result = audioManager.requestAudioFocus(audioFocusRequest!!)
                isAudioFocusOwned = result == AudioManager.AUDIOFOCUS_REQUEST_GRANTED
                isAudioFocusOwned
            } else {
                @Suppress("DEPRECATION")
                val result = audioManager.requestAudioFocus(
                    { onAudioFocusChange(it) },
                    AudioManager.STREAM_MUSIC,
                    AudioManager.AUDIOFOCUS_GAIN
                )
                isAudioFocusOwned = result == AudioManager.AUDIOFOCUS_REQUEST_GRANTED
                isAudioFocusOwned
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error", e)
            false
        }
    }

    fun abandonAudioFocus() {
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O && audioFocusRequest != null) {
                audioManager.abandonAudioFocusRequest(audioFocusRequest!!)
                audioFocusRequest = null
            } else {
                @Suppress("DEPRECATION")
                audioManager.abandonAudioFocus(null)
            }
            isAudioFocusOwned = false
        } catch (e: Exception) {
            Log.e(TAG, "Error", e)
        }
    }

    private fun onAudioFocusChange(focusChange: Int) {
        Log.d(TAG, "Focus change: $focusChange")
    }

    fun muteUnwantedNoise() {
        try {
            @Suppress("DEPRECATION")
            audioManager.setStreamVolume(AudioManager.STREAM_NOTIFICATION, 0, 0)
        } catch (e: Exception) {
            Log.e(TAG, "Error", e)
        }
    }

    fun restoreAudioStreams() {
        try {
            val maxNotif = audioManager.getStreamMaxVolume(AudioManager.STREAM_NOTIFICATION)
            @Suppress("DEPRECATION")
            audioManager.setStreamVolume(AudioManager.STREAM_NOTIFICATION, maxNotif / 2, 0)
        } catch (e: Exception) {
            Log.e(TAG, "Error", e)
        }
    }

    fun getAudioState(): String {
        return try {
            val vol = audioManager.getStreamVolume(AudioManager.STREAM_MUSIC)
            val max = audioManager.getStreamMaxVolume(AudioManager.STREAM_MUSIC)
            "Focus: ${if (isAudioFocusOwned) "Si" else "No"} | Vol: $vol/$max"
        } catch (e: Exception) { "Error" }
    }
}
