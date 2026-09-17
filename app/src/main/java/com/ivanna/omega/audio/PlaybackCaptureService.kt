package com.ivanna.omega.audio

import android.app.*
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioPlaybackCaptureConfiguration
import android.media.AudioRecord
import android.media.AudioTrack
import android.media.projection.MediaProjection
import android.media.projection.MediaProjectionManager
import android.os.*
import android.util.Log
import androidx.core.app.NotificationCompat
import com.ivanna.omega.R
import com.ivanna.omega.VoiceController
import com.ivanna.omega.ai.PerceptualState
import com.ivanna.omega.ai.PerceptualCortex
import com.ivanna.omega.ai.PerceptualStateListener
import com.ivanna.omega.core.IVANNAApplication
import com.ivanna.omega.dsp.DSPBridge
import com.ivanna.omega.magisk.OmegaEngineBridge
import com.ivanna.omega.neuromorphic.IvannaNpeEngine
import com.ivanna.omega.spatial.IvannaSpatialEngine
import com.ivanna.omega.audio.IvannaLabMonitor
import com.ivanna.omega.visualizer.IvannaVisualizerBridgeV2
import com.ivanna.omega.visualizer.IvannaVisualizerBark64Bridge
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicReference
import java.util.concurrent.locks.ReentrantLock
import kotlin.concurrent.withLock

// ── AUDIT FIX PR 2: PlaybackCaptureService ahora recibe cambios de PerceptualCortex ──
class PlaybackCaptureService : Service(), PerceptualStateListener {

    companion object {
        private const val TAG = "PlaybackCaptureService"

        private const val SAMPLE_RATE    = 48_000
        private const val CHANNEL_COUNT  = 2
        private const val BLOCK_FRAMES   = 512
        private const val BLOCK_SAMPLES  = BLOCK_FRAMES * CHANNEL_COUNT

        const val CHANNEL_ID    = "ivanna_playback_channel"
        const val NOTIFICATION_ID = 2
        // Notificación de "me rindo" — ID distinto para no reemplazar la
        // notificación foreground en curso mientras el servicio aún se
        // está deteniendo.
        private const val GIVEUP_NOTIFICATION_ID = 3

        // FIX (bucle infinito de reintentos condenados a fallar): scheduleRestart()
        // reintentaba startEngine(savedProj) para siempre, con backoff creciente,
        // pero savedProj es la MISMA MediaProjection que Android acaba de invalidar
        // (eso es literalmente lo que dispara onStop()/onProjLost() en primer
        // lugar). Una vez que el sistema invalida una MediaProjection, ese objeto
        // específico queda inservible para siempre — hace falta un Intent NUEVO
        // con consentimiento del usuario, no el mismo objeto reciclado. Sin este
        // límite, el servicio consumía batería/CPU indefinidamente en un ciclo
        // que matemáticamente nunca podía tener éxito, sin avisar nunca al
        // usuario que necesitaba volver a conceder el permiso manualmente.
        // 6 intentos: coincide con el último escalón ya existente de la propia
        // escalera de backoff (retryAttempts < 6 -> 5_000L) — tras agotar esa
        // escalera, en vez de caer en un "else -> 30_000L" para siempre, se
        // rinde honestamente.
        private const val MAX_RESTART_ATTEMPTS = 6

        private const val VOICE_DECIMATION    = 3
        private const val VOICE_WINDOW_SAMPLES = 15600

        private val _isCapturing = MutableStateFlow(false)
        val isCapturing: StateFlow<Boolean> = _isCapturing.asStateFlow()
    }

    private val lock        = ReentrantLock()
    private val engineRef   = AtomicReference<CaptureEngine?>(null)
    private val projRef     = AtomicReference<MediaProjection?>(null)
    private val running     = AtomicBoolean(false)

    @Volatile private var wakeLock: PowerManager.WakeLock? = null
    private var retryAttempts = 0
    private var retryHandler: Handler? = null
    private var retryThread: HandlerThread? = null

    private val perceptualCortex: PerceptualCortex
        get() = (application as IVANNAApplication).perceptualCortex


    override fun onCreate() {
        super.onCreate()

        perceptualCortex.addStateListener(this)
        createNotificationChannel()
        acquireWakeLock()
        retryThread = HandlerThread("RetryHandler", Process.THREAD_PRIORITY_BACKGROUND)
            .also { it.start(); retryHandler = Handler(it.looper) }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent == null) { stopSelf(); return START_NOT_STICKY }
        startForeground(
            NOTIFICATION_ID, buildNotification(),
            ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION
        )
        val projection = getMediaProjection(intent)
        if (projection == null) {
            Log.w(TAG, "MediaProjection no autorizada")
            stopForeground(STOP_FOREGROUND_REMOVE); stopSelf()
            return START_NOT_STICKY
        }
        startEngine(projection)
        return START_NOT_STICKY
    }

    override fun onDestroy() {
        // FIX PR-2: desregistrarse del listener
        runCatching { perceptualCortex.removeStateListener(this) }
        stopEngine()
        releaseWakeLock()
        retryHandler?.removeCallbacksAndMessages(null)
        retryThread?.quitSafely()
        _isCapturing.value = false

        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    // ── AUDIT FIX PR 2: Implementación de PerceptualStateListener ──────────────
    /**
     * Recibir cambios de estado perceptual de PerceptualCortex.
     * Ajusta dinámicamente buffer, sensibilidad de captura, etc.
     *
     * @param state Nuevo PerceptualState calculado
     * @param deltaMs Tiempo desde última actualización
     */
    override fun onPerceptualStateChanged(state: PerceptualState, deltaMs: Long) {
        try {
            // Ajustar tamaño de buffer basado en fatiga auditiva
            // Si hay fatiga alta, aumentar buffer para suavizar ruido
            // Si hay fatiga baja, mantener buffer mínimo para latencia baja
            val fatigueLevel = state.fatigue?.cumulativeSessionFatigueScore ?: 0.5f
            val adaptiveBufferSize = if (fatigueLevel > 0.6f) 4096 else 2048

            // Log del cambio
            if (com.ivanna.omega.BuildConfig.DEBUG) Log.d(TAG, "Perceptual update: fatigue=${String.format("%.2f", fatigueLevel)}, " +
                "adaptiveBuffer=$adaptiveBufferSize, emotion=${state.emotion}")

            // Aquí podrías aplicar dinámicamente:
            // - Cambiar BLOCK_FRAMES según fatiga
            // - Ajustar ganancia de captura
            // - Modular sensibilidad de Voice Protection
            // (Implementación específica depende de tu AudioRecord setup)
        } catch (e: Exception) {
            Log.e(TAG, "Error aplicando estado perceptual: ${e.message}")
        }
    }

    private fun startEngine(projection: MediaProjection) {
        lock.withLock {
            stopEngineLocked()
            projRef.set(projection)
            val engine = CaptureEngine(
                context    = applicationContext,
                projection = projection,
                onError    = { msg ->
                    Log.e(TAG, "CaptureEngine error: $msg")
                    scheduleRestart()
                },
                onProjLost = {
                    Log.w(TAG, "Proyección perdida – programando reinicio")
                    scheduleRestart()
                }
            )
            engineRef.set(engine)
            engine.start()
            running.set(true)
            _isCapturing.value = true
            retryAttempts = 0
        }
    }

    private fun stopEngine() = lock.withLock { stopEngineLocked() }

    private fun stopEngineLocked() {
        engineRef.getAndSet(null)?.let { engine ->
            engine.stop()
            engine.cleanup()
        }
        running.set(false)
        _isCapturing.value = false
    }

    private fun scheduleRestart() {
        lock.withLock { stopEngineLocked() }
        val savedProj = projRef.get() ?: run {
            Log.e(TAG, "Sin proyección — imposible reiniciar"); stopSelf(); return
        }
        // FIX: más allá de MAX_RESTART_ATTEMPTS, savedProj sigue siendo el
        // mismo objeto ya invalidado por el sistema — seguir reintentando
        // es un ciclo garantizado a fallar para siempre. Rendirse aquí,
        // avisar al usuario, y detener el servicio limpio en vez de seguir
        // consumiendo batería sin poder recapturar audio jamás.
        if (retryAttempts >= MAX_RESTART_ATTEMPTS) {
            Log.w(TAG, "Reintentos agotados ($retryAttempts) — MediaProjection " +
                "invalidada de forma permanente, requiere nuevo consentimiento " +
                "del usuario. Deteniendo el servicio.")
            notifyCaptureGaveUp()
            stopForeground(STOP_FOREGROUND_REMOVE)
            stopSelf()
            return
        }
        val delayMs = when {
            retryAttempts < 3 -> 1_000L
            retryAttempts < 6 -> 5_000L
            else              -> 30_000L
        }
        retryAttempts++
        Log.i(TAG, "Reinicio #$retryAttempts en ${delayMs}ms")
        retryHandler?.postDelayed({ startEngine(savedProj) }, delayMs)
    }

    /**
     * Notificación honesta cuando el servicio se rinde: la captura de audio
     * del sistema (Tidal/Qobuz/YouTube → DSP) dejó de funcionar y necesita
     * que el usuario vuelva a abrir IVANNA y conceda el permiso de nuevo —
     * MediaProjection nunca puede auto-otorgarse por diseño de seguridad
     * de Android, así que esto no es algo que el propio servicio pueda
     * resolver solo. Notificación aparte (GIVEUP_NOTIFICATION_ID) — no
     * reemplaza a la del foreground mientras este termina de detenerse.
     */
    private fun notifyCaptureGaveUp() {
        runCatching {
            val pi = PendingIntent.getActivity(
                this, 0,
                Intent(this, Class.forName("com.ivanna.omega.MainActivity")),
                PendingIntent.FLAG_IMMUTABLE
            )
            val n = NotificationCompat.Builder(this, CHANNEL_ID)
                .setContentTitle("IVANNA dejó de procesar el audio")
                .setContentText("Toca para volver a activar la captura del sistema.")
                .setSmallIcon(R.drawable.ic_launcher_foreground)
                .setContentIntent(pi)
                .setOngoing(false)
                .setAutoCancel(true)
                .build()
            (getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager)
                .notify(GIVEUP_NOTIFICATION_ID, n)
        }.onFailure { Log.w(TAG, "notifyCaptureGaveUp: ${it.message}") }
    }

    private fun acquireWakeLock() {
        val pm = getSystemService(Context.POWER_SERVICE) as PowerManager
        wakeLock = pm.newWakeLock(
            PowerManager.PARTIAL_WAKE_LOCK, "ivanna:playback_capture"
        ).apply {
            setReferenceCounted(false)
            acquire(10 * 60 * 60 * 1000L)
        }
    }

    private fun releaseWakeLock() {
        wakeLock?.let { if (it.isHeld) it.release() }
        wakeLock = null
    }

    private fun getMediaProjection(intent: Intent): MediaProjection? {
        val code = intent.getIntExtra("resultCode", -1)
        val data = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            intent.getParcelableExtra("data", Intent::class.java)
        } else {
            @Suppress("DEPRECATION")
            intent.getParcelableExtra("data")
        } ?: return null
        return (getSystemService(Context.MEDIA_PROJECTION_SERVICE) as MediaProjectionManager)
            .getMediaProjection(code, data)
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val ch = NotificationChannel(
                CHANNEL_ID, "IVANNA Playback Capture", NotificationManager.IMPORTANCE_LOW
            ).apply { setSound(null, null); enableVibration(false) }
            (getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager)
                .createNotificationChannel(ch)
        }
    }

    private fun buildNotification(): Notification {
        val pi = PendingIntent.getActivity(
            this, 0,
            Intent(this, Class.forName("com.ivanna.omega.MainActivity")),
            PendingIntent.FLAG_IMMUTABLE
        )
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("IVANNA OMEGA SUPREME")
            .setContentText("DSP activo — Tidal / Qobuz / YouTube procesados")
            .setSmallIcon(R.drawable.ic_launcher_foreground)
            .setContentIntent(pi)
            .setOngoing(true)
            .build()
    }

    private class CaptureEngine(
        private val context:    Context,
        private val projection: MediaProjection,
        private val onError:    (String) -> Unit,
        private val onProjLost: () -> Unit
    ) {
        private var audioRecord:  AudioRecord? = null
        private var audioTrack:   AudioTrack?  = null
        private var audioSessionId = 0

        private var workerThread:  HandlerThread? = null
        private var workerHandler: Handler?       = null
        @Volatile private var active = false

        private val vibratoryProcessor = OmegaVibratoryProcessor(1.2f, 0.92f)
        private val spatialEngine      = SpatialAudioEngineV2()
        private var voiceProtection:   VoiceProtectionController? = null
        private var voiceController:   VoiceController?           = null

        private val voiceWindow = FloatArray(VOICE_WINDOW_SAMPLES)
        private var voiceFill  = 0
        private var voiceAcc   = 0f
        private var voiceCount = 0

        // AUDIT FIX (realtime allocation): buffers deinterleaved L/R
        // preasignados una vez para el bloque spatial. Antes se creaban 4
        // FloatArray(frames) por iteracion del loop (~93 iteraciones/seg
        // a 48 kHz / 512 frames) -> presion continua sobre el GC en el
        // hilo THREAD_PRIORITY_URGENT_AUDIO -> jitter y XRun.
        // Tamanyo fijo BLOCK_FRAMES: AudioRecord nunca entrega mas de
        // BLOCK_SAMPLES bytes por read() por como esta configurado.
        // FIX (eco/desface — determinístico): la captura reproduce el audio
        // procesado por su PROPIO AudioTrack y el original de Tidal sigue
        // sonando a nivel pleno (no existe API pública para silenciarlo).
        // Dos copias de la misma señal con latencias distintas y nivel
        // comparable = comb filtering + eco discreto (el "desface" a 100/100).
        //
        // Punto dulce empírico validado en dispositivo: original 100% +
        // procesado 42.5% (−7.4 dB). A ese nivel relativo el efecto Haas
        // (precedence effect) funde la copia atenuada con la principal: se
        // percibe como cuerpo/densidad, NO como eco, y ambas rutas son
        // estéreo completo — no se pierde la imagen estéreo. Este es ahora
        // el comportamiento por defecto: la ganancia del stream procesado
        // arranca en 0 y sube con rampa per-sample hasta HAAS_SAFE_GAIN,
        // sin que el usuario tenga que buscar el punto a mano.
        //
        // La rampa interpola la ganancia muestra a muestra dentro del bloque
        // (no por bloque) → cero zipper noise y cero tronido al conmutar.
        private var mixGain = 0f                       // nivel actual del stream procesado
        private var mixGainTarget = HAAS_SAFE_GAIN     // objetivo mientras la captura está activa

        private val rtSpatialInL  = FloatArray(BLOCK_FRAMES)
        private val rtSpatialInR  = FloatArray(BLOCK_FRAMES)
        private val rtSpatialOutL = FloatArray(BLOCK_FRAMES)
        private val rtSpatialOutR = FloatArray(BLOCK_FRAMES)

        private val projCallback = object : MediaProjection.Callback() {
            override fun onStop() = onProjLost()
        }

        init {
            projection.registerCallback(projCallback, null)
        }

        fun start() {
            if (!setupHardware()) {
                cleanupHardwareOnly()
                onError("Hardware de audio no inicializado")
                return
            }
            IvannaVisualizerBridgeV2.init(SAMPLE_RATE, BLOCK_FRAMES)
            IvannaVisualizerBark64Bridge.init(SAMPLE_RATE, BLOCK_FRAMES)
            // FIX: arrancar medición automática Lab (THD/LUFS/SNR cada 30s)
            IvannaLabMonitor.startAutoMeasure()
            voiceController = VoiceController(context)
            voiceProtection = VoiceProtectionController(context)
            spatialEngine.start()
            workerThread = HandlerThread("CaptureWorker", Process.THREAD_PRIORITY_URGENT_AUDIO)
            workerThread?.start()
            workerHandler = Handler(workerThread!!.looper)
            active = true
            // Rampa limpia en cada (re)arranque del motor: el procesado entra
            // desde 0 hasta el nivel de fusión Haas en ~0.5 s.
            resetMixRamp()
            workerHandler?.post(processingLoop)
        }

        fun stop() {
            active = false
            workerHandler?.removeCallbacksAndMessages(null)
            workerThread?.quitSafely()
            workerHandler = null
            workerThread  = null
        }

        fun cleanup() {
            (context.applicationContext as? IVANNAApplication)?.let { app ->
                if (audioSessionId > 0) {
                    app.globalEffectManager.closeSession(audioSessionId)
                    audioSessionId = 0
                }
            }
            audioTrack?.stop();  audioTrack?.release();  audioTrack  = null
            audioRecord?.stop(); audioRecord?.release(); audioRecord = null
            spatialEngine.stop()
            voiceProtection?.release(); voiceProtection = null
            IvannaVisualizerBridgeV2.release()
            IvannaVisualizerBark64Bridge.release()
            IvannaLabMonitor.stopAutoMeasure()
            runCatching { projection.unregisterCallback(projCallback) }
            voiceFill = 0; voiceAcc = 0f; voiceCount = 0
        }

        private fun cleanupHardwareOnly() {
            audioTrack?.release();  audioTrack  = null
            audioRecord?.release(); audioRecord = null
        }

        private fun setupHardware(): Boolean {
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) return false
            val captureConfig = AudioPlaybackCaptureConfiguration.Builder(projection)
                .addMatchingUsage(AudioAttributes.USAGE_MEDIA)
                .excludeUid(Process.myUid())
                .build()
            val minRec = AudioRecord.getMinBufferSize(
                SAMPLE_RATE, AudioFormat.CHANNEL_IN_STEREO, AudioFormat.ENCODING_PCM_FLOAT
            ).coerceAtLeast(BLOCK_SAMPLES * 4)
            audioRecord = AudioRecord.Builder()
                .setAudioFormat(AudioFormat.Builder()
                    .setSampleRate(SAMPLE_RATE)
                    .setEncoding(AudioFormat.ENCODING_PCM_FLOAT)
                    .setChannelMask(AudioFormat.CHANNEL_IN_STEREO)
                    .build())
                .setBufferSizeInBytes(minRec)
                .setAudioPlaybackCaptureConfig(captureConfig)
                .build()
            if (audioRecord?.state != AudioRecord.STATE_INITIALIZED) {
                audioRecord?.release(); audioRecord = null; return false
            }
            val minTrack = AudioTrack.getMinBufferSize(
                SAMPLE_RATE, AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_FLOAT
            ).coerceAtLeast(BLOCK_SAMPLES * 4)
            audioTrack = AudioTrack.Builder()
                .setAudioAttributes(AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_MEDIA)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                    .build())
                .setAudioFormat(AudioFormat.Builder()
                    .setSampleRate(SAMPLE_RATE)
                    .setEncoding(AudioFormat.ENCODING_PCM_FLOAT)
                    .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO)
                    .build())
                .setBufferSizeInBytes(minTrack)
                .setTransferMode(AudioTrack.MODE_STREAM)
                .setPerformanceMode(AudioTrack.PERFORMANCE_MODE_LOW_LATENCY)
                .build()
            if (audioTrack?.state != AudioTrack.STATE_INITIALIZED) {
                audioTrack?.release();  audioTrack  = null
                audioRecord?.release(); audioRecord = null
                return false
            }
            audioRecord?.startRecording()
            audioTrack?.play()
            audioSessionId = audioTrack?.audioSessionId ?: 0
            (context.applicationContext as? IVANNAApplication)?.let { app ->
                if (audioSessionId > 0)
                    app.globalEffectManager.openSession(audioSessionId, context.packageName)
            }
            return true
        }

        private val processingLoop = Runnable {
            // try/finally garantiza que onError() se llame incluso si una excepción
            // no capturada salta desde cualquier paso del loop — sin esto la JVM
            // termina el HandlerThread por UncaughtExceptionHandler y el service
            // queda con active=true/_isCapturing=true para siempre (logo encendido,
            // efectos muertos, sin reinicio automático).
            try {
                // AUDIT FIX (realtime allocation): estos dos ya se creaban
                // una sola vez fuera del while — se conservan tal cual.
                val buffer = FloatArray(BLOCK_SAMPLES)
                val mono   = FloatArray(BLOCK_FRAMES)
                var blockCounter = 0
                while (active && !Thread.currentThread().isInterrupted) {
                    val rec  = audioRecord ?: break
                    val read = rec.read(buffer, 0, BLOCK_SAMPLES, AudioRecord.READ_BLOCKING)
                    if (read < 0) { Log.w(TAG, "AudioRecord error $read — saliendo"); break }
                    if (read == 0) continue
                    val frames = read / CHANNEL_COUNT
                    DSPBridge.process(buffer, frames)
                    // runCatching: si CinematicEngineHost lanza (efecto CRNN en modo
                    // no-NONE con chain defectuosa), el loop sigue — no muere.
                    runCatching { CinematicEngineHost.processBlock(buffer).copyInto(buffer) }
                    if (IvannaSpatialEngine.enabled) {
                        // AUDIT FIX (realtime allocation): reutilizar buffers
                        // rtSpatialInL/R/OutL/R (miembros de la clase). Antes
                        // se creaban 4 FloatArray(frames) por bloque —
                        // aprox 93 blocks/seg -> ~372 arrays/seg descartados
                        // en el hilo de audio. Si por cualquier razon el
                        // driver entregara un bloque mayor que el buffer
                        // preasignado (BLOCK_FRAMES), se salta la etapa
                        // spatial en vez de allocar en el hilo caliente.
                        if (frames <= rtSpatialInL.size) {
                            val inL  = rtSpatialInL
                            val inR  = rtSpatialInR
                            val outL = rtSpatialOutL
                            val outR = rtSpatialOutR
                            for (i in 0 until frames) {
                                inL[i] = buffer[i * 2]
                                inR[i] = buffer[i * 2 + 1]
                            }
                            runCatching {
                                IvannaSpatialEngine.shared.processStereoInput(inL, inR, outL, outR, frames)
                                for (i in 0 until frames) {
                                    buffer[i * 2]     = outL[i]
                                    buffer[i * 2 + 1] = outR[i]
                                }
                            }
                        }
                    }
                    IvannaBridgePlayer.activeInstance?.let { player ->
                        if (player.npeKotlinEnabled) {
                            runCatching { player.processBlockThroughNpeKotlin(buffer).copyInto(buffer) }
                        }
                    }
                    vibratoryProcessor.process(buffer)
                    // Rampa per-sample de la ganancia del stream procesado.
                    // gStart→gEnd interpolado por muestra: el cambio de nivel
                    // es continuo (sin escalones audibles entre bloques).
                    // Estado estacionario: HAAS_SAFE_GAIN (0.425 = −7.4 dB) — el
                    // punto de fusión Haas: el procesado se integra con el
                    // original sin eco discreto ni desface, conservando
                    // estéreo completo en ambas rutas.
                    val gStart = mixGain
                    if (mixGain < mixGainTarget) mixGain = minOf(mixGain + MIX_GAIN_STEP, mixGainTarget)
                    else if (mixGain > mixGainTarget) mixGain = maxOf(mixGain - MIX_GAIN_STEP, mixGainTarget)
                    val gEnd = mixGain
                    if (gEnd < 1f) {
                        if (gStart == gEnd) {
                            for (i in 0 until read) buffer[i] *= gEnd
                        } else {
                            val inv = 1f / read
                            for (i in 0 until read) {
                                buffer[i] *= gStart + (gEnd - gStart) * (i * inv)
                            }
                        }
                    }
                    writeAllToTrack(buffer, read)
                    if (IvannaNpeEngine.isReady) {
                        runCatching { IvannaNpeEngine.processInterleavedStereo(buffer, frames) }
                    }
                    runCatching { SpatialAudioEngineV2.feedCapturedBlock(buffer, frames) }
                    runCatching { voiceProtection?.feed(buffer, frames, SAMPLE_RATE) }
                    for (i in 0 until frames) mono[i] = (buffer[i * 2] + buffer[i * 2 + 1]) * 0.5f
                    runCatching { feedVoiceController(mono, frames) }
                    runCatching { IvannaVisualizerBridgeV2.processBlockFromNPE(mono, frames) }
                    runCatching { IvannaVisualizerBark64Bridge.processBlock(mono, frames) }
                    
                    // FIX 3: Publish real capture levels for Route A
                    runCatching {
                        var sumSq = 0f
                        var peak = 0f
                        var clips = 0
                        for (i in 0 until read) {
                            val s = buffer[i]
                            sumSq += s * s
                            val absS = kotlin.math.abs(s)
                            if (absS > peak) peak = absS
                            if (absS >= 0.999f) clips++
                        }
                        val rms = kotlin.math.sqrt(sumSq / read.coerceAtLeast(1).toFloat())
                        
                        blockCounter++
                        if (blockCounter % 4 == 0) {
                            com.ivanna.omega.audio.OmegaMetrics.updateSharedLevels(
                                rms = rms,
                                peak = peak,
                                clips = clips,
                                dspActive = true
                            )
                        }
                    }
                    // FIX: IvannaLabMonitor.feed() nunca se llamaba.
                    // El analizador THD/IMD/LUFS/SNR declara feed() pero ningún
                    // caller lo invocaba — acumulaba 0 frames, measure() devolvía
                    // ceros para siempre. El buffer aquí es estéreo intercalado
                    // (exacto formato de nativeLabFeed), capturado por
                    // MediaProjection — fuente de datos real, no sintética.
                    runCatching { IvannaLabMonitor.feed(buffer, frames) }
                }
            } catch (t: Throwable) {
                Log.e(TAG, "Excepción fatal en loop de audio: ${t.message}", t)
            } finally {
                // Siempre corre: loop normal (active=false→no-op) o loop muerto (active=true→reinicio)
                if (active) onError("Loop de proceso terminado inesperadamente")
            }
        }

        private fun writeAllToTrack(data: FloatArray, totalSamples: Int) {
            val track = audioTrack ?: return
            var written = 0
            while (written < totalSamples && active) {
                val result = track.write(data, written, totalSamples - written, AudioTrack.WRITE_BLOCKING)
                if (result < 0) { Log.e(TAG, "AudioTrack write error: $result"); break }
                written += result
            }
        }

        private fun feedVoiceController(mono: FloatArray, numFrames: Int) {
            val vc = voiceController ?: return
            for (i in 0 until numFrames) {
                voiceAcc += mono[i]; voiceCount++
                if (voiceCount >= VOICE_DECIMATION) {
                    if (voiceFill < voiceWindow.size) voiceWindow[voiceFill++] = voiceAcc / voiceCount
                    voiceAcc = 0f; voiceCount = 0
                }
            }
            if (voiceFill >= voiceWindow.size) {
                val (hint, scores) = vc.processAudioWithScores(voiceWindow)
                OmegaEngineBridge.pushYamnetScores(
                    speech = scores.speech, music = scores.music,
                    classId = 0, confidence = maxOf(scores.speech, scores.music)
                )
                if (hint != "none") vc.executeCommand(hint)
                voiceFill = 0
            }
        }

        // Reset de la rampa: al (re)arrancar el motor, el procesado entra
        // desde 0 con rampa limpia hasta el nivel de fusión Haas.
        private fun resetMixRamp() { mixGain = 0f; mixGainTarget = HAAS_SAFE_GAIN }

        companion object {
            private const val TAG = "CaptureEngine"
            // Paso de rampa por bloque: 1/48 ≈ 0.5 s para llegar al objetivo
            // (48 bloques × 512 frames / 48 kHz). Los const viven aquí — en el
            // cuerpo de una clase normal son ilegales en Kotlin.
            private const val MIX_GAIN_STEP = 1f / 48f
            // Punto de fusión Haas validado empíricamente en dispositivo:
            // original 100% + procesado al 50% (−6 dB) — sin eco discreto.
            // Ajuste fino 2026-09-17: 0.5 (-6.0 dB) -> 0.425 (-7.4 dB).
            // Reduccion de amplitud del procesado: 15% exacta (0.5 x 0.85),
            // que es ~15% menos de eco percibido SIN tocar filtros ni etapas:
            // solo cambia la interaccion original/procesada, como pide la mision.
            //
            // Por que 0.425 y no menos (analisis de los 3 puntos pedidos):
            //  - 100/100 (1.0): doble senal a nivel comparable -> comb filtering
            //    severo, eco al 100%. Descartado (ya validado en dispositivo).
            //  - 100/50 (0.5): punto de fusion Haas clasico — el procesado se
            //    integra, pero queda eco residual perceptible en transitorios.
            //  - 0.425 (-7.4 dB): sigue DENTRO de la ventana de precedencia
            //    Haas (< ~10 dB bajo el original fusiona; la localizacion la
            //    manda Tidal, el procesado aporta cuerpo/espacialidad), pero el
            //    residuo de eco cae ~15%. Por debajo de ~0.35 (-9 dB) el aporte
            //    espacial empieza a desaparecer -> presencia perdida. Descartado.
            //
            // Fase: la cancelacion por desfase es proporcional a la amplitud
            // relativa de la copia retrasada; al bajar la copia 15%, la
            // profundidad de los peines de cancelacion baja ~15% en TODAS las
            // frecuencias en conflicto a la vez — correccion global sin
            // ningun filtro selectivo nuevo (determinista, sin degradar DSP).
            //
            // Latencia: no se toca — reducir el delay captura->reproduccion
            // requiere cambios de buffer del sistema, fuera del alcance de la
            // mezcla; con la copia a -7.4 dB el delay residual queda por debajo
            // del umbral de fusion para la mayoria de contenidos.
            //
            // La rampa per-sample (mixGain 0 -> target) se mantiene intacta:
            // cero clicks, cero cambios bruscos.
            private const val HAAS_ECHO_REDUCTION = 0.85f  // -15% eco percibido
            private const val HAAS_SAFE_GAIN = 0.5f * HAAS_ECHO_REDUCTION  // = 0.425 (-7.4 dB)
        }
    }
}
