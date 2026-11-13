package com.ivanna.omega.audio

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import android.util.Log
import androidx.core.app.NotificationCompat
import androidx.core.content.ContextCompat
import com.ivanna.omega.R

/**
 * BridgePlaybackService — servicio en primer plano (mediaPlayback) para el
 * reproductor propio (IvannaBridgePlayer: Tidal y archivos locales).
 *
 * PROBLEMA (v2.5.438, reporte Esse 2026-11-13): el BridgePlayer vive en
 * MainActivity, NO en un servicio en primer plano. El servicio en primer
 * plano que existía (PlaybackCaptureService, mediaProjection) solo cubre
 * la CAPTURA, no la reproducción del BridgePlayer. Al cambiar a otra app,
 * IVANNA pasa a segundo plano y Android estrangula el proceso: aunque el
 * hilo de audio tenga prioridad URGENT_AUDIO, no alcanza a llenar el
 * buffer → underruns → el tronido-metralleta. Dentro de la app no truena.
 *
 * FIX: este servicio NO posee el reproductor (sigue en MainActivity); su
 * único trabajo es mantener el proceso en primer plano mientras suena el
 * BridgePlayer, para que Android no estrangule el hilo de audio al
 * cambiar de app. Un servicio en primer plano eleva TODO el proceso, no
 * solo sus propios hilos.
 *
 * CICLO DE VIDA: MainActivity lo arranca cuando el estado del reproductor
 * es PLAYING y lo detiene en PAUSED/STOPPED/IDLE/ERROR y al liberar el
 * reproductor (DisposableEffect onDispose). Solo corre mientras suena.
 * Arranque idempotente (flag isRunning) y con runCatching: en Android 12+
 * arrancar un FGS desde segundo plano está restringido; si el arranque
 * falla (p. ej. reanudar desde un botón del auricular con la app en
 * segundo plano) se registra y no se rompe nada — el bucle de sondeo de
 * MainActivity reintenta cada 100 ms y arranca en cuanto se permite.
 */
class BridgePlaybackService : Service() {

    companion object {
        private const val TAG = "BridgePlaybackService"
        const val CHANNEL_ID = "ivanna_bridge_playback_channel"
        const val NOTIFICATION_ID = 4

        @Volatile private var isRunning = false

        /** Arranca el servicio en primer plano (idempotente). */
        fun start(context: Context) {
            if (isRunning) return
            runCatching {
                val intent = Intent(context, BridgePlaybackService::class.java)
                ContextCompat.startForegroundService(context, intent)
            }.onFailure { e ->
                Log.w(TAG, "No se pudo arrancar el servicio en primer plano", e)
            }
        }

        /** Detiene el servicio (idempotente). */
        fun stop(context: Context) {
            if (!isRunning) return
            runCatching {
                context.stopService(Intent(context, BridgePlaybackService::class.java))
            }.onFailure { e ->
                Log.w(TAG, "No se pudo detener el servicio en primer plano", e)
            }
        }
    }

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        startForeground(
            NOTIFICATION_ID, buildNotification(),
            ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK
        )
        isRunning = true
        Log.i(TAG, "Servicio en primer plano activo (mediaPlayback)")
        return START_STICKY
    }

    override fun onDestroy() {
        isRunning = false
        Log.i(TAG, "Servicio en primer plano detenido")
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val ch = NotificationChannel(
                CHANNEL_ID, "IVANNA Reproducción", NotificationManager.IMPORTANCE_LOW
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
            .setContentText("Reproduciendo — el audio sigue en primer plano")
            .setSmallIcon(R.drawable.ic_launcher_foreground)
            .setContentIntent(pi)
            .setOngoing(true)
            .build()
    }
}
