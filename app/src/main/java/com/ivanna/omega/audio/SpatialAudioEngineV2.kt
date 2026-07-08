package com.ivanna.omega.audio

import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.AudioTrack
import android.media.MediaRecorder
import android.util.Log
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
        try {
            Log.i("SpatialAudioEngineV2", "Iniciando motor binaural...")
            isRunning = true
            
            try {
                IvannaNativeLib.nativeInitSpatialEngine(sampleRate, bufferSize)
                Log.i("SpatialAudioEngineV2", "Motor nativo inicializado: sr=$sampleRate, bufSize=$bufferSize")
            } catch (e: Exception) {
                Log.e("SpatialAudioEngineV2", "nativeInitSpatialEngine falló", e)
                isRunning = false
                return
            }

            val recBufSize = AudioRecord.getMinBufferSize(sampleRate, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT)
            if (recBufSize == AudioRecord.ERROR || recBufSize == AudioRecord.ERROR_BAD_VALUE) {
                Log.e("SpatialAudioEngineV2", "getMinBufferSize() falló: $recBufSize")
                isRunning = false
                return
            }
            
            Log.i("SpatialAudioEngineV2", "Creando AudioRecord: bufSize=$recBufSize")
            audioRecord = AudioRecord(MediaRecorder.AudioSource.MIC, sampleRate, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT, recBufSize)
            if (audioRecord?.state != AudioRecord.STATE_INITIALIZED) {
                Log.e("SpatialAudioEngineV2", "AudioRecord no inicializó. State=${audioRecord?.state}")
                audioRecord?.release()
                audioRecord = null
                isRunning = false
                return
            }

            val trkBufSize = AudioTrack.getMinBufferSize(sampleRate, AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_16BIT)
            if (trkBufSize == AudioTrack.ERROR || trkBufSize == AudioTrack.ERROR_BAD_VALUE) {
                Log.e("SpatialAudioEngineV2", "getMinBufferSize() para track falló: $trkBufSize")
                audioRecord?.release()
                audioRecord = null
                isRunning = false
                return
            }
            
            Log.i("SpatialAudioEngineV2", "Creando AudioTrack: bufSize=$trkBufSize")
            audioTrack = AudioTrack.Builder()
               .setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA).setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
               .setAudioFormat(AudioFormat.Builder().setEncoding(AudioFormat.ENCODING_PCM_16BIT).setSampleRate(sampleRate).setChannelMask(AudioFormat.CHANNEL_OUT_STEREO).build())
               .setBufferSizeInBytes(trkBufSize)
               .build()
            
            if (audioTrack?.state != AudioTrack.STATE_INITIALIZED) {
                Log.e("SpatialAudioEngineV2", "AudioTrack no inicializó. State=${audioTrack?.state}")
                audioRecord?.release()
                audioRecord = null
                audioTrack?.release()
                audioTrack = null
                isRunning = false
                return
            }

            Log.i("SpatialAudioEngineV2", "Iniciando grabación y playback...")
            audioRecord?.startRecording()
            audioTrack?.play()

            scope.launch {
                try {
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
                } catch (e: Exception) {
                    Log.e("SpatialAudioEngineV2", "Error en loop de renderizado", e)
                    isRunning = false
                }
            }
            Log.i("SpatialAudioEngineV2", "Motor binaural iniciado correctamente")
        } catch (e: Exception) {
            Log.e("SpatialAudioEngineV2", "Error al iniciar motor binaural", e)
            isRunning = false
            audioRecord?.release()
            audioRecord = null
            audioTrack?.release()
            audioTrack = null
            try {
                IvannaNativeLib.nativeReleaseSpatialEngine()
            } catch (e2: Exception) {
                Log.e("SpatialAudioEngineV2", "Error al liberar motor nativo", e2)
            }
        }
    }

    fun stop() {
        try {
            Log.i("SpatialAudioEngineV2", "Deteniendo motor binaural...")
            isRunning = false
            scope.cancel()
            
            try {
                audioRecord?.stop()
                Log.i("SpatialAudioEngineV2", "AudioRecord detenido")
            } catch (e: Exception) {
                Log.e("SpatialAudioEngineV2", "Error deteniendo AudioRecord", e)
            }
            
            try {
                audioRecord?.release()
                Log.i("SpatialAudioEngineV2", "AudioRecord liberado")
            } catch (e: Exception) {
                Log.e("SpatialAudioEngineV2", "Error liberando AudioRecord", e)
            }
            audioRecord = null
            
            try {
                audioTrack?.stop()
                Log.i("SpatialAudioEngineV2", "AudioTrack detenido")
            } catch (e: Exception) {
                Log.e("SpatialAudioEngineV2", "Error deteniendo AudioTrack", e)
            }
            
            try {
                audioTrack?.release()
                Log.i("SpatialAudioEngineV2", "AudioTrack liberado")
            } catch (e: Exception) {
                Log.e("SpatialAudioEngineV2", "Error liberando AudioTrack", e)
            }
            audioTrack = null
            
            try {
                IvannaNativeLib.nativeReleaseSpatialEngine()
                Log.i("SpatialAudioEngineV2", "Motor nativo liberado")
            } catch (e: Exception) {
                Log.e("SpatialAudioEngineV2", "Error liberando motor nativo", e)
            }
            
            Log.i("SpatialAudioEngineV2", "Motor binaural detenido")
        } catch (e: Exception) {
            Log.e("SpatialAudioEngineV2", "Error crítico en stop()", e)
        }
    }
}
