package com.ivanna.omega.audio

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import android.media.AudioTimestamp
import android.media.MediaCodec
import android.media.MediaExtractor
import android.media.MediaFormat
import android.net.Uri
import android.util.Log
import com.ivanna.omega.ai.PerceptualState
import com.ivanna.omega.ai.PerceptualStateListener
import com.ivanna.omega.dsp.DSPBridge
import com.ivanna.omega.audio.AudioRoutingManager
import com.ivanna.omega.neuromorphic.IvannaNpeEngine
import com.ivanna.omega.neuromorphic.IvannaNpeNative
import com.ivanna.omega.audio.effects.NeuromorphicProcessingEngine
import com.ivanna.omega.audio.effects.VolterraH2Processor
import com.ivanna.omega.audio.VolterraSwitch
import com.ivanna.omega.core.IvannaNativeLib
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
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
// ── AUDIT FIX PR 1: IvannaBridgePlayer ahora recibe cambios de PerceptualCortex ──
class IvannaBridgePlayer(private val context: Context) : PerceptualStateListener {

    // FIX (issue 5 — conflicto de foco con Volterra/switches externos):
    // antes NO existía gestión de AudioFocus alguna para este reproductor
    // (AudioCallbackManager estaba definida pero jamás instanciada en todo
    // el proyecto). Ahora se solicita foco cooperativo al iniciar playback
    // y se PAUSA (no se destruye el track) en pérdida transitoria,
    // reanudando solo si el usuario no había pausado manualmente.
    private val focusManager by lazy {
        AudioCallbackManager(
            context.getSystemService(Context.AUDIO_SERVICE) as android.media.AudioManager
        ) { focusChange ->
            when (focusChange) {
                android.media.AudioManager.AUDIOFOCUS_LOSS,
                android.media.AudioManager.AUDIOFOCUS_LOSS_TRANSIENT -> {
                    if (state == State.PLAYING) {
                        pauseRequested = true
                        runCatching { audioTrack?.pause() }
                        state = State.PAUSED
                    }
                }
                android.media.AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK -> {
                    runCatching { audioTrack?.setVolume(0.3f) }
                }
                android.media.AudioManager.AUDIOFOCUS_GAIN -> {
                    runCatching { audioTrack?.setVolume(1.0f) }
                    if (state == State.PAUSED && pauseRequested) resume()
                }
            }
        }
    }

    companion object {
        private const val TAG = "IVANNA.BridgePlayer"
        private const val TIMEOUT_US = 10_000L
        private const val TARGET_SAMPLE_RATE = 96_000
        private const val MAX_CHUNK_FRAMES = 2048

        /** Última instancia activa — usada por PiLstmBridge para AGC seguro. */
        @Volatile var activeInstance: IvannaBridgePlayer? = null
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
    private val analyzeTickCounter = java.util.concurrent.atomic.AtomicInteger(0)

    // --- Posición y duración reales para la barra de progreso ---
    private val _currentPositionMs = MutableStateFlow(0L)
    val currentPositionMs: StateFlow<Long> = _currentPositionMs.asStateFlow()

    private val _durationMs = MutableStateFlow(0L)
    val durationMs: StateFlow<Long> = _durationMs.asStateFlow()

    // seekTarget: -1 = sin seek pendiente. seekTo() escribe aquí; el loop lo consume.
    @Volatile private var seekTargetUs = -1L

    // ── PARCHE 1: Puente de métricas agregadas ───────────────────────────────
    private val _omegaMetrics = MutableStateFlow(OmegaMetrics())
    val omegaMetrics: StateFlow<OmegaMetrics> = _omegaMetrics.asStateFlow()


    private fun estimateHardwareLatencyMs(): Float {
        return try {
            val track = audioTrack ?: return 0f

            val timestamp = AudioTimestamp()

            if (track.getTimestamp(timestamp)) {
                val nowNs = System.nanoTime()

                val writtenFrames =
                    timestamp.framePosition

                val frameTimeNs =
                    (writtenFrames * 1_000_000_000L) /
                    AudioPipeline.SAMPLE_RATE

                ((nowNs - frameTimeNs) / 1_000_000f)
                    .coerceAtLeast(0f)
            } else {
                0f
            }
        } catch (_: Throwable) {
            0f
        }
    }

    private fun pollOmegaMetrics() {
        try {
            val pipelineState = IvannaUnifiedPipeline.state.value

            val dspActive = IvannaNativeLib.isLoaded &&
                try { IvannaNativeLib.nativeIsAdaptiveEngineRunning() } catch (_: Throwable) { false }

            val hrtfActive = try {
                IvannaNativeLib.isLoaded &&
                IvannaNativeLib.nativeGetSpatialState().contains("HRTF_ON")
            } catch (_: Throwable) { false }

            val clipCount = try {
                if (IvannaNativeLib.isLoaded) IvannaNativeLib.nativeGetClipCount() else 0
            } catch (_: Throwable) { 0 }

            val cpuPercent = try {
                val telemetry = IvannaNativeLib.nativeGetAdaptiveTelemetry()
                if (telemetry != null && telemetry.size > 3) telemetry[3] * 100f else 0f
            } catch (_: Throwable) { 0f }

            val yamnetCategory = try {
                if (IvannaNpeEngine.isReady) IvannaNpeNative.nativeGetDetectedGenre() else "—"
            } catch (_: Throwable) { "—" }

            val yamnetConfidence = try {
                if (IvannaNpeEngine.isReady) {
                    val sig = IvannaNpeNative.nativeGetSynthClassify()
                    if (sig.isNotEmpty()) sig[0] else 0f
                } else 0f
            } catch (_: Throwable) { 0f }

            _omegaMetrics.value = OmegaMetrics(
                rmsLevel         = pipelineState.rms,
                peakLevel        = pipelineState.peak,
                clipCount        = clipCount,
                cpuPercent       = cpuPercent,
                latencyMs        = estimateHardwareLatencyMs(),
                sampleRate       = AudioPipeline.SAMPLE_RATE,
                yamnetCategory   = yamnetCategory,
                yamnetConfidence = yamnetConfidence,
                dspActive        = dspActive,
                hrtfActive       = hrtfActive,
                spatialWidth     = pipelineState.spatialWidth
            )
        } catch (t: Throwable) {
            android.util.Log.w("IVANNA.BridgePlayer", "pollOmegaMetrics: ${t.message}")
        }
    }
    // ── FIN PARCHE 1 ─────────────────────────────────────────────────────────

    // ── Motor Neuromórfico Kotlin (ESN puro, sin JNI) ────────────────────────
    // Inicializado lazy para no pagar el coste de la matriz de pesos
    // (O(N²) = 64² floats) hasta que se active por primera vez.
    @Volatile var npeKotlinEnabled: Boolean = false
    @Volatile var volterraEnabled: Boolean = false
    private val npeKotlin: NeuromorphicProcessingEngine by lazy {
        NeuromorphicProcessingEngine(
            neuronCount       = 64,
            spectralRadius    = 0.9f,
            inputScaling      = 0.4f,
            leakRate          = 0.08f,
            threshold         = 1.0f,
            outputScaling     = 0.25f,
            plasticityRate    = 0.003f,
            homeostasisRate   = 0.001f,
            resonanceBankSize = 8
        )
    }
    private val volterraProcessor: VolterraH2Processor by lazy { VolterraH2Processor() }

    /** Procesa un bloque PCM estéreo intercalado a través del NPE Kotlin.
     *  Llamado desde PlaybackCaptureService para aplicar el mismo motor
     *  neuromórfico al audio capturado de apps externas. */
    fun processBlockThroughNpeKotlin(buffer: FloatArray): FloatArray =
        npeKotlin.process(buffer)
    fun updateNpeKotlinParams(
        spectralRadius: Float  = 0.9f,
        inputScaling:   Float  = 0.4f,
        outputScaling:  Float  = 0.25f,
        plasticityRate: Float  = 0.003f
    ) {
        if (npeKotlinEnabled) {
            npeKotlin.updateParameters(
                spectralRadius = spectralRadius,
                inputScaling   = inputScaling,
                outputScaling  = outputScaling,
                plasticityRate = plasticityRate
            )
        }
    }

    // FIX (reproducción consecutiva): cola real. play() sigue soportando
    // un solo Uri (comportamiento previo intacto); playQueue() agrega
    // avance automático real cuando el track termina por EOS natural
    // (no cuando el usuario para manualmente con stop()).
    private val queue = mutableListOf<Uri>()
    private var queueIndex = -1
    var onQueueAdvance: ((Uri) -> Unit)? = null

    fun playQueue(uris: List<Uri>, startIndex: Int = 0) {
        queue.clear(); queue.addAll(uris)
        queueIndex = startIndex.coerceIn(0, queue.lastIndex)
        if (queue.isNotEmpty()) play(queue[queueIndex])
    }

    private fun advanceQueueIfAny() {
        if (queueIndex < 0 || queueIndex >= queue.lastIndex) return
        queueIndex++
        val next = queue[queueIndex]
        onQueueAdvance?.invoke(next)
        play(next)
    }

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
        activeInstance = this
        stop()
        stopRequested = false
        pauseRequested = false
        focusManager.requestAudioFocus() // FIX: foco cooperativo antes de sonar
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

    /**
     * Solicita un salto a [positionMs] milisegundos.
     * El decode loop lo consume en la próxima iteración:
     *   - llama extractor.seekTo() en modo SEEK_TO_PREVIOUS_SYNC
     *   - vacía el codec (flush + restart)
     *   - actualiza currentPositionMs
     * Si el reproductor estaba en PAUSED sigue en PAUSED (el caller de UI
     * puede llamar resume() si lo desea tras el seek).
     */
    fun seekTo(positionMs: Long) {
        val clamped = positionMs.coerceIn(0L, _durationMs.value)
        _currentPositionMs.value = clamped
        seekTargetUs = clamped * 1_000L
    }

    fun stop() {
        stopRequested = true
        pauseRequested = false
        job?.cancel()
        job = null
        releaseTrack()
        focusManager.abandonAudioFocus() // FIX: libera el foco al parar de verdad
        state = State.STOPPED
    }

    fun release() {
        if (activeInstance === this) activeInstance = null
        stop()
        runCatching { voiceProtection?.release() }
        scope.cancel()
    }

    // ── AUDIT FIX PR 1: Implementación de PerceptualStateListener ──────────────
    /**
     * Recibir cambios de estado perceptual de PerceptualCortex.
     * Se aplican dinámicamente durante la reproducción.
     *
     * @param state Nuevo PerceptualState calculado por la corteza perceptual
     * @param deltaMs Tiempo desde última actualización
     */
    override fun onPerceptualStateChanged(state: PerceptualState, deltaMs: Long) {
        // Aplicar dinámicamente los parámetros DSP calculados
        // El DSPBridge ya recibe estos parámetros via el pipeline normal,
        // pero aquí podemos hacer ajustes específicos al bridge player:
        //
        // 1. Ajustar volumen basado en adaptación perceptual
        // 2. Cambiar buffering si hay fatiga auditiva
        // 3. Modular la ganancia del exciter
        // 4. Aplicar EQ dinámico

        try {
            val dspControl = state.dsp
            Log.d("IvannaBridgePlayer", 
                "Perceptual update: gain=${dspControl.gain}, " +
                "compressor=${dspControl.compressor}, " +
                "exciter=${dspControl.exciter}, " +
                "spatial=${dspControl.spatial}")

            // Enviar parámetros al DSPBridge para que se apliquen al stream en vivo
            if (isPlaying()) {
                DSPBridge.applyPerceptualGain(dspControl.gain)
                DSPBridge.applyCompressorAmount(dspControl.compressor)
                DSPBridge.applyExciterReduction(dspControl.exciter)
                DSPBridge.applySpatialWidth(dspControl.spatial)
            }
        } catch (e: Exception) {
            Log.e("IvannaBridgePlayer", "Error aplicando estado perceptual: ${e.message}")
        }
    }

    private fun isPlaying(): Boolean = state == State.PLAYING

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

            // Duración real del archivo (µs → ms). KEY_DURATION puede no existir en
            // streams sin cabecera; si es así se deja en 0 (barra indeterminada en UI).
            val rawDurationUs = if (format.containsKey(MediaFormat.KEY_DURATION))
                format.getLong(MediaFormat.KEY_DURATION) else 0L
            _durationMs.value = rawDurationUs / 1_000L
            _currentPositionMs.value = 0L
            seekTargetUs = -1L

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

                // --- Seek pendiente ---
                val pendingSeekUs = seekTargetUs
                if (pendingSeekUs >= 0L) {
                    seekTargetUs = -1L
                    sawInputEOS = false
                    extractor.seekTo(pendingSeekUs, MediaExtractor.SEEK_TO_PREVIOUS_SYNC)
                    codec.flush()
                    if (npeKotlinEnabled) npeKotlin.reset()
                }

                if (!sawInputEOS) {
                    val inIndex = codec.dequeueInputBuffer(TIMEOUT_US)
                    if (inIndex >= 0) {
                        val inBuf = codec.getInputBuffer(inIndex)!!
                        val sampleSize = extractor.readSampleData(inBuf, 0)
                        if (sampleSize < 0) {
                            codec.queueInputBuffer(inIndex, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
                            sawInputEOS = true
                        } else {
                            val sampleTimeUs = extractor.sampleTime
                            codec.queueInputBuffer(inIndex, 0, sampleSize, sampleTimeUs, 0)
                            _currentPositionMs.value = sampleTimeUs / 1_000L
                            // Poll de métricas cada ~500ms (sin alloc, sin bloqueo)
                            if (sampleTimeUs % 500_000L < 30_000L) pollOmegaMetrics()
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
                            if (com.ivanna.omega.core.IvannaNativeLib.isLoaded &&
                                analyzeTickCounter.incrementAndGet() % 50 == 0) {
                                runCatching { com.ivanna.omega.core.IvannaNativeLib.nativeAnalyzeAudio(chunk) }
                            }
                            if (IvannaNpeEngine.isReady && !npeSampleRateMismatch) {
                                try { IvannaNpeEngine.processInterleavedStereo(chunk, chunkFrames) } catch (e: Exception) { android.util.Log.e("IVANNA_DSP", "Crash NPE evitado: ${e.message}") }
                            }
                            // ── NPE Kotlin (ESN puro) ────────────────────
                            // Corre en paralelo con el NPE nativo — añade
                            // textura armónica no lineal desde el reservorio
                            // de neuronas LIF/resonador/bursting/adaptativo.
                            if (npeKotlinEnabled) {
                                try {
                                    val npeIn = chunk.copyOf()
                                    val npeOut = npeKotlin.process(npeIn)
                                    npeOut.copyInto(chunk)
                                } catch (e: Exception) {
                                    android.util.Log.e("IVANNA_DSP", "Crash NPE-Kotlin evitado: ${e.message}")
                                }
                            }

                            // Volterra H2 (distorsión armónica) — FIX: antes vivía anidado
                            // dentro de "if (npeKotlinEnabled)" por una llave mal cerrada,
                            // así que si el usuario activaba Volterra pero NO el NPE-Kotlin,
                            // el efecto nunca se ejecutaba a pesar del switch encendido.
                            // Ahora es independiente, como indica VolterraSwitch.enabled.
                            if (VolterraSwitch.enabled) {
                                try {
                                    val volterraOut = volterraProcessor.process(chunk)
                                    volterraOut.copyInto(chunk)
                                } catch (e: Exception) {
                                    android.util.Log.e("IVANNA_DSP", "Crash Volterra evitado: ${e.message}")
                                }
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
            // FIX: solo avanzar la cola si terminó por EOS natural, no si
            // el usuario llamó stop() manualmente (stopRequested=true).
            if (!stopRequested) advanceQueueIfAny()
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
