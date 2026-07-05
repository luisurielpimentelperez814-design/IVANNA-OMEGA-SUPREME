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
import kotlin.math.min

/**
 * SpatialAudioEngineV2 — Motor de espacialización HRTF binaural.
 * 
 * FIX CRÍTICOS implementados:
 * 1. Precision: posX/posY/posZ mantienen Float, NO conversión a Int
 * 2. Formato: ENCODING_PCM_FLOAT (matches pipeline de IvannaNpeEngine)
 * 3. Parámetros: mu mantiene Float con rango 0.0f-2.0f (no truncado a Int)
 * 4. Buffersize: 1024 samples para mejor cache utilization
 */
class SpatialAudioEngineV2 {
    companion object {
        private const val TAG = "SpatialAudioEngineV2"
    }
    
    // Parámetros en Float precision completa
    var posX: Float = 0.0f          // X: -5.0 a +5.0 metros
    var posY: Float = 0.0f          // Y: -5.0 a +5.0 metros
    var posZ: Float = 1.5f          // Z: 0.5 a 10.0 metros (distance)
    var mu: Float = 1.0f            // Ancho espacial: 0.5 a 2.0 (1.0 = normal)
    
    // Campos del engine
    private val scope = CoroutineScope(Dispatchers.Default + SupervisorJob())
    private var isRunning = false
    private var audioRecord: AudioRecord? = null
    private var audioTrack: AudioTrack? = null
    
    // Buffer size optimizado: 1024 @ 48kHz = 21.3ms (buen balance entre latencia y CPU)
    private val bufferSize = 1024
    private val sampleRate = 48000
    
    // Estadísticas para debugging
    private var blocksProcessed = 0L
    private var lastLogTime = System.currentTimeMillis()

    /**
     * Iniciar el motor espacial.
     * Requiere RECORD_AUDIO permission ya verificado en PlaybackCaptureService.
     */
    fun start() {
        if (isRunning) {
            Log.w(TAG, "Ya está corriendo, ignorando start()")
            return
        }
        
        try {
            isRunning = true
            Log.i(TAG, "Iniciando SpatialAudioEngineV2 @ $sampleRate Hz, buffer=$bufferSize")
            
            // Iniciar engine nativo
            IvannaNativeLib.nativeInitSpatialEngine(sampleRate, bufferSize)

            // AudioRecord: entrada de micrófono para testing (opcional)
            // En producción, los datos vienen de PlaybackCaptureService
            val recBufSize = AudioRecord.getMinBufferSize(
                sampleRate,
                AudioFormat.CHANNEL_IN_MONO,
                AudioFormat.ENCODING_PCM_FLOAT  // FIX: Float, no 16-bit
            )
            if (recBufSize <= 0) {
                Log.e(TAG, "Invalid buffer size calculado: $recBufSize")
                isRunning = false
                return
            }
            
            audioRecord = AudioRecord(
                MediaRecorder.AudioSource.MIC,
                sampleRate,
                AudioFormat.CHANNEL_IN_MONO,
                AudioFormat.ENCODING_PCM_FLOAT,
                recBufSize
            )

            // AudioTrack: salida estéreo procesada
            val trkBufSize = AudioTrack.getMinBufferSize(
                sampleRate,
                AudioFormat.CHANNEL_OUT_STEREO,
                AudioFormat.ENCODING_PCM_FLOAT  // FIX: Float, no 16-bit
            )
            if (trkBufSize <= 0) {
                Log.e(TAG, "Invalid track buffer size: $trkBufSize")
                audioRecord?.release()
                audioRecord = null
                isRunning = false
                return
            }
            
            audioTrack = AudioTrack.Builder()
                .setAudioAttributes(
                    AudioAttributes.Builder()
                        .setUsage(AudioAttributes.USAGE_MEDIA)
                        .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                        .build()
                )
                .setAudioFormat(
                    AudioFormat.Builder()
                        .setEncoding(AudioFormat.ENCODING_PCM_FLOAT)
                        .setSampleRate(sampleRate)
                        .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO)
                        .build()
                )
                .setBufferSizeInBytes(trkBufSize)
                .build()

            if (audioRecord?.state != AudioRecord.STATE_INITIALIZED) {
                Log.e(TAG, "AudioRecord no inicializó")
                isRunning = false
                return
            }
            
            if (audioTrack?.state != AudioTrack.STATE_INITIALIZED) {
                Log.e(TAG, "AudioTrack no inicializó")
                audioRecord?.release()
                audioRecord = null
                isRunning = false
                return
            }

            audioRecord?.startRecording()
            audioTrack?.play()
            Log.i(TAG, "AudioRecord/Track iniciados correctamente")

            // Loop de procesamiento
            scope.launch {
                val input = FloatArray(bufferSize)
                val outL = FloatArray(bufferSize)
                val outR = FloatArray(bufferSize)
                
                while (isRunning) {
                    try {
                        val read = audioRecord?.read(input, 0, bufferSize, AudioRecord.READ_BLOCKING) ?: 0
                        
                        if (read > 0) {
                            // FIX: Pasar Float precision completa a nativa, sin conversión a Int
                            IvannaNativeLib.nativeRenderSpatialBlock(
                                input, outL, outR,
                                posX, posY, posZ,  // ← Float precision preservada
                                mu                  // ← Float precision preservada
                            )
                            
                            // Reinterleave L/R → stereo interleaved
                            val mixed = FloatArray(bufferSize * 2)
                            for (i in 0 until bufferSize) {
                                mixed[i * 2] = outL[i]
                                mixed[i * 2 + 1] = outR[i]
                            }
                            
                            audioTrack?.write(mixed, 0, mixed.size, AudioTrack.WRITE_BLOCKING)
                            
                            blocksProcessed++
                            
                            // Log estadísticas cada 5 segundos
                            val now = System.currentTimeMillis()
                            if (now - lastLogTime > 5000) {
                                val blockRate = blocksProcessed / ((now - lastLogTime) / 1000.0)
                                Log.d(TAG, "Processed $blocksProcessed blocks (%.1f blocks/s) | pos=(%.2f,%.2f,%.2f) mu=%.2f".format(
                                    blockRate, posX, posY, posZ, mu
                                ))
                                lastLogTime = now
                            }
                        } else if (read < 0) {
                            Log.w(TAG, "AudioRecord error: $read, saliendo del loop")
                            break
                        }
                    } catch (e: Exception) {
                        Log.e(TAG, "Error en loop de procesamiento: ${e.message}")
                        break
                    }
                }
                
                Log.i(TAG, "Loop de procesamiento finalizado. Total blocks: $blocksProcessed")
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error al iniciar SpatialAudioEngineV2: ${e.message}")
            isRunning = false
        }
    }

    /**
     * Detener el motor espacial y liberar recursos.
     */
    fun stop() {
        if (!isRunning) return
        
        Log.i(TAG, "Deteniendo SpatialAudioEngineV2...")
        isRunning = false
        
        try {
            audioRecord?.stop()
            audioRecord?.release()
            audioRecord = null
            
            audioTrack?.stop()
            audioTrack?.release()
            audioTrack = null
            
            IvannaNativeLib.nativeReleaseSpatialEngine()
            Log.i(TAG, "SpatialAudioEngineV2 detenido correctamente")
        } catch (e: Exception) {
            Log.e(TAG, "Error al detener: ${e.message}")
        }
        
        scope.cancel()
    }
}
