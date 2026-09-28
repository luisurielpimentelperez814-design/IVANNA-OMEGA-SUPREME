package com.ivanna.omega.audio

import android.media.AudioAttributes
import android.media.AudioFocusRequest
import android.media.AudioManager
import android.os.Build
import android.util.Log

class AudioCallbackManager(
    private val audioManager: AudioManager,
    private val onFocusChange: (Int) -> Unit = {}
) {
    companion object { private const val TAG = "AudioCallbackManager" }

    private var audioFocusRequest: AudioFocusRequest? = null
    private var isAudioFocusOwned = false

    // FIX (eco/desfase en captura de sistema): requestAudioFocus(duck=true) usa
    // AUDIOFOCUS_GAIN_TRANSIENT_MAY_DUCK en vez de AUDIOFOCUS_GAIN. Con GAIN
    // pleno, Tidal/Qobuz/YouTube normalmente PAUSAN su reproducción al perder
    // el foco — rompiendo la fuente que PlaybackCaptureService necesita seguir
    // capturando. Con TRANSIENT_MAY_DUCK, Android le pide a la app de origen
    // que baje su propio volumen (ducking estándar del sistema, no un
    // setStreamVolume global) en vez de detenerse, mientras IVANNA reproduce su
    // copia procesada — evitando la superposición a volumen pleno de dos
    // señales casi-idénticas y desfasadas que producía el eco/comb filtering.
    // setWillPauseWhenDucked(false) es explícito: sin esto algunas apps OEM
    // pausan igual bajo TRANSIENT_MAY_DUCK.
    fun requestAudioFocus(duck: Boolean = false): Boolean {
        return try {
            val attrs = AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_MEDIA)
                .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                .build()
            val focusType = if (duck) AudioManager.AUDIOFOCUS_GAIN_TRANSIENT_MAY_DUCK
                             else AudioManager.AUDIOFOCUS_GAIN
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                audioFocusRequest = AudioFocusRequest.Builder(focusType)
                    .setAudioAttributes(attrs)
                    .setWillPauseWhenDucked(false)
                    .setOnAudioFocusChangeListener { onAudioFocusChange(it) }
                    .build()
                val result = audioFocusRequest?.let { audioManager.requestAudioFocus(it) } ?: android.media.AudioManager.AUDIOFOCUS_REQUEST_FAILED
                isAudioFocusOwned = result == AudioManager.AUDIOFOCUS_REQUEST_GRANTED
                isAudioFocusOwned
            } else {
                @Suppress("DEPRECATION")
                val result = audioManager.requestAudioFocus(
                    { onAudioFocusChange(it) },
                    AudioManager.STREAM_MUSIC,
                    focusType
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
                audioFocusRequest?.let { audioManager.abandonAudioFocusRequest(it) }
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
        onFocusChange(focusChange) // FIX: antes no propagaba a nadie — Volterra u
        // otro consumidor de audio focus tumbaba la reproducción sin aviso.
    }

    // FIX (separación de rutas): muteUnwantedNoise()/restoreAudioStreams()
    // llamaban AudioManager.setStreamVolume() — IVANNA escribía en el mixer de
    // Android (volumen del sistema, el mismo que Tidal y los botones físicos).
    // Nadie las consumía: código muerto con efecto global. IVANNA solo procesa
    // DSP espacial; el volumen del sistema pertenece a Android.
    fun getAudioState(): String {
        return try {
            val vol = audioManager.getStreamVolume(AudioManager.STREAM_MUSIC)
            val max = audioManager.getStreamMaxVolume(AudioManager.STREAM_MUSIC)
            "Focus: ${if (isAudioFocusOwned) "Si" else "No"} | Vol: $vol/$max"
        } catch (e: Exception) { "Error" }
    }
}
