package com.ivanna.omega.audio

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import android.media.MediaCodec
import android.media.MediaExtractor
import android.media.MediaFormat
import android.net.Uri
import android.util.Log
import com.ivanna.omega.dsp.DSPBridge
import com.ivanna.omega.neuromorphic.IvannaNpeEngine
import kotlinx.coroutines.*
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * IvannaBridgePlayer — reproductor propio de la app.
 *
 * POR QUÉ EXISTE ESTE ARCHIVO:
 *   Hasta ahora el pipeline nativo completo (DSPBridge → ParametricEQ →
 *   Compressor → HarmonicExciter → StereoWidener → PDEngine/NHO/Spatial →
 *   Kernel Evolutivo) solo tenía un camino real hacia el altavoz:
 *   AudioPipeline (loop mic → DSP → altavoz, pensado para "escuchar el
 *   mundo" en vivo, no para reproducir música).
 *
 *   Para apps de terceros (Spotify, YouTube, etc.) IvannaGlobalEffectManager
 *   solo puede enganchar efectos de stock de Android (Equalizer, BassBoost,
 *   Virtualizer, LoudnessEnhancer, DynamicsProcessing) — Android no permite
 *   inyectar DSP custom en el proceso de audio de otra app sin root. Todo
 *   el trabajo fino en C++ (NHO, HRTF, Kernel Evolutivo) queda invisible
 *   para el usuario que solo escucha Spotify.
 *
 *   IvannaBridgePlayer decodifica archivos locales (MediaExtractor +
 *   MediaCodec → PCM), pasa cada bloque por DSPBridge.process() —el mismo
 *   camino ya afinado y con los fixes de PDEngine/Kernel Evolutivo— y
 *   escribe el resultado a un AudioTrack propio. Es la única forma real
 *   de que todo el motor suene con música de verdad, no con el micrófono.
 *
 * FIXES v3.6:
 *   - pausa/reanudación reales en AudioTrack (sin seguir drenando buffer)
 *   - soporte correcto para salida PCM_FLOAT y PCM_16BIT del decoder
 *   - buffers reutilizables para evitar allocs por chunk
 *   - release() explícito del clasificador de Voice Protection
 */
class IvannaBridgePlayer(private val context: Context) {

    companion object {
        private const val TAG = "IVANNA.BridgePlayer"
        private const val TIMEOUT_US = 10_000L
        private const val TARGET_SAMPLE_RATE = 96_000
        private const val MAX_CHUNK_FRAMES = 2048
    }

    enum class State { IDLE, PLAYING, PAUSED, STOPPED, ERROR }

    @Volatile var state: State = State.IDLE
        private set

    private val scope = CoroutineScope(Dispatchers.Default + SupervisorJob())
    private var job: Job? = null
    private var audioTrack: AudioTrack? = null
    @Volatile private var voiceProtectionEnabled = true

    @Volatile private var pauseRequested = false
    @Volatile private var stopRequested = false

    private val voiceProtection: VoiceProtectionController? by lazy {
        try {
            VoiceProtectionController(context).also { it.enabled = voiceProtectionEnabled }
        } catch (e: Exception) {
            Log.w(TAG, "Voice Protection no disponible: ${e.message}")
            null
        }
    }

    fun setVoiceProtectionEnabled(enabled: Boolean) {
        voiceProtectionEnabled = enabled
        voiceProtection?.enabled = enabled
        if (!enabled) DSPBridge.setVoiceProtectScore(0f)
    }

    /** Reproduce el archivo en [uri]. Cancela cualquier reproducción previa. */
    fun play(uri: Uri) {
        stop()
        stopRequested = false
        pauseRequested = false
        job = scope.launch { runDecodeLoop(uri) }
    }

    fun pause() {
        pauseRequested = true
        runCatching { audioTrack?.pause() }
        state = State.PAUSED
    }

    fun resume() {
        pauseRequested = false
        runCatching { audioTrack?.play() }
        state = State.PLAYING
    }

    fun stop() {
        stopRequested = true
        pauseRequested = false
        job?.cancel()
        job = null
        releaseTrack()
        state = State.STOPPED
    }

    fun release() {
        stop()
        runCatching { voiceProtection?.release() }
        scope.cancel()
    }

    private fun releaseTrack() {
        // FIX: ya no se abre sesión de efectos stock para el propio track
        // (ver comentario en runDecodeLoop) — no hay nada que cerrar aquí.
        try { audioTrack?.pause() } catch (_: Throwable) {}
        try { audioTrack?.flush() } catch (_: Throwable) {}
        try { audioTrack?.stop() } catch (_: Throwable) {}
        try { audioTrack?.release() } catch (_: Throwable) {}
        audioTrack = null
    }

    private suspend fun runDecodeLoop(uri: Uri) = withContext(Dispatchers.Default) {
        val extractor = MediaExtractor()
        var codec: MediaCodec? = null
        try {
            extractor.setDataSource(context, uri, null)

            var trackIndex = -1
            var format: MediaFormat? = null
            for (i in 0 until extractor.trackCount) {
                val f = extractor.getTrackFormat(i)
                val mime = f.getString(MediaFormat.KEY_MIME) ?: continue
                if (mime.startsWith("audio/")) { trackIndex = i; format = f; break }
            }
            if (trackIndex < 0 || format == null) {
                Log.e(TAG, "Sin pista de audio en $uri")
                state = State.ERROR
                return@withContext
            }
            extractor.selectTrack(trackIndex)

            val inputSampleRate = format.getInteger(MediaFormat.KEY_SAMPLE_RATE)
            val sampleRate = TARGET_SAMPLE_RATE
            val channelCount = format.getInteger(MediaFormat.KEY_CHANNEL_COUNT)
            val mime = format.getString(MediaFormat.KEY_MIME)!!

            val resampler = StereoAudioResampler(TARGET_SAMPLE_RATE)
            resampler.setInputSampleRate(inputSampleRate)
            DSPBridge.init(sampleRate)

            val npeSampleRateMismatch = IvannaNpeEngine.isReady && IvannaNpeEngine.sampleRate != sampleRate
            if (npeSampleRateMismatch) {
                Log.w(TAG, "NPE inicializado a ${IvannaNpeEngine.sampleRate}Hz, salida bridge=$sampleRate Hz — NPE desactivado para esta reproducción")
            }

            val channelMask = AudioFormat.CHANNEL_OUT_STEREO
            val minBuf = AudioTrack.getMinBufferSize(
                sampleRate, channelMask, AudioFormat.ENCODING_PCM_FLOAT
            )
            if (minBuf <= 0) {
                Log.e(TAG, "AudioTrack no soporta $sampleRate Hz")
                state = State.ERROR
                return@withContext
            }

            val track = AudioTrack.Builder()
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
                        .setChannelMask(channelMask)
                        .build()
                )
                .setBufferSizeInBytes(minBuf * 4)
                .setPerformanceMode(AudioTrack.PERFORMANCE_MODE_LOW_LATENCY)
                .setTransferMode(AudioTrack.MODE_STREAM)
                .build()
            if (track.state != AudioTrack.STATE_INITIALIZED) {
                Log.e(TAG, "AudioTrack no inicializó")
                state = State.ERROR
                return@withContext
            }

            // FIX (unificación Vía A / Vía B, distorsión + Peak>1.33): esta
            // sesión YA pasó por la cadena nativa completa (DSPBridge +
            // NPE + Spatial) en el bucle de abajo. Antes también se le
            // enganchaban los efectos STOCK de Android (Equalizer,
            // BassBoost, Virtualizer, LoudnessEnhancer, DynamicsProcessing)
            // vía globalEffectManager sobre el MISMO audioSessionId —
            // doble procesamiento (software + plataforma) sumando ganancia
            // dos veces. globalEffectManager queda reservado EXCLUSIVAMENTE
            // para sesiones de apps de terceros (Spotify/YouTube, Ruta B),
            // que no pasan por DSPBridge y sí necesitan esos efectos.
            audioTrack = track

            codec = MediaCodec.createDecoderByType(mime)
            codec.configure(format, null, null, 0)
            codec.start()
            track.play()
            state = State.PLAYING

            val bufferInfo = MediaCodec.BufferInfo()
            var sawInputEOS = false
            var sawOutputEOS = false

            val spatialInL = FloatArray(MAX_CHUNK_FRAMES)
            val spatialInR = FloatArray(MAX_CHUNK_FRAMES)
            val spatialOutL = FloatArray(MAX_CHUNK_FRAMES)
            val spatialOutR = FloatArray(MAX_CHUNK_FRAMES)

            while (isActive && !sawOutputEOS && !stopRequested) {
                while (pauseRequested && !stopRequested) delay(50)
                if (stopRequested) break

                if (!sawInputEOS) {
                    val inIndex = codec.dequeueInputBuffer(TIMEOUT_US)
                    if (inIndex >= 0) {
                        val inBuf = codec.getInputBuffer(inIndex)!!
                        val sampleSize = extractor.readSampleData(inBuf, 0)
                        if (sampleSize < 0) {
                            codec.queueInputBuffer(inIndex, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
                            sawInputEOS = true
                        } else {
                            codec.queueInputBuffer(inIndex, 0, sampleSize, extractor.sampleTime, 0)
                            extractor.advance()
                        }
                    }
                }

                val outIndex = codec.dequeueOutputBuffer(bufferInfo, TIMEOUT_US)
                if (outIndex >= 0) {
                    val outBuf = codec.getOutputBuffer(outIndex)!!
                    if (bufferInfo.size > 0) {
                        outBuf.position(bufferInfo.offset)
                        outBuf.limit(bufferInfo.offset + bufferInfo.size)
                        val floats = pcmToFloat(
                            outBuf,
                            bufferInfo.size,
                            codec.outputFormat.getIntegerOrDefault(MediaFormat.KEY_PCM_ENCODING, AudioFormat.ENCODING_PCM_16BIT)
                        )
                        val stereo = if (channelCount == 1) monoToStereo(floats) else floats
                        val totalFrames = stereo.size / 2
                        var offset = 0
                        while (offset < totalFrames) {
                            val chunkFrames = minOf(MAX_CHUNK_FRAMES, totalFrames - offset)
                            val chunk = stereo.copyOfRange(offset * 2, (offset + chunkFrames) * 2)
                            voiceProtection?.feed(chunk, chunkFrames, sampleRate)
                            DSPBridge.process(chunk, chunkFrames)
                            if (IvannaNpeEngine.isReady && !npeSampleRateMismatch) {
                                try { IvannaNpeEngine.processInterleavedStereo(chunk, chunkFrames) } catch (e: Exception) { android.util.Log.e("IVANNA_DSP", "Crash NPE evitado: ${e.message}") }
                            }
                            if (com.ivanna.omega.dsp.ConcertMode.enabled) {
                                com.ivanna.omega.dsp.ConcertMode.shared.process(chunk)
                            }
                            if (com.ivanna.omega.spatial.IvannaSpatialEngine.enabled) {
                                for (i in 0 until chunkFrames) {
                                    spatialInL[i] = chunk[i * 2]
                                    spatialInR[i] = chunk[i * 2 + 1]
                                }
                                com.ivanna.omega.spatial.IvannaSpatialEngine.shared.processStereoInput(
                                    spatialInL, spatialInR, spatialOutL, spatialOutR, chunkFrames
                                )
                                for (i in 0 until chunkFrames) {
                                    chunk[i * 2] = spatialOutL[i]
                                    chunk[i * 2 + 1] = spatialOutR[i]
                                }
                            }
                            val outputChunk = resampler.process(chunk)
                            track.write(outputChunk, 0, outputChunk.size, AudioTrack.WRITE_BLOCKING)
                            offset += chunkFrames
                        }
                    }
                    codec.releaseOutputBuffer(outIndex, false)
                    if ((bufferInfo.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) {
                        sawOutputEOS = true
                    }
                }
            }
            state = State.STOPPED
        } catch (t: Throwable) {
            Log.e(TAG, "Error en decode loop", t)
            state = State.ERROR
        } finally {
            try { codec?.stop() } catch (_: Throwable) {}
            try { codec?.release() } catch (_: Throwable) {}
            extractor.release()
            releaseTrack()
        }
    }

    private fun monoToStereo(mono: FloatArray): FloatArray {
        val out = FloatArray(mono.size * 2)
        for (i in mono.indices) {
            out[2 * i] = mono[i]
            out[2 * i + 1] = mono[i]
        }
        return out
    }

    private fun pcmToFloat(buf: ByteBuffer, byteSize: Int, encoding: Int): FloatArray {
        return when (encoding) {
            AudioFormat.ENCODING_PCM_FLOAT -> pcmFloatToFloat(buf, byteSize)
            else -> pcm16ToFloat(buf, byteSize)
        }
    }

    private fun pcm16ToFloat(buf: ByteBuffer, byteSize: Int): FloatArray {
        val bb = buf.duplicate().order(ByteOrder.LITTLE_ENDIAN)
        val sampleCount = byteSize / 2
        val out = FloatArray(sampleCount)
        for (i in 0 until sampleCount) {
            out[i] = bb.short.toFloat() / 32768f
        }
        return out
    }

    private fun pcmFloatToFloat(buf: ByteBuffer, byteSize: Int): FloatArray {
        val bb = buf.duplicate().order(ByteOrder.LITTLE_ENDIAN)
        val sampleCount = byteSize / 4
        val out = FloatArray(sampleCount)
        for (i in 0 until sampleCount) {
            out[i] = bb.float.coerceIn(-1f, 1f)
        }
        return out
    }

    private fun MediaFormat.getIntegerOrDefault(key: String, defaultValue: Int): Int {
        return if (containsKey(key)) getInteger(key) else defaultValue
    }
}
