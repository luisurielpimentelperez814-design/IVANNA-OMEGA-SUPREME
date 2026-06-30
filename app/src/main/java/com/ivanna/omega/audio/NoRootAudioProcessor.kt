package com.ivanna.omega.audio

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import android.media.audiofx.DynamicsProcessing
import android.os.Build
import android.util.Log
import java.io.File

/**
 * NoRootAudioProcessor — Fallback cuando no hay Magisk/root.
 * Usa AudioTrack interno + DynamicsProcessing API 28+.
 * FIX v1.5: detecta ausencia de Magisk y procesa sin privilegios.
 */
class NoRootAudioProcessor(private val context: Context) {
    companion object {
        private const val TAG = "NoRootAudio"
        private const val SAMPLE_RATE = 48000
        private const val CHANNEL_CONFIG = AudioFormat.CHANNEL_OUT_STEREO
        private const val AUDIO_FORMAT = AudioFormat.ENCODING_PCM_FLOAT
        private const val BUFFER_SIZE = 4096
    }

    private var audioTrack: AudioTrack? = null
    private var dynamicsProcessing: DynamicsProcessing? = null
    private var isRunning = false

    /**
     * Verifica si el dispositivo tiene root/Magisk activo.
     */
    fun hasMagisk(): Boolean {
        return File("/data/adb/magisk").exists() ||
               File("/sbin/magisk").exists() ||
               File("/system/bin/magisk").exists()
    }

    /**
     * Inicia el procesamiento en modo no-root.
     * Usa DynamicsProcessing si está disponible (API 28+).
     */
    fun start(): Boolean {
        if (hasMagisk()) {
            Log.i(TAG, "Magisk detectado — modo no-root no necesario")
            return false
        }

        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.P) {
            Log.w(TAG, "DynamicsProcessing requiere API 28+ (Android 9+)")
            return false
        }

        try {
            val minBuffer = AudioTrack.getMinBufferSize(
                SAMPLE_RATE, CHANNEL_CONFIG, AUDIO_FORMAT
            )

            audioTrack = AudioTrack.Builder()
                .setAudioAttributes(
                    AudioAttributes.Builder()
                        .setUsage(AudioAttributes.USAGE_MEDIA)
                        .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                        .build()
                )
                .setAudioFormat(
                    AudioFormat.Builder()
                        .setSampleRate(SAMPLE_RATE)
                        .setEncoding(AUDIO_FORMAT)
                        .setChannelMask(CHANNEL_CONFIG)
                        .build()
                )
                .setBufferSizeInBytes(minBuffer.coerceAtLeast(BUFFER_SIZE * 4))
                .setTransferMode(AudioTrack.MODE_STREAM)
                .build()

            // Configurar DynamicsProcessing como fallback
            setupDynamicsProcessing()

            audioTrack?.play()
            isRunning = true
            Log.i(TAG, "NoRootAudioProcessor iniciado (DynamicsProcessing fallback)")
            return true
        } catch (e: Exception) {
            Log.e(TAG, "Error iniciando NoRootAudioProcessor: ${e.message}")
            return false
        }
    }

    private fun setupDynamicsProcessing() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.P) return

        try {
            // Crear configuración de DynamicsProcessing usando Builder
            val config = DynamicsProcessing.Config.Builder(
                DynamicsProcessing.VARIANT_FAVOR_FREQUENCY_RESOLUTION,
                2, // channelCount (stereo)
                true, // preEqEnabled
                4, // preEqBands
                true, // mbcEnabled
                4, // mbcBands
                true, // postEqEnabled
                4, // postEqBands
                true // limiterEnabled
            ).build()

            // Crear DynamicsProcessing con la configuración
            val dp = DynamicsProcessing(0, 2, config)

            // Configurar bandas PostEQ para preset Anti-Dolby básico
            // EqBand(enabled, frequency, gain) - SIN parámetro Q
            dp.setPostEqBandAllChannelsTo(0, DynamicsProcessing.EqBand(true, 200f, 2.0f))
            dp.setPostEqBandAllChannelsTo(1, DynamicsProcessing.EqBand(true, 1000f, 0.0f))
            dp.setPostEqBandAllChannelsTo(2, DynamicsProcessing.EqBand(true, 4000f, 2.0f))
            dp.setPostEqBandAllChannelsTo(3, DynamicsProcessing.EqBand(true, 8000f, 1.5f))

            dp.enabled = true
            dynamicsProcessing = dp

            Log.i(TAG, "DynamicsProcessing configurado con preset Anti-Dolby básico")
        } catch (e: Exception) {
            Log.w(TAG, "DynamicsProcessing no disponible: ${e.message}")
        }
    }

    fun stop() {
        isRunning = false
        try {
            audioTrack?.stop()
            audioTrack?.release()
            audioTrack = null
            dynamicsProcessing?.release()
            dynamicsProcessing = null
        } catch (e: Exception) {
            Log.e(TAG, "Error deteniendo: ${e.message}")
        }
    }

    fun isActive(): Boolean = isRunning
}
