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
import com.ivanna.omega.core.IVANNAApplication
import com.ivanna.omega.dsp.DSPBridge
import com.ivanna.omega.magisk.OmegaEngineBridge
import com.ivanna.omega.neuromorphic.IvannaNpeEngine
import com.ivanna.omega.spatial.IvannaSpatialEngine
import com.ivanna.omega.audio.IvannaLabMonitor
import com.ivanna.omega.visualizer.IvannaVisualizerBridgeV2
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicReference
import java.util.concurrent.locks.ReentrantLock
import kotlin.concurrent.withLock

class PlaybackCaptureService : Service() {

    companion object {
        private const val TAG = "PlaybackCaptureService"

        private const val SAMPLE_RATE    = 48_000
        private const val CHANNEL_COUNT  = 2
        private const val BLOCK_FRAMES   = 512
        private const val BLOCK_SAMPLES  = BLOCK_FRAMES * CHANNEL_COUNT

        const val CHANNEL_ID    = "ivanna_playback_channel"
        const val NOTIFICATION_ID = 2

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

    override fun onCreate() {
        super.onCreate()
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
        stopEngine()
        releaseWakeLock()
        retryHandler?.removeCallbacksAndMessages(null)
        retryThread?.quitSafely()
        _isCapturing.value = false
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

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
        val delayMs = when {
            retryAttempts < 3 -> 1_000L
            retryAttempts < 6 -> 5_000L
            else              -> 30_000L
        }
        retryAttempts++
        Log.i(TAG, "Reinicio #$retryAttempts en ${delayMs}ms")
        retryHandler?.postDelayed({ startEngine(savedProj) }, delayMs)
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
            // FIX: arrancar medición automática Lab (THD/LUFS/SNR cada 30s)
            IvannaLabMonitor.startAutoMeasure()
            voiceController = VoiceController(context)
            voiceProtection = VoiceProtectionController(context)
            spatialEngine.start()
            workerThread = HandlerThread("CaptureWorker", Process.THREAD_PRIORITY_URGENT_AUDIO)
            workerThread?.start()
            workerHandler = Handler(workerThread!!.looper)
            active = true
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
                val buffer = FloatArray(BLOCK_SAMPLES)
                val mono   = FloatArray(BLOCK_FRAMES)
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
                        val inL  = FloatArray(frames) { buffer[it * 2] }
                        val inR  = FloatArray(frames) { buffer[it * 2 + 1] }
                        val outL = FloatArray(frames)
                        val outR = FloatArray(frames)
                        runCatching {
                            IvannaSpatialEngine.shared.processStereoInput(inL, inR, outL, outR, frames)
                            for (i in 0 until frames) {
                                buffer[i * 2]     = outL[i]
                                buffer[i * 2 + 1] = outR[i]
                            }
                        }
                    }
                    IvannaBridgePlayer.activeInstance?.let { player ->
                        if (player.npeKotlinEnabled) {
                            runCatching { player.processBlockThroughNpeKotlin(buffer).copyInto(buffer) }
                        }
                    }
                    vibratoryProcessor.process(buffer)
                    writeAllToTrack(buffer, read)
                    if (IvannaNpeEngine.isReady) {
                        runCatching { IvannaNpeEngine.processInterleavedStereo(buffer, frames) }
                    }
                    runCatching { SpatialAudioEngineV2.feedCapturedBlock(buffer, frames) }
                    runCatching { voiceProtection?.feed(buffer, frames, SAMPLE_RATE) }
                    for (i in 0 until frames) mono[i] = (buffer[i * 2] + buffer[i * 2 + 1]) * 0.5f
                    runCatching { feedVoiceController(mono, frames) }
                    runCatching { IvannaVisualizerBridgeV2.processBlockFromNPE(mono, frames) }
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

        companion object {
            private const val TAG = "CaptureEngine"
        }
    }
}
