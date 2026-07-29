package com.ivanna.omega.audio

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioPlaybackCaptureConfiguration
import android.media.AudioRecord
import android.media.AudioTrack
import android.media.projection.MediaProjection
import android.media.projection.MediaProjectionManager
import android.os.Build
import android.os.IBinder
import android.util.Log
import androidx.core.app.NotificationCompat
import com.ivanna.omega.R
import com.ivanna.omega.VoiceController
import com.ivanna.omega.dsp.DSPBridge
import com.ivanna.omega.magisk.OmegaEngineBridge
import com.ivanna.omega.neuromorphic.IvannaNpeEngine
import com.ivanna.omega.spatial.IvannaSpatialEngine
import com.ivanna.omega.visualizer.IvannaVisualizerBridgeV2
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * PlaybackCaptureService v4.0 — captura + DSP + reinyección
 *
 * Pipeline: AudioRecord (MediaProjection) → DSPBridge → IvannaSpatialEngine
 *           → NPE Kotlin → AudioTrack → análisis YAMNet/visualizador
 *
 * Funciona sobre cualquier app: Tidal, Qobuz, YouTube, Spotify, etc.
 */
class PlaybackCaptureService : Service() {

    companion object {
        private const val TAG = "PlaybackCaptureService"
        private const val SAMPLE_RATE = 48_000
        private const val CHANNEL_COUNT = 2
        private const val BLOCK_FRAMES = 512
        private const val BLOCK_SAMPLES = BLOCK_FRAMES * CHANNEL_COUNT

        private val _isCapturing = MutableStateFlow(false)
        val isCapturing: StateFlow<Boolean> = _isCapturing.asStateFlow()

        const val CHANNEL_ID = "ivanna_playback_channel"
        const val NOTIFICATION_ID = 2

        private const val VOICE_DECIMATION = 3
        private const val VOICE_WINDOW_SAMPLES = 15600
    }

    private val scope = CoroutineScope(Dispatchers.Default + SupervisorJob())
    private var audioRecord: AudioRecord? = null
    private var audioTrack: AudioTrack? = null
    private var isRunning = false
    private var mediaProjection: MediaProjection? = null
    private var projectionCallback: MediaProjection.Callback? = null
    private var voiceController: VoiceController? = null

    private val voiceWindow = FloatArray(VOICE_WINDOW_SAMPLES)
    private var voiceWindowFill = 0
    private var voiceDecimAcc = 0f
    private var voiceDecimCount = 0

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent == null) { stopSelf(); return START_NOT_STICKY }
        startForeground(
            NOTIFICATION_ID,
            createNotification(),
            android.content.pm.ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION
        )
        val projection = getMediaProjection(intent)
        if (projection == null) {
            Log.w(TAG, "MediaProjection no autorizada")
            stopForeground(STOP_FOREGROUND_REMOVE); stopSelf()
            return START_NOT_STICKY
        }
        startCapture(projection)
        return START_NOT_STICKY
    }

    override fun onDestroy() {
        stopCapture()
        _isCapturing.value = false
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    private fun startCapture(projection: MediaProjection) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) return
        try {
            mediaProjection = projection
            val cb = object : MediaProjection.Callback() {
                override fun onStop() { stopCapture(); stopSelf() }
            }
            projectionCallback = cb
            projection.registerCallback(cb, null)

            val captureConfig = AudioPlaybackCaptureConfiguration.Builder(projection)
                .addMatchingUsage(AudioAttributes.USAGE_MEDIA)
                .excludeUid(android.os.Process.myUid())
                .build()

            val minRec = AudioRecord.getMinBufferSize(
                SAMPLE_RATE, AudioFormat.CHANNEL_IN_STEREO, AudioFormat.ENCODING_PCM_FLOAT
            ).coerceAtLeast(BLOCK_SAMPLES * 4)

            audioRecord = AudioRecord.Builder()
                .setAudioFormat(
                    AudioFormat.Builder()
                        .setSampleRate(SAMPLE_RATE)
                        .setEncoding(AudioFormat.ENCODING_PCM_FLOAT)
                        .setChannelMask(AudioFormat.CHANNEL_IN_STEREO)
                        .build()
                )
                .setBufferSizeInBytes(minRec)
                .setAudioPlaybackCaptureConfig(captureConfig)
                .build()

            if (audioRecord?.state != AudioRecord.STATE_INITIALIZED) {
                Log.e(TAG, "AudioRecord no inicializó"); audioRecord?.release()
                audioRecord = null; stopSelf(); return
            }

            val minTrack = AudioTrack.getMinBufferSize(
                SAMPLE_RATE, AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_FLOAT
            ).coerceAtLeast(BLOCK_SAMPLES * 4)

            audioTrack = AudioTrack.Builder()
                .setAudioAttributes(
                    AudioAttributes.Builder()
                        .setUsage(AudioAttributes.USAGE_MEDIA)
                        .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                        .build()
                )
                .setAudioFormat(
                    AudioFormat.Builder()
                        .setSampleRate(SAMPLE_RATE)
                        .setEncoding(AudioFormat.ENCODING_PCM_FLOAT)
                        .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO)
                        .build()
                )
                .setBufferSizeInBytes(minTrack)
                .setTransferMode(AudioTrack.MODE_STREAM)
                .setPerformanceMode(AudioTrack.PERFORMANCE_MODE_LOW_LATENCY)
                .build()

            if (audioTrack?.state != AudioTrack.STATE_INITIALIZED) {
                Log.e(TAG, "AudioTrack no inicializó")
                audioTrack?.release(); audioTrack = null
                audioRecord?.release(); audioRecord = null
                stopSelf(); return
            }

            IvannaVisualizerBridgeV2.init(SAMPLE_RATE, BLOCK_FRAMES)
            if (voiceController == null) voiceController = VoiceController(applicationContext)

            audioRecord?.startRecording()
            audioTrack?.play()
            isRunning = true
            _isCapturing.value = true
            Log.i(TAG, "Pipeline activo @ ${SAMPLE_RATE}Hz")

            scope.launch {
                val buffer = FloatArray(BLOCK_SAMPLES)
                val mono   = FloatArray(BLOCK_FRAMES)

                while (isRunning && isActive) {
                    val read = audioRecord?.read(
                        buffer, 0, BLOCK_SAMPLES, AudioRecord.READ_BLOCKING
                    ) ?: 0
                    if (read <= 0) continue
                    val frames = read / CHANNEL_COUNT

                    // 1. EQ / Compresor / Exciter / Widener
                    DSPBridge.process(buffer, frames)

                    // 2. HRTF binaural
                    if (IvannaSpatialEngine.enabled) {
                        val inL  = FloatArray(frames) { buffer[it * 2] }
                        val inR  = FloatArray(frames) { buffer[it * 2 + 1] }
                        val outL = FloatArray(frames)
                        val outR = FloatArray(frames)
                        IvannaSpatialEngine.shared.processStereoInput(inL, inR, outL, outR, frames)
                        for (i in 0 until frames) {
                            buffer[i * 2]     = outL[i]
                            buffer[i * 2 + 1] = outR[i]
                        }
                    }

                    // 3. NPE Kotlin
                    IvannaBridgePlayer.activeInstance?.let { player ->
                        if (player.npeKotlinEnabled) {
                            runCatching {
                                player.processBlockThroughNpeKotlin(buffer).copyInto(buffer)
                            }
                        }
                    }

                    // 4. Reinyección
                    audioTrack?.write(buffer, 0, read, AudioTrack.WRITE_BLOCKING)

                    // 5. Análisis neuromórfico (no altera señal)
                    if (IvannaNpeEngine.isReady) {
                        runCatching { IvannaNpeEngine.processInterleavedStereo(buffer, frames) }
                    }

                    // 6. Downmix mono → VoiceController + Visualizador
                    for (i in 0 until frames) {
                        mono[i] = (buffer[i * 2] + buffer[i * 2 + 1]) * 0.5f
                    }
                    runCatching { feedVoiceController(mono, frames) }
                    IvannaVisualizerBridgeV2.processBlockFromNPE(mono, frames)
                }
            }
        } catch (e: SecurityException) {
            Log.e(TAG, "SecurityException — falta RECORD_AUDIO", e)
            cleanup(); stopSelf()
        } catch (e: Exception) {
            Log.e(TAG, "Error iniciando captura", e)
            cleanup(); stopSelf()
        }
    }

    private fun stopCapture() {
        isRunning = false
        _isCapturing.value = false
        scope.cancel()
        cleanup()
        voiceWindowFill = 0; voiceDecimAcc = 0f; voiceDecimCount = 0
    }

    private fun cleanup() {
        audioTrack?.stop(); audioTrack?.release(); audioTrack = null
        audioRecord?.stop(); audioRecord?.release(); audioRecord = null
        IvannaVisualizerBridgeV2.release()
        projectionCallback?.let { mediaProjection?.unregisterCallback(it) }
        projectionCallback = null; mediaProjection = null
    }

    private fun feedVoiceController(mono: FloatArray, numFrames: Int) {
        val vc = voiceController ?: return
        for (i in 0 until numFrames) {
            voiceDecimAcc += mono[i]; voiceDecimCount++
            if (voiceDecimCount >= VOICE_DECIMATION) {
                if (voiceWindowFill < voiceWindow.size)
                    voiceWindow[voiceWindowFill++] = voiceDecimAcc / voiceDecimCount
                voiceDecimAcc = 0f; voiceDecimCount = 0
            }
        }
        if (voiceWindowFill >= voiceWindow.size) {
            val (hint, scores) = vc.processAudioWithScores(voiceWindow)
            OmegaEngineBridge.pushYamnetScores(
                speech = scores.speech, music = scores.music,
                classId = 0, confidence = maxOf(scores.speech, scores.music)
            )
            if (hint != "none") vc.executeCommand(hint)
            voiceWindowFill = 0
        }
    }

    private fun getMediaProjection(intent: Intent?): MediaProjection? {
        val code = intent?.getIntExtra("resultCode", -1) ?: return null
        val data = intent.getParcelableExtra<Intent>("data") ?: return null
        return (getSystemService(Context.MEDIA_PROJECTION_SERVICE) as MediaProjectionManager)
            .getMediaProjection(code, data)
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val ch = NotificationChannel(
                CHANNEL_ID, "IVANNA Playback Capture",
                NotificationManager.IMPORTANCE_LOW
            ).apply { setSound(null, null); enableVibration(false) }
            (getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager)
                .createNotificationChannel(ch)
        }
    }

    private fun createNotification(): Notification {
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
}
