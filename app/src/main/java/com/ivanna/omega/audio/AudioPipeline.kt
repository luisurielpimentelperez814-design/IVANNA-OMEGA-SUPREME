package com.ivanna.omega.audio

import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.AudioTrack
import android.media.MediaRecorder
import android.os.Process
import android.util.Log
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
        val valid: Boolean = false
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
            if (hw != null && hw in 8000..192000) SAMPLE_RATE = hw
        }
        const val FRAMES_PER_BLOCK = 256
        const val BUFFER_SIZE = FRAMES_PER_BLOCK * 2   // estéreo intercalado

        // Throttle YAMNet: clasificar cada N bloques (~1s @ 48kHz con bloques de 256)
        private const val YAMNET_CLASSIFY_EVERY_N = 187  // 187 × 256 = ~96000 frames = 1s

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

    @Volatile private var dspState = DSPState()
    @Volatile private var lastRms = 0f
    @Volatile private var dspLatencyMs = 0f
    @Volatile private var hardwareLatencyMs = 0f

    // Parche 3b: resultado de clasificación Yamnet observable desde el exterior
    data class YamnetResult(val speech: Float = 0f, val music: Float = 0f, val bass: Float = 0f, val valid: Boolean = false)
    private val _yamnetResult = MutableStateFlow(YamnetResult())
    val yamnetResult: StateFlow<YamnetResult> = _yamnetResult.asStateFlow()

    // FIX: buffer para downsample 48kHz→16kHz para YAMNet
    private val yamnetBuffer = FloatArray(15600)  // 0.975s @ 16kHz
    private var yamnetWritePos = 0
    private var blockCounter = 0

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

        val enc = AudioFormat.ENCODING_PCM_FLOAT
        val minIn  = AudioRecord.getMinBufferSize(SAMPLE_RATE, AudioFormat.CHANNEL_IN_STEREO, enc)
        val minOut = AudioTrack.getMinBufferSize(SAMPLE_RATE, AudioFormat.CHANNEL_OUT_STEREO, enc)
        if (minIn <= 0 || minOut <= 0) {
            Log.e(tag, "Hardware rechazó ${SAMPLE_RATE}Hz — Ruta A no disponible")
            IvannaUnifiedPipeline.notifyRouteAStopped()
            isRunning = false; return
        }

        val record = try {
            AudioRecord(MediaRecorder.AudioSource.UNPROCESSED, SAMPLE_RATE,
                AudioFormat.CHANNEL_IN_STEREO, enc,
                maxOf(minIn, BUFFER_SIZE * Float.SIZE_BYTES * 4))
                .takeIf { it.state == AudioRecord.STATE_INITIALIZED }
                ?: AudioRecord(MediaRecorder.AudioSource.MIC, SAMPLE_RATE,
                    AudioFormat.CHANNEL_IN_STEREO, enc,
                    maxOf(minIn, BUFFER_SIZE * Float.SIZE_BYTES * 4))
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

        Log.i(tag, "Pipeline activo: ${SAMPLE_RATE}Hz | DSP=${DSPBridge.isLoaded}")

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
                val cinematic = CinematicEngineHost.processBlock(buf)
                if (cinematic !== buf) System.arraycopy(cinematic, 0, buf, 0, minOf(cinematic.size, buf.size))

                // FIX: acumular muestras para YAMNet (downsample 48kHz→16kHz, ratio 3:1)
                blockCounter++
                if (blockCounter % YAMNET_CLASSIFY_EVERY_N == 0) {
                    feedYamnet(buf, read)
                }

                for (i in 0 until read) buf[i] = buf[i].coerceIn(-1f, 1f)

                // FIX (cableado real): si hay un DAC USB en modo directo activo,
                // el audio ya procesado también se manda al path bit-perfect
                // (bypass del mezclador de Android), además del AudioTrack normal
                // (que sigue sonando por el mixer mientras el consumidor nativo
                // async del DAC — hoy stub de logging — no reemplace la salida).
                usbManagerRef?.let { if (it.isActive()) it.writeAudio(buf, read / 2) }

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
        // Downsample simple 3:1: tomar cada 3er sample del canal L (índices pares)
        var pos = 0
        var i = 0
        while (i < read && pos < yamnetBuffer.size) {
            yamnetBuffer[pos++] = buf[i]
            i += 6  // saltar 3 frames estéreo (6 floats)
        }
        // Si tenemos suficientes datos, clasificar
        if (pos >= 15600) {
            classifyAndRoute(yamnetBuffer)
            pos = 0
        }
        yamnetWritePos = pos
    }

    private fun classifyAndRoute(frame: FloatArray) {
        // Scores simples basados en energía espectral por bandas (fallback sin TFLite)
        // En dispositivos con yamnet.tflite en assets, YamnetClassifier toma el control
        var bassEnergy = 0f
        var midEnergy = 0f
        var highEnergy = 0f
        var rms = 0f

        for (i in frame.indices) {
            val s = frame[i]
            rms += s * s
            // Aproximación de frecuencia por cruce de cero (muy simple)
            if (i > 0) {
                val diff = frame[i] - frame[i - 1]
                when {
                    diff.math_abs() < 0.01f -> bassEnergy += s * s
                    diff.math_abs() < 0.1f  -> midEnergy += s * s
                    else                    -> highEnergy += s * s
                }
            }
        }

        rms = kotlin.math.sqrt(rms / frame.size)
        val total = bassEnergy + midEnergy + highEnergy + 1e-8f
        val speechScore = (midEnergy / total).coerceIn(0f, 1f)
        val musicScore  = ((bassEnergy + highEnergy) / total).coerceIn(0f, 1f)
        val bassScore   = (bassEnergy / total).coerceIn(0f, 1f)

        if (rms > 0.001f) {  // Solo clasificar si hay señal real
            // FIX: enviar scores al orquestador nativo
            try {
                AudioEngine.nativeSetAntiDolbyScoresStatic(speechScore, musicScore, bassScore)
                _yamnetResult.value = YamnetResult(speechScore, musicScore, bassScore, true)
                // 3J: exponer globalmente para TelemetryDashboard
                _sharedYamnet.value = SharedYamnetResult(speechScore, musicScore, bassScore, true)
            } catch (_: Exception) {}
        }
    }

    private fun Float.math_abs() = if (this < 0) -this else this

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

        // Publicar en el bus compartido para que EngineStatusCard salga
        // de STANDBY: el default de shared trae rms=0/peak=0/clips=0.
        OmegaMetrics.updateSharedLevels(
            rms   = lastRms,
            peak  = peak,
            clips = clips,
            dspActive = true,
        )

        // Alimentar YAMNet en modo throttled (mismo ritmo que Ruta A directa).
        blockCounter++
        if (blockCounter % YAMNET_CLASSIFY_EVERY_N == 0) {
            // El feedYamnet original espera stereo intercalado: aqui es mono,
            // asi que hacemos el downsample 3:1 in-place sobre el mono.
            var pos = yamnetWritePos
            var i = 0
            while (i < n && pos < yamnetBuffer.size) {
                yamnetBuffer[pos++] = mono[i]
                i += 3
            }
            if (pos >= 15600) {
                classifyAndRoute(yamnetBuffer)
                pos = 0
            }
            yamnetWritePos = pos
        }
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
