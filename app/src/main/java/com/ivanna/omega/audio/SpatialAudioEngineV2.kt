package com.ivannafusion

import android.media.AudioFormat
import android.media.AudioRecord
import android.media.AudioTrack
import android.media.MediaRecorder
import kotlinx.coroutines.*

class SpatialAudioEngineV2 {
    private val scope = CoroutineScope(Dispatchers.IO + SupervisorJob())
    private var isRunning = false
    private var audioRecord: AudioRecord? = null
    private var audioTrack: AudioTrack? = null
    private val bufferSize = 64
    private val sampleRate = 48000

    fun start() {
        if (isRunning) return
        isRunning = true

        IvannaNativeLib.nativeInitSpatialEngine(sampleRate, bufferSize)

        val recordBufferSize = AudioRecord.getMinBufferSize(sampleRate, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT)
        audioRecord = AudioRecord(MediaRecorder.AudioSource.MIC, sampleRate, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT, recordBufferSize)

        val trackBufferSize = AudioTrack.getMinBufferSize(sampleRate, AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_16BIT)
        audioTrack = AudioTrack.Builder()
            .setAudioAttributes(android.media.AudioAttributes.Builder()
                .setUsage(android.media.AudioAttributes.USAGE_MEDIA)
                .setContentType(android.media.AudioAttributes.CONTENT_TYPE_MUSIC)
                .build())
            .setAudioFormat(android.media.AudioFormat.Builder()
                .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                .setSampleRate(sampleRate)
                .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO)
                .build())
            .setBufferSizeInBytes(trackBufferSize)
            .build()

        audioRecord?.startRecording()
        audioTrack?.play()

        scope.launch {
            val input = ShortArray(bufferSize)  // AudioRecord.read() con ShortArray
            val outL = FloatArray(bufferSize)
            val outR = FloatArray(bufferSize)
            while (isRunning) {
                val read = audioRecord?.read(input, 0, bufferSize) ?: 0
                if (read > 0) {
                    // Convertir ShortArray a FloatArray para el motor espacial
                    val inputFloat = FloatArray(bufferSize)
                    for (i in 0 until read) {
                        inputFloat[i] = input[i] / 32767.0f
                    }
                    val posX = DSPState.posX
                    val posY = DSPState.posY
                    val posZ = DSPState.posZ
                    val mu = DSPState.mu
                    IvannaNativeLib.nativeRenderSpatialBlock(inputFloat, outL, outR, posX, posY, posZ, mu)
                    // Mezclar y reproducir (float a short)
                    val mixed = ShortArray(bufferSize * 2)
                    for (i in 0 until bufferSize) {
                        mixed[i * 2] = (outL[i] * 32767).toInt().coerceIn(-32768, 32767).toShort()
                        mixed[i * 2 + 1] = (outR[i] * 32767).toInt().coerceIn(-32768, 32767).toShort()
                    }
                    audioTrack?.write(mixed, 0, mixed.size, AudioTrack.WRITE_BLOCKING)
                }
            }
        }
    }

    fun stop() {
        isRunning = false
        scope.cancel()
        audioRecord?.stop()
        audioRecord?.release()
        audioTrack?.stop()
        audioTrack?.release()
        IvannaNativeLib.nativeReleaseSpatialEngine()
    }
}
