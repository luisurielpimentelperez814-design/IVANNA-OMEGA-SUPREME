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
import android.os.PowerManager
import android.util.Log
import androidx.core.app.NotificationCompat
import com.ivanna.omega.R

class AudioForegroundService : Service() {

    companion object {
        const val CHANNEL_ID = "ivanna_audio_channel"
        const val NOTIFICATION_ID = 1
        private const val TAG = "IvannaFgSvc"
    }

    private lateinit var wakeLock: PowerManager.WakeLock

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
        val powerManager = getSystemService(Context.POWER_SERVICE) as PowerManager
        wakeLock = powerManager.newWakeLock(
            PowerManager.PARTIAL_WAKE_LOCK,
            "ivanna:audio_processing"
        )
        wakeLock.setReferenceCounted(false)
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (!wakeLock.isHeld) {
            wakeLock.acquire()
        }
        startForeground(NOTIFICATION_ID, createNotification())
        // Reinicio automático si el sistema mata el servicio
        return START_STICKY
    }

    override fun onTaskRemoved(rootIntent: Intent?) {
        // AUDIT FIX (invalid background restart): antes se llamaba
        //   startService(Intent(this, this::class.java))
        // desde onTaskRemoved. A partir de Android 8 (API 26) el sistema
        // considera este contexto como "background" en muchos escenarios
        // (task swipe con app ya en fondo) y lanza
        // IllegalStateException / BackgroundServiceStartNotAllowedException,
        // matando el proceso justo cuando queremos persistir el servicio.
        //
        // La persistencia real la sigue dando START_STICKY (onStartCommand):
        // si el sistema termina el servicio, se re-crea automáticamente.
        // Aquí NO se puede (ni debe) arrancar un nuevo Service desde fondo;
        // basta con propagar el evento al ciclo de vida estándar.
        //
        // Se mantiene el comportamiento pre-existente de propagar
        // super.onTaskRemoved() para no romper el diseño funcional.
        try {
            super.onTaskRemoved(rootIntent)
        } catch (t: Throwable) {
            Log.w(TAG, "onTaskRemoved super() threw: ${t.message}")
        }
        // Nota: NO llamamos startService/startForegroundService aquí.
        // START_STICKY + notificación foreground activa mantienen la
        // persistencia del servicio de forma compatible con Android 8-14.
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onDestroy() {
        if (wakeLock.isHeld) wakeLock.release()
        super.onDestroy()
    }

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
            val manager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
            manager.createNotificationChannel(channel)
        }
    }

    private fun createNotification(): Notification {
        val pendingIntent = PendingIntent.getActivity(
            this, 0,
            Intent(this, Class.forName("com.ivanna.omega.MainActivity")),
            PendingIntent.FLAG_IMMUTABLE
        )
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("IVANNA OMEGA SUPREME")
            .setContentText("Procesamiento de audio activo")
            // Ajusta el icono si tu proyecto no usa el foreground estándar
            .setSmallIcon(R.drawable.ic_launcher_foreground)
            .setContentIntent(pendingIntent)
            .setOngoing(true)
            .build()
    }
}
