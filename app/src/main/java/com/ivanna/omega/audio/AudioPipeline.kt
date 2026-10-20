package com.ivanna.omega.audio

import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.AudioTrack
import android.media.MediaRecorder
import android.os.Process
import android.util.Log
import android.Manifest
import android.content.pm.PackageManager
import com.ivanna.omega.safmus.SafmusAdapterPolicy
import com.ivanna.omega.safmus.SafmusHardwareCoordinator
import com.ivanna.omega.dsp.DSPBridge
import com.ivanna.omega.dsp.DSPState
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlin.math.sqrt

/**
 * AudioPipeline — Captura audio → DSP → reproducción.
 *
 * FIXES DE CONECTIVIDAD:
 *   1. nativeSetAntiDolbyScores: el bucle de audio llama a
 *      AudioEngine.nativeSetAntiDolbyScores() para que el orquestador C++
 *      ajuste widener/EQ dinámicamente según la clasificación YAMNet.
 *      Antes esta conexión no existía — el clasificador AI corría pero
 *      sus resultados nunca llegaban al hot-path de audio.
 *   2. YamnetClassifier integrado: se downsamplea el buffer 48kHz→16kHz
 *      y se clasifica cada ~1s (throttle de frames).
 */
class AudioPipeline {

    data class SharedYamnetResult(
        val speech: Float = 0f,
        val music: Float = 0f,
        val bass: Float = 0f,
        val valid: Boolean = false,
        /** Tiempo medido de clasificación (µs); 0 = sin medición. */
        val inferenceUs: Long = 0L,
        /** Origen real del resultado: "CRNN TFLite" o "Heurística espectral". */
        val source: String = "—"
    )

    companion object {
        /** SR real del hardware — query a AudioManager, fallback 48k si la propiedad no existe.
         *  Hardcodear 96kHz hacía que AudioRecord/AudioTrack rechazaran el pipeline en
         *  dispositivos cuyo DSP nativo corre a 48k (la mayoría): Ruta A moría al iniciar. */
        private const val FALLBACK_SAMPLE_RATE = 48000
        @Volatile var SAMPLE_RATE: Int = FALLBACK_SAMPLE_RATE
            private set
        fun syncHardwareSampleRate(context: android.content.Context) {
            val am = context.getSystemService(android.content.Context.AUDIO_SERVICE) as? android.media.AudioManager
            val hw = am?.getProperty(android.media.AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE)?.toIntOrNull()
            if (hw != null && hw in 8000..384000) SAMPLE_RATE = hw  // paridad con AudioEngine: rutas nativas directas hasta 384k (USB-DAC)
        }
        const val FRAMES_PER_BLOCK = 256
        const val BUFFER_SIZE = FRAMES_PER_BLOCK * 2   // estéreo intercalado

        // El clasificador recibe una ventana real de 0.975 s a 16 kHz;
        // la acumulación se hace por bloque, no por una llamada aislada cada segundo.

        // 3J: resultado YAMNet global — actualizado por cualquier instancia activa
        // de AudioPipeline; collectado en TelemetryDashboard sin pasar Context.
        private val _sharedYamnetResult = MutableStateFlow(
            object {
                val speech: Float = 0f; val music: Float = 0f
                val bass: Float = 0f; val valid: Boolean = false
            }
        )
        // Usamos la data class interna — se declara antes que companion en Kotlin
        // así que no la podemos referenciar aquí directamente. Exponemos campos
        // planos en un SimpleResult para evitar la dependencia de orden de init.
        private val _sharedYamnet = MutableStateFlow(SharedYamnetResult())
        val sharedYamnetResult: StateFlow<SharedYamnetResult> = _sharedYamnet.asStateFlow()

        /** Publicación desde el clasificador vivo (hilo de fondo, nunca el de audio). */
        fun publishLiveClassification(speech: Float, music: Float, bass: Float,
                                      inferenceUs: Long, source: String) {
            _sharedYamnet.value = SharedYamnetResult(speech, music, bass, true, inferenceUs, source)
        }
    }

    private val tag = "IVANNA.Pipeline"
    private val scope = CoroutineScope(Dispatchers.Default + SupervisorJob())
    @Volatile private var isRunning = false
    private var job: Job? = null
    private var audioTrack: AudioTrack? = null
    private var audioRecord: AudioRecord? = null

    // FIX (cableado real): referencia opcional al gestor USB directo; se
    // setea desde start(context) si el caller la provee. Sin esto la clase
    // nunca sabía que UsbAudioProManager existía.
    private var usbManagerRef: UsbAudioProManager? = null
    @Volatile private var appContext: android.content.Context? = null

    @Volatile private var dspState = DSPState()
    @Volatile private var lastRms = 0f
    @Volatile private var dspLatencyMs = 0f
    @Volatile private var hardwareLatencyMs = 0f

    // Parche 3b: resultado de clasificación Yamnet observable desde el exterior
    data class YamnetResult(val speech: Float = 0f, val music: Float = 0f, val bass: Float = 0f, val valid: Boolean = false)
    private val _yamnetResult = MutableStateFlow(YamnetResult())
    val yamnetResult: StateFlow<YamnetResult> = _yamnetResult.asStateFlow()

    // Ventanas independientes: Ruta A estéreo y captura externa mono pueden
    // coexistir; nunca deben escribir simultáneamente en el mismo acumulador.
    private val yamnetBuffer = FloatArray(15600)        // 0.975 s @ 16 kHz
    private var yamnetWritePos = 0
    private val capturedYamnetBuffer = FloatArray(15600)
    private var capturedYamnetWritePos = 0

    fun setState(state: DSPState) {
        dspState = state
        state.pushToNative()
    }

    fun start() {
        if (isRunning) return
        isRunning = true
        // SAMPLE_RATE ya sincronizado vía syncHardwareSampleRate() desde quien crea el pipeline
        DSPBridge.init(SAMPLE_RATE)
        IvannaUnifiedPipeline.notifyRouteAStarted()
        dspState.pushToNative()
        job = scope.launch { runPipeline() }
    }

    /**
     * Overload que además engancha el UsbAudioProManager singleton, para que
     * el loop de audio le escriba bloques cuando haya una sesión USB directa
     * activa (ver [UsbAudioProManager.isActive]/[UsbAudioProManager.writeAudio]).
     * Antes no había forma de que AudioPipeline supiera que ese manager existía.
     */
    fun start(context: android.content.Context) {
        appContext = context.applicationContext
        usbManagerRef = UsbAudioProManager.getInstance(context)
        start()
    }

    fun stop() {
        isRunning = false
        IvannaUnifiedPipeline.notifyRouteAStopped()
        job?.cancel()
        job = null
        releaseAudio()
    }

    private fun releaseAudio() {
        try { audioRecord?.stop() } catch (_: Throwable) {}
        try { audioRecord?.release() } catch (_: Throwable) {}
        audioRecord = null
        try { audioTrack?.stop() } catch (_: Throwable) {}
        try { audioTrack?.release() } catch (_: Throwable) {}
        audioTrack = null
    }

    private suspend fun runPipeline() {
        Process.setThreadPriority(Process.THREAD_PRIORITY_AUDIO)

        val captureContext = appContext
            ?: runCatching { com.ivanna.omega.core.IVANNAApplication.instance.applicationContext }.getOrNull()
        if (captureContext == null || captureContext.checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            Log.w(tag, "SAFMUS microphone adapter abstains: RECORD_AUDIO permission missing")
            IvannaUnifiedPipeline.notifyRouteAStopped()
            isRunning = false
            return
        }

        val enc = AudioFormat.ENCODING_PCM_FLOAT
        val minIn  = AudioRecord.getMinBufferSize(SAMPLE_RATE, AudioFormat.CHANNEL_IN_STEREO, enc)
        val minOut = AudioTrack.getMinBufferSize(SAMPLE_RATE, AudioFormat.CHANNEL_OUT_STEREO, enc)
        if (minIn <= 0 || minOut <= 0) {
            Log.e(tag, "Hardware rechazó ${SAMPLE_RATE}Hz — Ruta A no disponible")
            IvannaUnifiedPipeline.notifyRouteAStopped()
            isRunning = false; return
        }

        val adapterLoad = when (SafmusHardwareCoordinator.lastSnapshot?.budget) {
            SafmusHardwareCoordinator.Budget.CONSERVATIVE -> SafmusAdapterPolicy.Load.CONSERVATIVE
            SafmusHardwareCoordinator.Budget.SAFE -> SafmusAdapterPolicy.Load.SAFE
            else -> SafmusAdapterPolicy.Load.NORMAL
        }
        val captureRingBytes = SafmusAdapterPolicy.captureBufferBytes(
            minBytes = minIn,
            bytesPerBlock = BUFFER_SIZE * Float.SIZE_BYTES,
            load = adapterLoad
        )
        val record = try {
            AudioRecord(MediaRecorder.AudioSource.UNPROCESSED, SAMPLE_RATE,
                AudioFormat.CHANNEL_IN_STEREO, enc, captureRingBytes)
                .takeIf { it.state == AudioRecord.STATE_INITIALIZED }
                ?: AudioRecord(MediaRecorder.AudioSource.MIC, SAMPLE_RATE,
                    AudioFormat.CHANNEL_IN_STEREO, enc, captureRingBytes)
        } catch (t: Throwable) {
            Log.e(tag, "AudioRecord falló", t); isRunning = false; return
        }
        if (record.state != AudioRecord.STATE_INITIALIZED) {
            record.release(); isRunning = false; return
        }
        audioRecord = record

        val track = AudioTrack.Builder()
            .setAudioAttributes(AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_MEDIA)
                .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
            .setAudioFormat(AudioFormat.Builder()
                .setEncoding(enc).setSampleRate(SAMPLE_RATE)
                .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO).build())
            .setBufferSizeInBytes(maxOf(minOut, BUFFER_SIZE * Float.SIZE_BYTES * 4))
            .setPerformanceMode(AudioTrack.PERFORMANCE_MODE_LOW_LATENCY)
            .setTransferMode(AudioTrack.MODE_STREAM).build()

        if (track.state != AudioTrack.STATE_INITIALIZED) {
            releaseAudio(); isRunning = false; return
        }
        audioTrack = track

        Log.i(tag, "Pipeline activo: ${SAMPLE_RATE}Hz | DSP=${DSPBridge.isLoaded} | SAFMUS mic-ring=${captureRingBytes}B load=${adapterLoad}")

        // ── Análisis de concurrencia con Ruta B (omega_effect.cpp) ─────────────
        // Ruta A (este pipeline): captura MIC/UNPROCESSED → DSPBridge → AudioTrack.
        // Ruta B (omega_effect.cpp): intercepta la reproducción en AudioFlinger.
        //
        // Son señales físicamente DISTINTAS — no se solapan:
        //   Ruta A: entrada de micrófono  (AudioRecord SOURCE_UNPROCESSED)
        //   Ruta B: salida de reproducción (AudioFlinger effect chain)
        //
        // El ADR "Route Arbiter" y el campo route_mode del OmegaControlBus controlan
        // qué PRESET se aplica en cada ruta, no si se procesan en paralelo — eso
        // ya está garantizado por la separación física de AudioRecord vs AudioFlinger.
        //
        // Conclusión: SET_ROUTE_MODE=SYSTEM_WIDE + Ruta A activa simultáneamente
        // NO produce doble procesamiento de la misma señal. Verificado y cerrado.

        try {
            record.startRecording()
            track.play()
            val buf = FloatArray(BUFFER_SIZE)

            while (isRunning && currentCoroutineContext().isActive) {
                val read = record.read(buf, 0, buf.size, AudioRecord.READ_BLOCKING)
                if (read <= 0) continue

                var sumSq = 0f
                for (s in buf) sumSq += s * s
                lastRms = sqrt(sumSq / buf.size.coerceAtLeast(1))

                val t0 = System.nanoTime()
                DSPBridge.process(buf, read / 2)
                dspLatencyMs = (System.nanoTime() - t0) / 1_000_000f

                // FIX (PUNTO 2): alimenta RealTimeCinematicEngine con audio real
                // del hot-path. Identidad (no-op) si el toggle "Anti-Dolby
                // adaptativo" está apagado — ver CinematicEngineHost.
                val cinematic = CinematicEngineHost.processBlock(buf, read)
                if (cinematic !== buf) System.arraycopy(cinematic, 0, buf, 0, minOf(cinematic.size, buf.size))

                // Acumular cada bloque; la ventana completa dispara una clasificación
                // aproximadamente cada segundo sin perder los bloques intermedios.
                feedYamnet(buf, read)

                for (i in 0 until read) buf[i] = buf[i].coerceIn(-1f, 1f)

                // En el path unificado (default), el DAC Tipo-C recibe el audio
                // procesado directamente por AudioTrack -> AudioFlinger -> snd-usb-audio
                // (mismo path que AUX/Speaker/Bluetooth). Solo se alimenta writeAudio
                // si el bypass URB directo fue abierto explícitamente.
                val usbManager: UsbAudioProManager? = usbManagerRef
                if (usbManager != null && usbManager.isDirectBypassActive()) {
                    usbManager.writeAudio(buf, read / 2)
                }

                track.write(buf, 0, read, AudioTrack.WRITE_BLOCKING)
            }
        } catch (t: Throwable) {
            Log.e(tag, "Error en loop de audio", t)
        } finally {
            releaseAudio()
        }
    }

    /**
     * FIX: Downsamplea 48kHz→16kHz y manda scores al orquestador C++.
     * Antes el clasificador YAMNet corría pero los resultados nunca
     * llegaban al nativeSetAntiDolbyScores() — este era el eslabón roto.
     */
    private fun feedYamnet(buf: FloatArray, read: Int) {
        // Downsample 3:1 usando el canal izquierdo de PCM estéreo intercalado.
        var pos = yamnetWritePos
        var i = 0
        while (i < read && pos < yamnetBuffer.size) {
            yamnetBuffer[pos++] = buf[i]
            i += 6  // un sample por cada 3 frames estéreo
        }
        if (pos >= yamnetBuffer.size) {
            classifyAndRoute(yamnetBuffer)
            pos = 0
        }
        yamnetWritePos = pos
    }

    private fun classifyAndRoute(frame: FloatArray) {
        // When AntiDolby CRNN is active, it is the single source of classification.
        // Do not let the legacy spectral heuristic overwrite its native scores.
        if (CinematicEngineHost.isActive) return

        // Estimador por energía de bandas mediante filtros paso-bajo en cascada.
        // Es una heurística espectral, no un clasificador semántico de voz/música.
        val scores = AudioBandEnergyClassifier.analyze(frame, SAMPLE_RATE / 3)
        if (scores.rms > 0.001f) {
            try {
                AudioEngine.nativeSetAntiDolbyScoresStatic(scores.speech, scores.music, scores.bass)
                _yamnetResult.value = YamnetResult(scores.speech, scores.music, scores.bass, true)
                _sharedYamnet.value = SharedYamnetResult(
                    scores.speech, scores.music, scores.bass, true, 0L, "Heurística espectral por bandas"
                )
            } catch (_: Exception) {}
        }
    }

    /**
     * FIX (Ruta A telemetria muerta): PlaybackCaptureService captura mono
     * via MediaProjection y lo enruta al DSP, pero antes no habia un
     * punto de entrada publico en AudioPipeline para ese mono. Este
     * metodo lo expone SIN tocar el hot-path de AudioRecord/AudioTrack
     * de arriba: solo alimenta al clasificador YAMNet y al bus de
     * telemetria compartido, para que el usuario vea RMS/peak/clips
     * VIVOS aunque la reproduccion la haga Spotify/YouTube.
     *
     * Idempotente: si el buffer es vacio no hace nada. Thread-safe:
     * solo escribe en yamnetBuffer (uso exclusivo del scope de captura)
     * y en el StateFlow compartido (MutableStateFlow es thread-safe).
     *
     * @param mono Buffer mono en float [-1..1] a 48kHz.
     * @param frames Numero de muestras validas en mono.
     */
    fun feedCapturedMono(mono: FloatArray, frames: Int) {
        if (frames <= 0) return
        val n = minOf(frames, mono.size)

        // Niveles vivos: RMS y peak sobre el bloque capturado.
        var sumSq = 0f
        var peak = 0f
        var clips = 0
        for (i in 0 until n) {
            val s = mono[i]
            sumSq += s * s
            val a = if (s < 0f) -s else s
            if (a > peak) peak = a
            if (a >= 0.999f) clips++
        }
        lastRms = sqrt(sumSq / n.coerceAtLeast(1))

        // Ruta B/captura externa: alimentar el mismo CRNN AntiDolby que usa Ruta A.
        // El host sólo observa/clasifica y devuelve el audio intacto.
        if (CinematicEngineHost.isActive) CinematicEngineHost.processBlock(mono, n)

        // Publicar en el bus compartido para que EngineStatusCard salga
        // de STANDBY: el default de shared trae rms=0/peak=0/clips=0.
        OmegaMetrics.updateSharedLevels(
            rms   = lastRms,
            peak  = peak,
            clips = clips,
            dspActive = true,
        )

        // Alimentar una ventana mono independiente: downsample 48 kHz → 16 kHz.
        // La acumulación por llamada evita descartar casi todo el audio entre inferencias.
        var pos = capturedYamnetWritePos
        var i = 0
        while (i < n && pos < capturedYamnetBuffer.size) {
            capturedYamnetBuffer[pos++] = mono[i]
            i += 3
        }
        if (pos >= capturedYamnetBuffer.size) {
            classifyAndRoute(capturedYamnetBuffer)
            pos = 0
        }
        capturedYamnetWritePos = pos
    }

    fun setBypass(bypass: Boolean) { setState(dspState.copy(bypass = bypass)) }

    fun getMetrics(): Map<String, Float> = mapOf(
        "rms"                to lastRms,
        "dspLatencyMs"       to dspLatencyMs,
        "hardwareLatencyMs"  to hardwareLatencyMs,
        "totalLatencyMs"     to (dspLatencyMs + hardwareLatencyMs),
        "correlation"        to 1f
    )
}
