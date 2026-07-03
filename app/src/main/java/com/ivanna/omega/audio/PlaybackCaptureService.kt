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
import android.media.AudioManager
import android.media.AudioPlaybackCaptureConfiguration
import android.media.AudioRecord
import android.media.AudioTrack
import android.media.projection.MediaProjection
import android.media.projection.MediaProjectionManager
import android.Manifest
import android.content.pm.PackageManager
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat
import androidx.core.content.ContextCompat
import com.ivanna.omega.MainActivity
import com.ivanna.omega.R
import com.ivanna.omega.core.ParameterStore
import com.ivanna.omega.dsp.DSPBridge
import com.ivanna.omega.neuromorphic.IvannaNpeEngine
import com.ivanna.omega.visualizer.IvannaVisualizerBridge
import kotlinx.coroutines.*

/**
 * PlaybackCaptureService — Servicio de captura de audio de reproducción.
 * Versión corregida con sintaxis válida.
 */
class PlaybackCaptureService : Service() {

    companion object {
        const val CHANNEL_ID = "ivanna_playback_channel"
        const val NOTIFICATION_ID = 2
        const val INPUT_SAMPLES = 2048
    }

    private val scope = CoroutineScope(Dispatchers.Default + SupervisorJob())
    private var audioRecord: AudioRecord? = null
    private var audioTrack: AudioTrack? = null
    private var isRunning = false
    private var savedMusicVolume: Int? = null

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
        DSPBridge.init(48000)
        IvannaNpeEngine.init(48000, INPUT_SAMPLES / 2)
        IvannaVisualizerBridge.init(48000, INPUT_SAMPLES / 2)
        val params = ParameterStore(this)
        IvannaNpeEngine.setBypass(params.isNpeBypass())
        IvannaNpeEngine.setEngineFlags(
            params.isNpeHrtfEnabled(), params.isNpeCochlearEnabled(), params.isNpeAdaptEnabled()
        )
        IvannaNpeEngine.setNeuroParams(
            params.getNpeHarmonicGain(), params.getNpeLateralInhib(),
            params.getNpeOhcCompression(), params.getNpeMasterGainDb()
        )
        IvannaNpeEngine.setAGC(params.getNpeAgcTargetDb(), params.getNpeAgcRate())
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val notification = createNotification()
        startForeground(NOTIFICATION_ID, notification)

        // [FIX-CRASH] Guard de permiso RECORD_AUDIO antes de tocar AudioRecord.
        // AudioPlaybackCaptureConfiguration sigue exigiendo este permiso a nivel
        // de plataforma aunque no se use el micrófono físico. Sin este guard,
        // AudioRecord.Builder().build() lanza UnsupportedOperationException
        // dentro de onStartCommand() -> excepción no capturada en un Service
        // -> crash de la app completa.
        val hasRecordPermission = ContextCompat.checkSelfPermission(
            this, Manifest.permission.RECORD_AUDIO
        ) == PackageManager.PERMISSION_GRANTED

        if (!hasRecordPermission) {
            stopSelf()
            return START_NOT_STICKY
        }

        val mediaProjection = getMediaProjection(intent)
        if (mediaProjection != null) {
            try {
                startCapture(mediaProjection)
            } catch (e: Exception) {
                // [FIX-CRASH] Cualquier fallo de inicialización de AudioRecord/
                // AudioTrack (bufferSize inválido, hardware no estándar, permiso
                // revocado justo después del check, etc.) se contiene aquí en
                // vez de propagarse y matar la app.
                stopCapture()
                stopSelf()
            }
        } else {
            stopSelf()
        }

        return START_STICKY
    }

    override fun onDestroy() {
        stopCapture()
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    private fun getMediaProjection(intent: Intent?): MediaProjection? {
        val resultCode = intent?.getIntExtra("resultCode", -1) ?: return null
        val data = intent.getParcelableExtra<Intent>("data") ?: return null
        val projectionManager = getSystemService(Context.MEDIA_PROJECTION_SERVICE) as MediaProjectionManager
        return projectionManager.getMediaProjection(resultCode, data)
    }

    private fun startCapture(mediaProjection: MediaProjection) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) return

        val config = AudioPlaybackCaptureConfiguration.Builder(mediaProjection)
            .addMatchingUsage(AudioAttributes.USAGE_MEDIA)
            .build()

        val minBuf = AudioRecord.getMinBufferSize(
            48000,
            AudioFormat.CHANNEL_IN_STEREO,
            AudioFormat.ENCODING_PCM_FLOAT
        )
        // [FIX-CRASH] getMinBufferSize puede devolver ERROR_BAD_VALUE (-2) o
        // ERROR (-1) en hardware no estándar. Pasar un tamaño negativo a
        // setBufferSizeInBytes lanza IllegalArgumentException sin capturar.
        if (minBuf <= 0) {
            throw IllegalStateException("AudioRecord.getMinBufferSize inválido ($minBuf) para 48kHz/stereo/float")
        }
        val bufferSize = minBuf

        audioRecord = AudioRecord.Builder()
            .setAudioFormat(
                AudioFormat.Builder()
                    .setSampleRate(48000)
                    .setEncoding(AudioFormat.ENCODING_PCM_FLOAT)
                    .setChannelMask(AudioFormat.CHANNEL_IN_STEREO)
                    .build()
            )
            .setBufferSizeInBytes(bufferSize)
            .setAudioPlaybackCaptureConfig(config)
            .build()

        // [FIX-CRASH] Igual que SystemAudioCapture.kt: si AudioRecord no llega a
        // STATE_INITIALIZED (permiso revocado, política de audio, etc.) hay que
        // abortar aquí en vez de llamar startRecording() sobre un objeto inválido
        // (eso lanza IllegalStateException más abajo, sin contexto claro).
        if (audioRecord?.state != AudioRecord.STATE_INITIALIZED) {
            throw IllegalStateException("AudioRecord no se pudo inicializar (permiso o política de audio)")
        }

        // FIX: silencia el stream de música original (la fuente sigue
        // generando PCM y se sigue capturando arriba, solo se apaga su
        // salida al altavoz) y saca el audio procesado por un stream
        // distinto (ACCESSIBILITY) que no se ve afectado por ese mute.
        // Resultado: solo se escucha el audio ya procesado por IVANNA.
        val audioManager = getSystemService(Context.AUDIO_SERVICE) as AudioManager
        savedMusicVolume = audioManager.getStreamVolume(AudioManager.STREAM_MUSIC)
        audioManager.setStreamVolume(AudioManager.STREAM_MUSIC, 0, 0)

        audioTrack = AudioTrack.Builder()
            .setAudioAttributes(
                AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_ASSISTANCE_ACCESSIBILITY)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                    .setAllowedCapturePolicy(AudioAttributes.ALLOW_CAPTURE_BY_NONE)
                    .build()
            )
            .setAudioFormat(
                AudioFormat.Builder()
                    .setSampleRate(48000)
                    .setEncoding(AudioFormat.ENCODING_PCM_FLOAT)
                    .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO)
                    .build()
            )
            .setBufferSizeInBytes(bufferSize)
            .setTransferMode(AudioTrack.MODE_STREAM)
            .build()

        // [FIX-CRASH] Mismo principio que AudioRecord: si AudioTrack no inicializa
        // (ruta de salida ASSISTANCE_ACCESSIBILITY no disponible en el dispositivo,
        // por ejemplo), abortar limpiamente en vez de reproducir sobre un track roto.
        if (audioTrack?.state != AudioTrack.STATE_INITIALIZED) {
            throw IllegalStateException("AudioTrack no se pudo inicializar")
        }

        audioRecord?.startRecording()
        audioTrack?.play()
        isRunning = true

        scope.launch {
            val buffer = FloatArray(INPUT_SAMPLES)
            val mono = FloatArray(INPUT_SAMPLES / 2)
            while (isRunning && isActive) {
                val read = audioRecord?.read(buffer, 0, buffer.size, AudioRecord.READ_BLOCKING) ?: 0
                if (read > 0) {
                    val frames = read / 2
                    DSPBridge.process(buffer, frames)
                    IvannaNpeEngine.processInterleavedStereo(buffer, frames)
                    for (i in 0 until frames) {
                        mono[i] = 0.5f * (buffer[i * 2] + buffer[i * 2 + 1])
                    }
                    IvannaVisualizerBridge.processBlock(mono, frames)
                    audioTrack?.write(buffer, 0, read, AudioTrack.WRITE_BLOCKING)
                }
            }
        }
    }

    private fun stopCapture() {
        isRunning = false
        scope.cancel()
        IvannaVisualizerBridge.release()
        audioRecord?.stop()
        audioRecord?.release()
        audioRecord = null
        audioTrack?.stop()
        audioTrack?.release()
        audioTrack = null
        IvannaNpeEngine.release()
        savedMusicVolume?.let {
            val audioManager = getSystemService(Context.AUDIO_SERVICE) as AudioManager
            audioManager.setStreamVolume(AudioManager.STREAM_MUSIC, it, 0)
        }
        savedMusicVolume = null
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                "IVANNA Playback Capture",
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = "Canal para captura de audio de reproducción"
                setSound(null, null)
                enableVibration(false)
            }

            val notificationManager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
            notificationManager.createNotificationChannel(channel)
        }
    }

    private fun createNotification(): Notification {
        val pendingIntent = PendingIntent.getActivity(
            this,
            0,
            Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_IMMUTABLE
        )

        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("IVANNA OMEGA SUPREME")
            .setContentText("Captura de audio activa")
            .setSmallIcon(R.drawable.ic_launcher_foreground)
            .setContentIntent(pendingIntent)
            .setOngoing(true)
            .build()
    }
}
