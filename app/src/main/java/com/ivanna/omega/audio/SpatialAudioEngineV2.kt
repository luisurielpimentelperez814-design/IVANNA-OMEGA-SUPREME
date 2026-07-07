package com.ivanna.omega.audio

import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.AudioTrack
import android.media.MediaRecorder
import com.ivanna.omega.core.IvannaNativeLib
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import kotlinx.coroutines.cancel

class SpatialAudioEngineV2 {
    var posX: Float = 0.0f
    var posY: Float = 0.0f
    var posZ: Float = 0.0f
    var mu: Float = 1.0f

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

        val recBufSize = AudioRecord.getMinBufferSize(sampleRate, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT)
        audioRecord = AudioRecord(MediaRecorder.AudioSource.MIC, sampleRate, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT, recBufSize)

        val trkBufSize = AudioTrack.getMinBufferSize(sampleRate, AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_16BIT)
        audioTrack = AudioTrack.Builder()
           .setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA).setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
           .setAudioFormat(AudioFormat.Builder().setEncoding(AudioFormat.ENCODING_PCM_16BIT).setSampleRate(sampleRate).setChannelMask(AudioFormat.CHANNEL_OUT_STEREO).build())
           .setBufferSizeInBytes(trkBufSize)
           .build()

        audioRecord?.startRecording()
        audioTrack?.play()

        scope.launch {
            val input = ShortArray(bufferSize)
            val outL = FloatArray(bufferSize)
            val outR = FloatArray(bufferSize)
            while (isRunning) {
                val read = audioRecord?.read(input, 0, bufferSize)?: 0
                if (read > 0) {
                    val inputFloat = FloatArray(bufferSize)
                    for (i in 0 until read) inputFloat[i] = input[i] / 32767.0f
                    IvannaNativeLib.nativeRenderSpatialBlock(inputFloat, outL, outR, posX.toInt(), posY.toInt(), posZ.toInt(), mu.toInt())
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
        audioRecord?.stop(); audioRecord?.release(); audioRecord = null
        audioTrack?.stop(); audioTrack?.release(); audioTrack = null
        IvannaNativeLib.nativeReleaseSpatialEngine()
    }
}
