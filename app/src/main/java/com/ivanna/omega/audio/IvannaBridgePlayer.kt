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
 * ALCANCE DE ESTA PRIMERA VERSIÓN:
 *   Reproducción simple (play/pause/stop) de un archivo local por URI.
 *   Sin seek, sin cola de reproducción, sin notificación media-session
 *   todavía — eso se agrega encima de esta base, no se rediseña.
 */
class IvannaBridgePlayer(private val context: Context) {

    companion object {
        private const val TAG = "IVANNA.BridgePlayer"
        private const val TIMEOUT_US = 10_000L
    }

    enum class State { IDLE, PLAYING, PAUSED, STOPPED, ERROR }

    @Volatile var state: State = State.IDLE
        private set

    private val scope = CoroutineScope(Dispatchers.Default + SupervisorJob())
    private var job: Job? = null
    private var audioTrack: AudioTrack? = null

    @Volatile private var pauseRequested = false
    @Volatile private var stopRequested = false

    // FEATURE (Voice Protection): lazy para no cargar YamnetClassifier
    // (modelo TFLite) hasta la primera reproducción real.
    private val voiceProtection: VoiceProtectionController? by lazy {
        try { VoiceProtectionController(context) } catch (e: Exception) {
            Log.w(TAG, "Voice Protection no disponible: ${e.message}"); null
        }
    }

    /**
     * Control real (no cosmético) de Voice Protection — delega a
     * VoiceProtectionController.enabled, que ya gatea feed() de verdad
     * (ver VoiceProtectionController.kt: `if (!enabled) return` al inicio
     * de feed()). Antes era una propiedad `private`, sin forma de
     * controlarla desde la UI.
     */
    fun setVoiceProtectionEnabled(enabled: Boolean) {
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

    fun pause() { pauseRequested = true; state = State.PAUSED }

    fun resume() { pauseRequested = false; state = State.PLAYING }

    fun stop() {
        stopRequested = true
        job?.cancel()
        job = null
        releaseTrack()
        state = State.STOPPED
    }

    private fun releaseTrack() {
        // Desregistrar la sesion de AudioEffect antes de liberar
        runCatching {
            audioTrack?.audioSessionId?.let { sid ->
                (context.applicationContext as? com.ivanna.omega.core.IVANNAApplication)
                    ?.globalEffectManager?.closeSession(sid)
            }
        }
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
                Log.e(TAG, "Sin pista de audio en $uri"); state = State.ERROR; return@withContext
            }
            extractor.selectTrack(trackIndex)

            val sampleRate = format.getInteger(MediaFormat.KEY_SAMPLE_RATE)
            val channelCount = format.getInteger(MediaFormat.KEY_CHANNEL_COUNT)
            val mime = format.getString(MediaFormat.KEY_MIME)!!

            // Reinicializa el DSP nativo con el sample rate REAL del archivo.
            // Los filtros (biquads, HRTF, NHO) dependen de fs; usar siempre
            // 48000 fijo desalinearía las frecuencias de corte con archivos
            // a 44100/22050/etc.
            DSPBridge.init(sampleRate)

            // IvannaNpeEngine, en cambio, se inicializa UNA sola vez en
            // MainActivity.onCreate() a un sample rate fijo (no se puede
            // reinicializar por archivo sin destruir y recrear el handle
            // nativo — no implementado). Si el archivo actual no coincide,
            // sus filtros internos (calculados para la fs de init) sonarían
            // desafinados. Se salta el procesamiento en ese caso, en vez de
            // procesar mal en silencio — misma fs es el caso común (los
            // presets de referencia del proyecto son 48000).
            val npeSampleRateMismatch = IvannaNpeEngine.isReady && IvannaNpeEngine.sampleRate != sampleRate
            if (npeSampleRateMismatch) {
                Log.w(TAG, "NPE inicializado a ${IvannaNpeEngine.sampleRate}Hz, archivo es ${sampleRate}Hz — NPE desactivado para esta reproducción")
            }

            // FIX: DSPBridge.nativeProcess asume SIEMPRE 2ch intercalado
            // (L0,R0,L1,R1,...) — no soporta mono. Se fuerza salida estéreo
            // real y, si la fuente es mono, se upmixea (L=R) antes de
            // pasar por el DSP. Así no hay dos formatos de buffer
            // compitiendo con el mismo motor nativo.
            val channelMask = AudioFormat.CHANNEL_OUT_STEREO
            val minBuf = AudioTrack.getMinBufferSize(
                sampleRate, channelMask, AudioFormat.ENCODING_PCM_FLOAT)
            if (minBuf <= 0) {
                Log.e(TAG, "AudioTrack no soporta $sampleRate Hz"); state = State.ERROR; return@withContext
            }

            val track = AudioTrack.Builder()
                .setAudioAttributes(AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_MEDIA)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
                .setAudioFormat(AudioFormat.Builder()
                    .setEncoding(AudioFormat.ENCODING_PCM_FLOAT)
                    .setSampleRate(sampleRate)
                    .setChannelMask(channelMask).build())
                .setBufferSizeInBytes(minBuf * 4)
                .setTransferMode(AudioTrack.MODE_STREAM)
                .build()
            if (track.state != AudioTrack.STATE_INITIALIZED) {
                Log.e(TAG, "AudioTrack no inicializó"); state = State.ERROR; return@withContext
            }
            // FIX (controles desconectados): registrar la sesion del AudioTrack
            // en IvannaGlobalEffectManager para que adjustLiveParams() tenga
            // al menos una sesion real sobre la que aplicar EQ/Virtualizer/Comp.
            // Sin esto, activeSessions esta vacio y los sliders no afectan nada.
            runCatching {
                (context.applicationContext as? com.ivanna.omega.core.IVANNAApplication)
                    ?.globalEffectManager
                    ?.openSession(track.audioSessionId, context.packageName)
            }.onFailure { Log.w(TAG, "No se pudo registrar sesion AudioTrack: \${it.message}") }
            audioTrack = track

            codec = MediaCodec.createDecoderByType(mime)
            codec.configure(format, null, null, 0)
            codec.start()
            track.play()
            state = State.PLAYING

            val bufferInfo = MediaCodec.BufferInfo()
            var sawInputEOS = false
            var sawOutputEOS = false

            while (isActive && !sawOutputEOS && !stopRequested) {
                // Backpressure simple para pausa: no alimentamos ni drenamos
                // mientras esté en pausa, pero mantenemos el codec vivo.
                while (pauseRequested && !stopRequested) delay(50)
                if (stopRequested) break

                if (!sawInputEOS) {
                    val inIndex = codec.dequeueInputBuffer(TIMEOUT_US)
                    if (inIndex >= 0) {
                        val inBuf = codec.getInputBuffer(inIndex)!!
                        val sampleSize = extractor.readSampleData(inBuf, 0)
                        if (sampleSize < 0) {
                            codec.queueInputBuffer(inIndex, 0, 0, 0,
                                MediaCodec.BUFFER_FLAG_END_OF_STREAM)
                            sawInputEOS = true
                        } else {
                            codec.queueInputBuffer(inIndex, 0, sampleSize,
                                extractor.sampleTime, 0)
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
                        val floats = pcm16ToFloat(outBuf, bufferInfo.size)
                        val stereo = if (channelCount == 1) monoToStereo(floats) else floats

                        // El motor nativo clampea internamente a 2048 frames
                        // por llamada; no hay garantía de que el buffer de
                        // salida del decoder respete ese límite en todos los
                        // codecs/dispositivos, así que se trocea aquí para
                        // que nunca se escriba audio sin procesar.
                        // Buffers reutilizables para IvannaSpatialEngine
                        // (opera sobre canales separados, no intercalados —
                        // se declaran una vez fuera del loop para no
                        // reasignar memoria en cada chunk).
                        val spatialInL = FloatArray(2048)
                        val spatialInR = FloatArray(2048)
                        val spatialOutL = FloatArray(2048)
                        val spatialOutR = FloatArray(2048)

                        val totalFrames = stereo.size / 2
                        var offset = 0
                        while (offset < totalFrames) {
                            val chunkFrames = minOf(2048, totalFrames - offset)
                            val chunk = stereo.copyOfRange(offset * 2, (offset + chunkFrames) * 2)
                            // FEATURE (Voice Protection): clasifica el audio
                            // CRUDO (antes del DSP) — clasificar después
                            // sería sobre una señal ya coloreada por
                            // Exciter/EQ, menos fiel a lo que YAMNet espera.
                            voiceProtection?.feed(chunk, chunkFrames, sampleRate)
                            DSPBridge.process(chunk, chunkFrames)
                            // FIX (motor NPE nunca sonaba): IvannaNpeEngine
                            // (NHO+LIF+BiquadEnvelopeBank+AutonomousBrain,
                            // el motor detrás de los sliders de Inhibición
                            // Lateral/Compresión OHC/Master Gain/AGC/HRTF/
                            // Coclear/Adapt/Manifold en la UI) solo se
                            // llamaba desde PlaybackCaptureService, cuyo
                            // resultado se descarta siempre (esa ruta es
                            // captura+análisis del sistema, sin salida de
                            // audio real — ver auditoría). Este es el único
                            // lugar de toda la app que escribe a un
                            // AudioTrack real, así que es el único lugar
                            // donde procesar aquí tiene efecto audible.
                            // isReady evita el costo si el motor no
                            // inicializó (ver IvannaNpeEngine.init).
                            if (IvannaNpeEngine.isReady && !npeSampleRateMismatch) {
                                IvannaNpeEngine.processInterleavedStereo(chunk, chunkFrames)
                            }
                            // FIX (control sin efecto real): "modo concierto"
                            // por voz encendía parámetros de un motor que
                            // nadie ejecutaba nunca. Ahora sí se aplica, acá,
                            // el único lugar con salida de audio audible real.
                            if (com.ivanna.omega.dsp.ConcertMode.enabled) {
                                com.ivanna.omega.dsp.ConcertMode.shared.process(chunk)
                            }
                            // FIX (toggle conectado al motor equivocado —
                            // auditoría de cableado): "MOTOR BINAURAL · 32
                            // OBJETOS" en la UI describe textualmente a
                            // IvannaSpatialEngine (upmixer+VBAP+HRTF+head-
                            // tracking) pero estaba wireado a
                            // SpatialAudioEngineV2, que es solo telemetría
                            // (se deja para el HUD). Se aplica acá el motor
                            // real, sobre canales deinterleaved — este sí
                            // preserva la separación estéreo (dos fuentes
                            // virtuales L/R, no un downmix a mono).
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
                            track.write(chunk, 0, chunk.size, AudioTrack.WRITE_BLOCKING)
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

    /** Mono intercalado (M0,M1,...) → estéreo intercalado (M0,M0,M1,M1,...). */
    private fun monoToStereo(mono: FloatArray): FloatArray {
        val out = FloatArray(mono.size * 2)
        for (i in mono.indices) { out[2 * i] = mono[i]; out[2 * i + 1] = mono[i] }
        return out
    }

    /** PCM16 little-endian intercalado → Float [-1,1] intercalado (mismo layout que espera DSPBridge). */
    private fun pcm16ToFloat(buf: ByteBuffer, byteSize: Int): FloatArray {
        val bb = buf.duplicate().order(ByteOrder.LITTLE_ENDIAN)
        val sampleCount = byteSize / 2
        val out = FloatArray(sampleCount)
        for (i in 0 until sampleCount) {
            out[i] = bb.short.toFloat() / 32768f
        }
        return out
    }
}
