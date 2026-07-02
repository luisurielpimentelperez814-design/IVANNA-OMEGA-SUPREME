package com.ivanna.omega.audio

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.AudioTrack
import android.media.MediaRecorder
import android.os.Process
import android.util.Log
import com.ivanna.omega.dsp.DSPBridge
import com.ivanna.omega.dsp.DSPState
import com.ivanna.omega.ai.YamnetClassifier
import kotlinx.coroutines.*
import kotlin.math.sqrt

/**
 * AudioPipeline — Captura audio → YAMNet clasificación → DSP → reproducción.
 *
 * FIXES DE CONECTIVIDAD v2.0 (Unified Engine Integration):
 *   1. YAMNet classifier ahora inyecta scores directamente al PDEngine C++
 *      via nativeSetAntiDolbyScores() cada ~1s.
 *   2. Flujo: AudioRecord → downsample 48k→16k → YamnetClassifier.classify()
 *      → AudioEngine.nativeSetAntiDolbyScoresStatic(voice, music, bass, silence)
 *   3. Control plane (audio_control_plane.cpp) lee estos scores y los aplica
 *      como multiplicadores dinámicos en EQ/Widener/Spatial.
 *   4. No se bloquea audio thread — clasificación en coroutine async.
 */
class AudioPipeline(private val context: Context) {
    companion object {
        private const val TAG = "AudioPipeline"
        private const val SAMPLE_RATE = 48000
        private const val CHANNEL_CONFIG = AudioFormat.CHANNEL_IN_STEREO
        private const val AUDIO_FORMAT = AudioFormat.ENCODING_PCM_16BIT
        private const val BUFFER_SIZE_FRAMES = 480  // 10ms @ 48kHz
        private const val DOWNSAMPLE_RATE = 16000
        private const val DOWNSAMPLE_FACTOR = SAMPLE_RATE / DOWNSAMPLE_RATE  // 3
    }

    private var audioRecord: AudioRecord? = null
    private var audioTrack: AudioTrack? = null
    private var isRunning = false
    private var processingScope: CoroutineScope? = null

    // ── YAMNet clasificador ────────────────────────────────────────────
    private var yamnetClassifier: YamnetClassifier? = null
    private val yamnetScores = FloatArray(4)  // [voice, music, bass, silence]
    private var yamnetAccumulator = FloatArray(BUFFER_SIZE_FRAMES)
    private var yamnetAccumIndex = 0
    private val YAMNET_INTERVAL_SAMPLES = SAMPLE_RATE  // clasificar cada ~1s

    fun initialize() {
        try {
            val bufferSize = AudioRecord.getMinBufferSize(SAMPLE_RATE, CHANNEL_CONFIG, AUDIO_FORMAT)
            audioRecord = AudioRecord(
                MediaRecorder.AudioSource.DEFAULT,
                SAMPLE_RATE,
                CHANNEL_CONFIG,
                AUDIO_FORMAT,
                bufferSize * 2
            )

            audioTrack = AudioTrack(
                AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_MEDIA)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                    .build(),
                AudioFormat.Builder()
                    .setSampleRate(SAMPLE_RATE)
                    .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO)
                    .setEncoding(AUDIO_FORMAT)
                    .build(),
                bufferSize * 2,
                AudioTrack.MODE_STREAM,
                android.media.AudioManager.AUDIO_SESSION_ID_GENERATE
            )

            // El constructor de YamnetClassifier ya inicializa el intérprete TFLite
            // (ver com.ivanna.omega.ai.YamnetClassifier). No hay método initialize()
            // separado; si el modelo no está disponible entra en modo fallback.
            yamnetClassifier = YamnetClassifier(context)

            Log.i(TAG, "AudioPipeline inicializado: $SAMPLE_RATE Hz, buffer=$bufferSize")
        } catch (e: Exception) {
            Log.e(TAG, "Error inicializando AudioPipeline: ${e.message}")
        }
    }

    fun start() {
        if (isRunning) return
        isRunning = true
        audioRecord?.startRecording()
        audioTrack?.play()

        processingScope = CoroutineScope(Dispatchers.Default + Job())
        processingScope?.launch {
            processAudio()
        }
    }

    fun stop() {
        isRunning = false
        audioRecord?.stop()
        audioTrack?.stop()
        processingScope?.cancel()
        Log.i(TAG, "AudioPipeline detenido")
    }

    fun release() {
        stop()
        audioRecord?.release()
        audioTrack?.release()
        yamnetClassifier?.release()
        Log.i(TAG, "AudioPipeline liberado")
    }

    private suspend fun processAudio() {
        val buffer = ShortArray(BUFFER_SIZE_FRAMES * 2)  // stereo
        var totalSamplesProcessed = 0

        while (isRunning) {
            try {
                // ────────────────────────────────────────────────────────────────
                // 1. Captura audio vía AudioRecord
                // ────────────────────────────────────────────────────────────────
                val samplesRead = audioRecord?.read(buffer, 0, buffer.size) ?: 0
                if (samplesRead <= 0) continue

                // ────────────────────────────────────────────────────────────────
                // 2. Procesa DSP (DSPBridge.process) en tiempo real
                // ────────────────────────────────────────────────────────────────
                val floatBuffer = FloatArray(samplesRead)
                for (i in 0 until samplesRead) {
                    floatBuffer[i] = buffer[i].toFloat() / 32768.0f
                }
                // DSPBridge.process opera in-place sobre FloatArray normalizado a [-1,1]
                DSPBridge.process(floatBuffer, samplesRead / 2)
                val processedBuffer = ShortArray(samplesRead)
                for (i in 0 until samplesRead) {
                    processedBuffer[i] = (floatBuffer[i] * 32767.0f)
                        .coerceIn(-32768.0f, 32767.0f).toInt().toShort()
                }

                // ────────────────────────────────────────────────────────────────
                // 3. Acumula muestras para clasificación YAMNet (downsample)
                // ────────────────────────────────────────────────────────────────
                totalSamplesProcessed += samplesRead / 2  // mono count

                // Downsample 48kHz → 16kHz (tomar cada 3era muestra)
                var downsampled = 0
                for (i in 0 until samplesRead / 2) {
                    if (i % DOWNSAMPLE_FACTOR == 0) {
                        if (yamnetAccumIndex < yamnetAccumulator.size) {
                            yamnetAccumulator[yamnetAccumIndex++] = buffer[i * 2].toFloat() / 32768.0f
                        }
                        downsampled++
                    }
                }

                // Clasificar cada ~1s (YAMNET_INTERVAL_SAMPLES @ 48kHz)
                if (totalSamplesProcessed >= YAMNET_INTERVAL_SAMPLES) {
                    classifyWithYamnet()
                    totalSamplesProcessed = 0
                    yamnetAccumIndex = 0
                }

                // ────────────────────────────────────────────────────────────────
                // 4. Reproduce audio procesado
                // ────────────────────────────────────────────────────────────────
                audioTrack?.write(processedBuffer, 0, processedBuffer.size)

            } catch (e: Exception) {
                Log.e(TAG, "Error en processAudio: ${e.message}")
            }
        }
    }

    private suspend fun classifyWithYamnet() {
        try {
            // ────────────────────────────────────────────────────────────────
            // Clasifica el audio acumulado con YAMNet
            // ────────────────────────────────────────────────────────────────
            val downsampled = yamnetAccumulator.slice(0 until yamnetAccumIndex).toFloatArray()
            if (downsampled.isEmpty()) return

            // YamnetClassifier.classify(FloatArray) devuelve ClassificationResult
            // (speech, music, bass, isValid). Volcamos en yamnetScores para mantener
            // el bus interno de este pipeline.
            val result = yamnetClassifier?.classify(downsampled)
            val voice   = result?.speech ?: 0f
            val music   = result?.music  ?: 0f
            val bass    = result?.bass   ?: 0f
            // YAMNet no expone 'silence' directamente; se aproxima como
            // complemento de la energía de las tres clases informativas.
            val silence = (1f - (voice + music + bass)).coerceIn(0f, 1f)
            yamnetScores[0] = voice
            yamnetScores[1] = music
            yamnetScores[2] = bass
            yamnetScores[3] = silence

            Log.d(TAG, "YAMNet scores: voice=$voice music=$music bass=$bass silence=$silence")

            // ────────────────────────────────────────────────────────────────
            // CONEXIÓN CRÍTICA: Inyecta scores en el orquestador C++
            // ────────────────────────────────────────────────────────────────
            // Esto actualiza g_control_frame.yamnet_*_score (atómicos, lock-free)
            // que luego son leídos por control_apply_frame() en audio thread.
            AudioEngine.nativeSetAntiDolbyScoresStatic(voice, music, bass, silence)

        } catch (e: Exception) {
            Log.e(TAG, "Error en classifyWithYamnet: ${e.message}")
        }
    }
}
