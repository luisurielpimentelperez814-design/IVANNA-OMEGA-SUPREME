package com.ivanna.omega.audio

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.media.AudioFormat
import android.media.AudioTrack
import android.media.AudioTimestamp
import android.media.MediaCodec
import android.media.MediaExtractor
import android.media.MediaFormat
import android.net.Uri
import android.os.SystemClock
import android.util.Log
import com.ivanna.omega.ai.PerceptualState
import com.ivanna.omega.ai.PerceptualStateListener
import com.ivanna.omega.dsp.DSPBridge
import com.ivanna.omega.dsp.AdaptiveDSPState
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
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
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
    private data class OutputRoute(val sampleRate: Int, val preferredDevice: AudioDeviceInfo?)

    /**
     * Chooses the app-owned AudioTrack output clock. USB keeps priority; A2DP uses
     * the selected device's negotiated codec rate when Android exposes it. This
     * does not control the remote sink clock or the Qualcomm vendor HAL.
     */
    private fun selectOutputRoute(): OutputRoute {
        val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
        val outputs = runCatching {
            audioManager.getDevices(AudioManager.GET_DEVICES_OUTPUTS)
        }.getOrNull().orEmpty()

        val usb = outputs.firstOrNull {
            it.type == AudioDeviceInfo.TYPE_USB_DEVICE ||
                it.type == AudioDeviceInfo.TYPE_USB_HEADSET ||
                it.type == AudioDeviceInfo.TYPE_USB_ACCESSORY
        }
        if (usb != null) {
            val rates = usb.sampleRates
            val advertised = rates.filter { it in 44_100..96_000 }.distinct().sortedDescending()
            val rate = advertised.firstOrNull() ?: USB_FALLBACK_SAMPLE_RATE
            Log.i(TAG, "USB audio output selected: ${usb.productName}; rate=${rate} Hz; advertised=${rates.contentToString()}")
            return OutputRoute(rate, usb)
        }

        val bluetooth = outputs.firstOrNull {
            it.type == AudioDeviceInfo.TYPE_BLUETOOTH_A2DP ||
                it.type == AudioDeviceInfo.TYPE_BLE_HEADSET ||
                it.type == AudioDeviceInfo.TYPE_BLE_SPEAKER
        }
        if (bluetooth != null) {
            // Never fall back to the first connected headset when Android does not expose
            // a stable address for this output; that could apply another device's codec rate.
            val codec = if (
                bluetooth.type == AudioDeviceInfo.TYPE_BLUETOOTH_A2DP &&
                BluetoothOutputRatePolicy.hasReliableDeviceAddress(bluetooth.address)
            ) {
                runCatching { BluetoothCodecDetector.detectActiveCodec(context, bluetooth.address) }.getOrNull()
            } else null
            val codecRate = codec?.sampleRateHz ?: 0
            val candidateRate = BluetoothOutputRatePolicy.select(codecRate)
            val candidateMinBuffer = AudioTrack.getMinBufferSize(
                candidateRate, AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_FLOAT
            )
            val rate = if (candidateMinBuffer > 0) candidateRate else BluetoothOutputRatePolicy.FALLBACK_RATE_HZ
            Log.i(TAG, "Bluetooth output selected: ${bluetooth.productName}; codec=${codec?.codec ?: "BLE/unknown"}; negotiatedRate=${codecRate}Hz; trackRate=${rate}Hz")
            return OutputRoute(rate, bluetooth)
        }
        return OutputRoute(TARGET_SAMPLE_RATE, null)
    }

    private val focusManager by lazy {
        AudioCallbackManager(
            context.getSystemService(Context.AUDIO_SERVICE) as android.media.AudioManager
        ) { focusChange ->
            when (focusChange) {
                android.media.AudioManager.AUDIOFOCUS_LOSS,
                android.media.AudioManager.AUDIOFOCUS_LOSS_TRANSIENT -> {
                    if (state == State.PLAYING) {
                        pauseRequested = true
                        // FIX (tronido al cambiar de ventana): pause() en seco y
                        // setVolume escalonado cortaban la onda en medio de un
                        // ciclo. Fade corto a 0 antes de pausar.
                        fadeVolume(0f, 40) { runCatching { audioTrack?.pause() } }
                        state = State.PAUSED
                    }
                }
                android.media.AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK -> {
                    fadeVolume(0.3f, 80)
                }
                android.media.AudioManager.AUDIOFOCUS_GAIN -> {
                    if (state == State.PAUSED && pauseRequested) {
                        runCatching { audioTrack?.setVolume(0f) }
                        resume()
                    }
                    fadeVolume(1.0f, 80)
                }
            }
        }
    }

    // Rampa lineal de volumen del AudioTrack (evita escalones = clicks).
    @Volatile private var fadeToken = 0
    private fun fadeVolume(target: Float, ms: Int, onDone: () -> Unit = {}) {
        val token = ++fadeToken
        val track = audioTrack ?: run { onDone(); return }
        Thread {
            val steps = (ms / 5).coerceAtLeast(1)
            var from = 1.0f
            // Volumen inicial real no legible vía API: se asume el último destino.
            from = lastVolume
            for (i in 1..steps) {
                if (token != fadeToken) return@Thread
                val v = from + (target - from) * i / steps
                runCatching { track.setVolume(v) }
                try { Thread.sleep(5) } catch (_: InterruptedException) { return@Thread }
            }
            lastVolume = target
            onDone()
        }.start()
    }
    @Volatile private var lastVolume = 1.0f

    companion object {
        private const val TAG = "IVANNA.BridgePlayer"
        private const val TIMEOUT_US = 10_000L
        private const val TARGET_SAMPLE_RATE = 96_000
        private const val USB_FALLBACK_SAMPLE_RATE = 48_000
        private const val MAX_CHUNK_FRAMES = 2048

        /** Última instancia activa — usada por PiLstmBridge para AGC seguro. */
        @Volatile var activeInstance: IvannaBridgePlayer? = null
    }

    enum class State { IDLE, PLAYING, PAUSED, STOPPED, ERROR }

    // ── Estado explícito del ciclo de vida de MediaCodec / AudioTrack ──
    // Independiente de State (estado lógico hacia la UI). CodecState
    // refleja qué llamadas son LEGALES sobre los recursos nativos:
    //   IDLE      → no hay recursos asignados
    //   CONFIGURED→ codec.configure() ok, aún sin start()
    //   RUNNING   → codec.start() + track.play() en curso
    //   STOPPED   → codec ya arrancó y ahora está detenido
    // codec.stop() sólo se invoca desde RUNNING; codec.start() sólo desde
    // CONFIGURED/STOPPED. Toda transición va bajo codecMutex — elimina el
    // IllegalStateException clásico al llamar play()/stop() repetido o
    // fuera de estado válido.
    private enum class CodecState { IDLE, CONFIGURED, RUNNING, STOPPED }

    @Volatile
    private var codecState: CodecState = CodecState.IDLE

    // Serializa todas las transiciones de codecState: play()/stop()/
    // release() concurrentes no pueden solapar un codec.start() con un
    // codec.stop()/release() de otro hilo.
    private val codecMutex = Mutex()

    @Volatile var state: State = State.IDLE
        private set

    private val scope = CoroutineScope(Dispatchers.Default + SupervisorJob())
    private var job: Job? = null
    private var audioTrack: AudioTrack? = null
    // Session-scoped counters used only for diagnostic queue-latency telemetry.
    @Volatile private var submittedFramesForMetrics: Long = 0L
    // Separate ITD/crossfeed history from the 48 kHz system-wide capture path.
    private val playbackSpatialEngine = com.ivanna.omega.spatial.IvannaSpatialEngine.createIsolated()
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


    /**
     * Estima la cola pendiente del AudioTrack (frames aceptados por la app menos
     * frames ya presentados según AudioTimestamp). No es una medición acústica
     * del retardo del altavoz/DAC: ese requiere loopback físico calibrado.
     */
    private fun estimateQueuedOutputLatencyMs(): Float {
        return try {
            val track = audioTrack ?: return 0f
            val rate = track.sampleRate.takeIf { it > 0 } ?: return 0f
            val timestamp = AudioTimestamp()
            if (!track.getTimestamp(timestamp)) return 0f

            val submitted = submittedFramesForMetrics
            val queuedFrames = (submitted - timestamp.framePosition).coerceAtLeast(0L)
            (queuedFrames * 1_000f / rate).coerceAtLeast(0f)
        } catch (_: Throwable) {
            0f
        }
    }

    // Solo lo tocan el decode loop y pollOmegaMetrics(), que corre dentro del mismo bucle.
    @Volatile private var predictiveAnalyzer: PredictiveAudioAnalyzer? = null

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

            // FIX: telemetry[3] es target_gain (0..2), no CPU: antes se mostraba la ganancia
            // objetivo como "% de CPU". Sin fuente nativa real de CPU → 0 = sin medición.
            val cpuPercent = 0f

            val yamnetCategory = try {
                if (IvannaNpeEngine.isReady) IvannaNpeNative.nativeGetDetectedGenre() else "—"
            } catch (_: Throwable) { "—" }

            val yamnetConfidence = try {
                if (IvannaNpeEngine.isReady) {
                    val sig = IvannaNpeNative.nativeGetSynthClassify()
                    if (sig.size > 1) sig[1] else 0f  // [cluster_id, confidence, ...]: antes leía el id de cluster
                } else 0f
            } catch (_: Throwable) { 0f }

            val predictive = predictiveAnalyzer?.snapshot() ?: PredictiveSnapshot()
            OmegaMetrics.updateSharedPredictive(predictive.valid, predictive.confidence, predictive.predictionGainDb, predictive.transient)

            _omegaMetrics.value = OmegaMetrics(
                predictiveValid      = predictive.valid,
                predictiveConfidence = predictive.confidence,
                predictiveGainDb     = predictive.predictionGainDb,
                predictiveTransient  = predictive.transient,
                rmsLevel         = pipelineState.rms,
                peakLevel        = pipelineState.peak,
                clipCount        = clipCount,
                cpuPercent       = cpuPercent,
                latencyMs        = estimateQueuedOutputLatencyMs(),
                sampleRate       = audioTrack?.sampleRate ?: AudioPipeline.SAMPLE_RATE,
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
        queue.clear()
        queue.addAll(uris)
        if (queue.isEmpty()) {
            queueIndex = -1
            stop()
            // Notify the UI that the queue is cleared without inventing a track URI.
            onQueueAdvance?.invoke(Uri.EMPTY)
            return
        }
        queueIndex = startIndex.coerceIn(0, queue.lastIndex)
        play(queue[queueIndex])
    }

    private fun advanceQueueIfAny() {
        if (queueIndex < 0 || queueIndex >= queue.lastIndex) return
        queueIndex++
        val next = queue[queueIndex]
        onQueueAdvance?.invoke(next)
        play(next)
    }

    /**
     * Avance explícito solicitado por controles del sistema (wearables,
     * auriculares, pantalla de bloqueo). Comparte la misma cola y callback
     * que el avance por EOS; no depende de que termine el tema actual.
     */
    fun skipToNext(): Boolean {
        if (queueIndex < 0 || queueIndex >= queue.lastIndex) {
            Log.i(TAG, "skipToNext ignored: no next item in queue")
            return false
        }
        queueIndex++
        val next = queue[queueIndex]
        onQueueAdvance?.invoke(next)
        play(next)
        return true
    }

    /** Previous-track action from the Android media session / compatible wearables. */
    fun skipToPrevious(): Boolean {
        if (queueIndex <= 0 || queueIndex > queue.lastIndex) {
            // At the start of the queue, the conventional action restarts this track.
            if (queueIndex == 0) {
                seekTo(0L)
                if (state == State.PAUSED) return true
                return true
            }
            Log.i(TAG, "skipToPrevious ignored: no active queue item")
            return false
        }
        queueIndex--
        val previous = queue[queueIndex]
        onQueueAdvance?.invoke(previous)
        play(previous)
        return true
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
        runCatching {
            audioTrack?.let {
                Log.v(TAG, "pause() prev codecState=$codecState")
                if (codecState == CodecState.RUNNING) it.pause()
                else Log.w(TAG, "pause() skip: codecState=$codecState")
            }
        }.onFailure { Log.w(TAG, "pause() falló (prev=$codecState): ${it.message}") }
        state = State.PAUSED
    }

    fun resume() {
        pauseRequested = false
        runCatching {
            audioTrack?.let {
                Log.v(TAG, "resume() prev codecState=$codecState")
                if (codecState == CodecState.RUNNING) it.play()
                else Log.w(TAG, "resume() skip: codecState=$codecState")
            }
        }.onFailure { Log.w(TAG, "resume() falló (prev=$codecState): ${it.message}") }
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

    /**
     * stop() idempotente: sólo actúa sobre track si el estado previo lo
     * permite. El cierre real se serializa en codecMutex para que nunca
     * se solape con un codec.start() en curso dentro de runDecodeLoop.
     */
    fun stop() {
        stopRequested = true
        pauseRequested = false
        job?.cancel()
        job = null
        scope.launch {
            codecMutex.withLock { releaseTrackLocked() }
        }
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
                DSPBridge.applyAdaptiveState(
                    AdaptiveDSPState(
                        gain = dspControl.gain,
                        compressor = dspControl.compressor,
                        exciter = dspControl.exciter,
                        spatial = dspControl.spatial,
                        lowEqDb = dspControl.lowEqDb,
                        midEqDb = dspControl.midEqDb,
                        highEqDb = dspControl.highEqDb,
                        iso226Compensation = dspControl.iso226Compensation,
                        fatigueProtection = dspControl.fatigueProtection
                    )
                )
            }
        } catch (e: Exception) {
            Log.e("IvannaBridgePlayer", "Error aplicando estado perceptual: ${e.message}")
        }
    }

    private fun isPlaying(): Boolean = state == State.PLAYING

    /**
     * Cierre de AudioTrack con conocimiento de estado. DEBE llamarse con
     * codecMutex ya tomado (sufijo "Locked"). Sólo ejecuta las llamadas
     * legales para el estado previo:
     *   RUNNING            → pause + flush + stop + release
     *   STOPPED/CONFIGURED → sólo release
     *   IDLE               → no-op
     */
    private fun releaseTrackLocked() {
        // FIX: ya no se abre sesión de efectos stock para el propio track
        // (ver comentario en runDecodeLoop) — no hay nada que cerrar aquí.
        val prev = codecState
        when (prev) {
            CodecState.RUNNING -> {
                runCatching { audioTrack?.pause() }.onFailure {
                    Log.w(TAG, "releaseTrack pause (from $prev): ${it.message}")
                }
                runCatching { audioTrack?.flush() }.onFailure {
                    Log.w(TAG, "releaseTrack flush (from $prev): ${it.message}")
                }
                runCatching { audioTrack?.stop() }.onFailure {
                    Log.w(TAG, "releaseTrack stop (from $prev): ${it.message}")
                }
                runCatching { audioTrack?.release() }.onFailure {
                    Log.w(TAG, "releaseTrack release (from $prev): ${it.message}")
                }
            }
            CodecState.STOPPED, CodecState.CONFIGURED -> {
                runCatching { audioTrack?.release() }.onFailure {
                    Log.w(TAG, "releaseTrack release (from $prev): ${it.message}")
                }
            }
            CodecState.IDLE -> { /* nada que cerrar */ }
        }
        audioTrack = null
        codecState = CodecState.IDLE
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
            val outputRoute = selectOutputRoute()
            val sampleRate = outputRoute.sampleRate
            val channelCount = format.getInteger(MediaFormat.KEY_CHANNEL_COUNT)
            val mime = format.getString(MediaFormat.KEY_MIME)!!

            val resampler = StereoAudioResampler(sampleRate)
            resampler.setInputSampleRate(inputSampleRate)
            playbackSpatialEngine.configureSampleRate(sampleRate)
            playbackSpatialEngine.reset()
            // Rate estimator remains diagnostic; the closed-loop correction is
            // driven by actual AudioTrack queue phase (submitted - consumed frames).
            val clockDriftServo = AudioTrackClockDriftServo(sampleRate)
            val bufferPll = AudioTrackBufferPll(sampleRate)
            val outputTimestamp = AudioTimestamp()
            OmegaMetrics.updateClockServoTelemetry(false, false, 0.0, 0.0, 0.0)
            DSPBridge.init(sampleRate)
            val predictive = PredictiveAudioAnalyzer(sampleRate)
            predictiveAnalyzer = predictive

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
            outputRoute.preferredDevice?.let { preferred ->
                if (!track.setPreferredDevice(preferred)) {
                    Log.w(TAG, "Android rechazó la ruta preferida ${preferred.productName}; se conserva la ruta elegida por AudioPolicy")
                }
            }
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
            // Configuración + arranque bajo codecMutex: ningún stop()/
            // release() concurrente puede cerrar el track mientras
            // configure()/start() están en curso (IllegalStateException).
            codecMutex.withLock {
                if (codecState != CodecState.IDLE) {
                    Log.w(TAG, "runDecodeLoop con codecState=$codecState — cerrando antes de recrear")
                    releaseTrackLocked()
                    // audioTrack quedó en null; volver a enlazar el de esta sesión
                    audioTrack = track
                }
                runCatching { codec.configure(format, null, null, 0) }
                    .onFailure {
                        Log.e(TAG, "codec.configure falló (prev codecState=$codecState): ${it.message}")
                        releaseTrackLocked()
                        state = State.ERROR
                        return@withContext
                    }
                codecState = CodecState.CONFIGURED
                runCatching {
                    codec.start()
                    track.play()
                    AudioTrackRouteReadback.update(track, outputRoute.preferredDevice, sampleRate)
                    Log.i(TAG, "AudioTrack route readback after play: ${AudioTrackRouteReadback.current()}")
                }.onFailure {
                    Log.e(TAG, "codec.start/track.play falló (prev codecState=CONFIGURED): ${it.message}")
                    releaseTrackLocked()
                    state = State.ERROR
                    return@withContext
                }
                codecState = CodecState.RUNNING
            }
            state = State.PLAYING

            val bufferInfo = MediaCodec.BufferInfo()
            var sawInputEOS = false
            var sawOutputEOS = false
            // AudioTrack.write(float[]) reports float samples, so divide by
            // two to count accepted interleaved stereo frames cumulatively.
            var submittedFrames = 0L
            submittedFramesForMetrics = 0L
            var lastClockPollNs = 0L

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
                    predictive.reset()
                    extractor.seekTo(pendingSeekUs, MediaExtractor.SEEK_TO_PREVIOUS_SYNC)
                    // codec.flush() sólo es legal en estado Executing (nuestro
                    // RUNNING). Guard + log del estado previo — sin guard,
                    // flush en estado inválido lanza IllegalStateException.
                    runCatching {
                        if (codecState == CodecState.RUNNING) codec.flush()
                        else Log.w(TAG, "flush skip: codecState=$codecState")
                    }.onFailure { Log.w(TAG, "codec.flush (prev=$codecState): ${it.message}") }
                    if (npeKotlinEnabled) npeKotlin.reset()
                }

                if (!sawInputEOS) {
                    val inIndex = codec.dequeueInputBuffer(TIMEOUT_US)
                    if (inIndex >= 0) {
                        val inBuf = codec.getInputBuffer(inIndex) ?: continue
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
                    val outBuf = codec.getOutputBuffer(outIndex) ?: continue
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
                            // Convert source PCM to the selected output clock BEFORE DSP/HRTF.
                            // Those kernels are initialized at sampleRate, not inputSampleRate.
                            // Keep headroom so upsampling cannot overflow spatial scratch buffers.
                            val maxInputChunkFrames = (
                                (MAX_CHUNK_FRAMES - 64).toDouble() * inputSampleRate / sampleRate
                            ).toInt().coerceAtLeast(1)
                            val inputChunkFrames = minOf(maxInputChunkFrames, totalFrames - offset)
                            val inputChunk = stereo.copyOfRange(offset * 2, (offset + inputChunkFrames) * 2)
                            val chunk = resampler.process(inputChunk)
                            val chunkFrames = chunk.size / 2
                            offset += inputChunkFrames
                            if (chunkFrames <= 0) continue
                            voiceProtection?.feed(chunk, chunkFrames, sampleRate)
                            // Ruta A (reproductor propio) y Ruta B (apps de terceros) son
                            // mutuamente excluyentes: AudioSessionReceiver filtra el paquete
                            // de IVANNA, por lo que esta sesión NO tiene omega_effect global.
                            // La conexión del daemon solo acredita el plano de control/socket;
                            // no prueba que AudioFlinger procese este AudioTrack. Mantener aquí
                            // el DSP local evita audio sin procesar cuando el daemon está vivo.
                            val nativeDspHandled = DSPBridge.isLoaded
                            if (DSPBridge.isLoaded) {
                                DSPBridge.process(chunk, chunkFrames)
                            }
                            if (com.ivanna.omega.core.IvannaNativeLib.isLoaded &&
                                analyzeTickCounter.incrementAndGet() % 50 == 0) {
                                runCatching { com.ivanna.omega.core.IvannaNativeLib.nativeAnalyzeAudio(chunk) }
                            }
                            // Árbitro de Ruta Única: si el daemon system-wide está activo o DSPBridge
                            // nativo ya ejecutó la cadena maestra (Volterra C++, 5 Ejes, Spatial C++ y
                            // SafetyLimiter), no apilar procesadores duplicados sobre el mismo bloque.
                            if (!nativeDspHandled) {
                                if (IvannaNpeEngine.isReady && !npeSampleRateMismatch) {
                                    try { IvannaNpeEngine.processInterleavedStereo(chunk, chunkFrames) } catch (e: Exception) { android.util.Log.e("IVANNA_DSP", "Crash NPE evitado: ${e.message}") }
                                }
                                if (npeKotlinEnabled) {
                                    try {
                                        val npeIn = chunk.copyOf()
                                        val npeOut = npeKotlin.process(npeIn)
                                        npeOut.copyInto(chunk)
                                    } catch (e: Exception) {
                                        android.util.Log.e("IVANNA_DSP", "Crash NPE-Kotlin evitado: ${e.message}")
                                    }
                                }
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
                                    playbackSpatialEngine.processStereoInput(
                                        spatialInL, spatialInR, spatialOutL, spatialOutR, chunkFrames
                                    )
                                    for (i in 0 until chunkFrames) {
                                        chunk[i * 2] = spatialOutL[i]
                                        chunk[i * 2 + 1] = spatialOutR[i]
                                    }
                                }
                            }
                            predictive.feed(chunk, chunkFrames)   // observador: no modifica `chunk`
                            val writtenSamples = track.write(
                                chunk, 0, chunk.size, AudioTrack.WRITE_BLOCKING
                            )
                            // Closed-loop software PLL: estimate queue phase from frames
                            // accepted by this AudioTrack minus its timestamped consumed frames.
                            // No Bluetooth/DAC/vendor-HAL clock writes are attempted.
                            if (writtenSamples > 0) {
                                AudioTrackRouteReadback.update(track, outputRoute.preferredDevice, sampleRate)
                                submittedFrames += (writtenSamples / 2).toLong()
                                submittedFramesForMetrics = submittedFrames
                                // Timestamp polling is throttled off the per-chunk hot path.
                                val nowNs = SystemClock.elapsedRealtimeNanos()
                                if (lastClockPollNs == 0L || nowNs - lastClockPollNs >= 250_000_000L) {
                                    lastClockPollNs = nowNs
                                    if (track.getTimestamp(outputTimestamp)) {
                                        // Frequency estimate is diagnostic; the buffer PI drives ASRC.
                                        clockDriftServo.observe(
                                            outputTimestamp.framePosition,
                                            outputTimestamp.nanoTime
                                        )
                                        val correctionPpm = bufferPll.observe(
                                            submittedFrames = submittedFrames,
                                            consumedFrames = outputTimestamp.framePosition,
                                            bufferCapacityFrames = track.bufferSizeInFrames,
                                            timestampNs = outputTimestamp.nanoTime
                                        )
                                        resampler.setClockCorrectionPpm(correctionPpm)
                                        OmegaMetrics.updateClockServoTelemetry(
                                            timestampAvailable = true,
                                            locked = bufferPll.isLocked,
                                            measuredRateHz = clockDriftServo.measuredRateHz,
                                            measuredDriftPpm = clockDriftServo.measuredDriftPpm,
                                            correctionPpm = correctionPpm
                                        )
                                    } else {
                                        clockDriftServo.reset()
                                        bufferPll.reset()
                                        resampler.setClockCorrectionPpm(0.0)
                                        OmegaMetrics.updateClockServoTelemetry(false, false, 0.0, 0.0, 0.0)
                                    }
                                }
                            }
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
            predictiveAnalyzer?.close()
            predictiveAnalyzer = null
            OmegaMetrics.updateSharedPredictive(false, 0f, 0f, false)
            // Cierre SIEMPRE bajo mutex y sólo si el estado lo permite:
            // codec.stop() es ilegal fuera de Executing → nuestro RUNNING.
            codecMutex.withLock {
                val prev = codecState
                codec?.let { c ->
                    when (prev) {
                        CodecState.RUNNING -> {
                            runCatching { c.stop() }.onFailure {
                                Log.w(TAG, "codec.stop (from $prev): ${it.message}")
                            }
                            codecState = CodecState.STOPPED
                        }
                        else -> Log.v(TAG, "codec.stop skip: prev=$prev")
                    }
                    runCatching { c.release() }.onFailure {
                        Log.w(TAG, "codec.release (from $codecState): ${it.message}")
                    }
                }
                releaseTrackLocked() // deja codecState en IDLE
            }
            extractor.release()
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
