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
import android.media.projection.MediaProjection
import android.media.projection.MediaProjectionManager
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat
import com.ivanna.omega.MainActivity
import com.ivanna.omega.R
import com.ivanna.omega.neuromorphic.IvannaNpeEngine
import com.ivanna.omega.visualizer.IvannaVisualizerBridgeV2
import kotlinx.coroutines.*

/**
 * PlaybackCaptureService — Servicio de captura de audio de reproducción.
 *
 * FIX v3.0: conecta el visualizador al loop de audio REAL. Antes el buffer
 * capturado se descartaba por completo ("// Procesar buffer capturado" sin
 * cuerpo) — el handle nativo del visualizador (IvannaVisualizerBridgeV2)
 * nunca se creaba ni se alimentaba, así que VisualizerRendererV2 siempre
 * leía ceros: la UI del visualizer se abría pero mostraba silencio.
 *
 * Pipeline real por bloque capturado:
 *   AudioRecord (MediaProjection, stereo interleaved)
 *     → IvannaNpeEngine.processInterleavedStereo()  (análisis neuromórfico:
 *       alimenta género/RMS/AGC/confianza que la UI ya lee por polling)
 *     → downmix a mono
 *     → IvannaVisualizerBridgeV2.processBlockFromNPE() (bandas espectrales
 *       para el shader GLSL del fondo Aurora)
 *
 * Nota: esta captura es sólo de LECTURA/ANÁLISIS — no se reinyecta a la
 * salida de audio (evitaría eco/duplicado). El procesamiento audible real
 * ocurre vía IvannaGlobalEffectManager (AudioEffect en la sesión de la app
 * fuente) o, para IvannaBridgePlayer, vía AudioEngine/DSPBridge.
 */
class PlaybackCaptureService : Service() {

    companion object {
        const val CHANNEL_ID = "ivanna_playback_channel"
        const val NOTIFICATION_ID = 2
        const val INPUT_SAMPLES = 2048
    }

    private val scope = CoroutineScope(Dispatchers.Default + SupervisorJob())
    private var audioRecord: AudioRecord? = null
    private var isRunning = false

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val notification = createNotification()
        startForeground(NOTIFICATION_ID, notification)

        val mediaProjection = getMediaProjection(intent)
        if (mediaProjection != null) {
            startCapture(mediaProjection)
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

        val sampleRate = 48000

        val config = AudioPlaybackCaptureConfiguration.Builder(mediaProjection)
            .addMatchingUsage(AudioAttributes.USAGE_MEDIA)
            .build()

        val bufferSize = AudioRecord.getMinBufferSize(
            sampleRate,
            AudioFormat.CHANNEL_IN_STEREO,
            AudioFormat.ENCODING_PCM_FLOAT
        )

        audioRecord = AudioRecord.Builder()
            .setAudioFormat(
                AudioFormat.Builder()
                    .setSampleRate(sampleRate)
                    .setEncoding(AudioFormat.ENCODING_PCM_FLOAT)
                    .setChannelMask(AudioFormat.CHANNEL_IN_STEREO)
                    .build()
            )
            .setBufferSizeInBytes(bufferSize)
            .setAudioPlaybackCaptureConfig(config)
            .build()

        // FIX: handle nativo del visualizador — maxBlockFrames en FRAMES por
        // canal (INPUT_SAMPLES es el tamaño del array intercalado L,R,L,R,
        // así que el máximo de frames por canal es la mitad).
        IvannaVisualizerBridgeV2.init(sampleRate, INPUT_SAMPLES / 2)
        // bufferSize está en bytes; frame estéreo float = 2 canales * 4 bytes
        val estimatedLatencyMs = (bufferSize.toFloat() / 8f) / sampleRate * 1000f
        IvannaVisualizerBridgeV2.setDeviceLatencyMs(estimatedLatencyMs)

        audioRecord?.startRecording()
        isRunning = true

        scope.launch {
            val buffer = FloatArray(INPUT_SAMPLES)
            val mono = FloatArray(INPUT_SAMPLES / 2)
            while (isRunning && isActive) {
                val read = audioRecord?.read(buffer, 0, buffer.size, AudioRecord.READ_BLOCKING) ?: 0
                if (read > 0) {
                    // read son muestras intercaladas L,R; frames por canal = read/2
                    val numFrames = read / 2
                    if (numFrames > 0) {
                        // Análisis neuromórfico (género/RMS/AGC/confianza) —
                        // in-place, no altera el audio real de salida.
                        if (IvannaNpeEngine.isReady) {
                            IvannaNpeEngine.processInterleavedStereo(buffer, numFrames)
                        }
                        // Downmix a mono para el visualizador
                        for (i in 0 until numFrames) {
                            mono[i] = 0.5f * (buffer[i * 2] + buffer[i * 2 + 1])
                        }
                        IvannaVisualizerBridgeV2.processBlockFromNPE(mono, numFrames)
                    }
                }
            }
        }
    }

    private fun stopCapture() {
        isRunning = false
        scope.cancel()
        audioRecord?.stop()
        audioRecord?.release()
        audioRecord = null
        IvannaVisualizerBridgeV2.release()
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
