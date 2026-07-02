package com.ivanna.omega.audio

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat
import com.ivanna.omega.MainActivity
import com.ivanna.omega.R
import com.ivanna.omega.dsp.DSPBridge

/**
 * AudioForegroundService — Servicio en primer plano para procesamiento de audio.
 *
 * FIX DE CONECTIVIDAD:
 *   Antes el servicio arrancaba AudioPipeline pero no inicializaba DSPBridge.
 *   Si la app se mataba y el servicio reiniciaba (START_STICKY), DSPBridge
 *   quedaba sin init → silencio en el procesamiento.
 *   Ahora llama DSPBridge.init() antes de arrancar la pipeline.
 */
class AudioForegroundService : Service() {

    companion object {
        const val CHANNEL_ID = "ivanna_audio_channel"
        const val NOTIFICATION_ID = 1
    }

    // FIX: se eliminó AudioPipeline (captura por mic) de este servicio.
    // Capturaba con MediaRecorder.AudioSource.DEFAULT mientras AudioTrack
    // reproducía por el altavoz al mismo tiempo -> el mic recapturaba la
    // salida ya procesada (feedback acústico) = sonido horrible, y además
    // arrancaba en onCreate() de MainActivity sin checar RECORD_AUDIO ->
    // AudioRecord lanzaba SecurityException -> crash si no había permiso.
    // La captura real ahora vive en PlaybackCaptureService (MediaProjection),
    // que lee el audio interno digitalmente, sin pasar por el micrófono.

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
        DSPBridge.init(48000)
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val notification = createNotification()
        startForeground(NOTIFICATION_ID, notification)
        return START_STICKY
    }

    override fun onDestroy() {
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                "IVANNA Audio Processing",
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = "Canal para procesamiento de audio en tiempo real"
                setSound(null, null)
                enableVibration(false)
            }
            val nm = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
            nm.createNotificationChannel(channel)
        }
    }

    private fun createNotification(): Notification {
        val pendingIntent = PendingIntent.getActivity(
            this, 0,
            Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_IMMUTABLE
        )
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("IVANNA OMEGA SUPREME")
            .setContentText("Procesamiento de audio activo")
            .setSmallIcon(R.drawable.ic_launcher_foreground)
            .setContentIntent(pendingIntent)
            .setOngoing(true)
            .build()
    }
}
