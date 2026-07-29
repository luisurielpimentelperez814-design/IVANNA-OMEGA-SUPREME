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
import androidx.core.app.NotificationCompat
import com.ivanna.omega.R
import com.ivanna.omega.dsp.DSPBridge
import com.ivanna.omega.spatial.IvannaHeadTracker
import com.ivanna.omega.spatial.IvannaSpatialEngine

/**
 * AudioForegroundService — foreground service resistente a cambio de ventana.
 *
 * FIX v3.7:
 *   - PARTIAL_WAKE_LOCK con renovación periódica (evita muerte en background)
 *   - START_STICKY + rearranque idempotente
 *   - Persistencia forzada de sliders vía ParameterStore al detenerse
 */
class AudioForegroundService : Service() {

    companion object {
        const val CHANNEL_ID = "ivanna_audio_channel"
        const val NOTIFICATION_ID = 1
        private const val WAKELOCK_TAG = "ivanna:audio_fg"
        private const val WAKELOCK_TIMEOUT_MS = 10L * 60L * 1000L
    }

    private var audioPipeline: AudioPipeline? = null
    private var headTracker: IvannaHeadTracker? = null
    private var wakeLock: PowerManager.WakeLock? = null
    private var paramStore: ParameterStore? = null

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
        DSPBridge.init(96000)
        paramStore = ParameterStore(applicationContext)
        // Restaurar último estado ANTES de arrancar la pipeline
        AudioStateManager.updateState { paramStore!!.loadParameters() }
        acquireWakeLock()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        startForeground(NOTIFICATION_ID, createNotification())

        if (audioPipeline == null) {
            audioPipeline = AudioPipeline().apply { start(applicationContext) }
        }
        if (headTracker == null) {
            IvannaSpatialEngine.shared.init()
            val tracker = IvannaHeadTracker(applicationContext)
            tracker.init(); tracker.start()
            IvannaSpatialEngine.setHeadTracker(tracker)
            headTracker = tracker
        }
        // Renovar wakelock cada onStartCommand (redundancia útil)
        acquireWakeLock()
        return START_STICKY
    }

    override fun onDestroy() {
        // Persistir SIEMPRE antes de morir
        runCatching { paramStore?.saveParametersNow(AudioStateManager.audioState.value) }
        audioPipeline?.stop(); audioPipeline = null
        headTracker?.release(); headTracker = null
        releaseWakeLock()
        super.onDestroy()
    }

    override fun onTaskRemoved(rootIntent: Intent?) {
        // Usuario cerró la app desde recientes: NO matar el servicio,
        // solo persistir y reprogramar el foreground.
        runCatching { paramStore?.saveParametersNow(AudioStateManager.audioState.value) }
        val restart = Intent(applicationContext, AudioForegroundService::class.java)
        startForegroundService(restart)
        super.onTaskRemoved(rootIntent)
    }

    override fun onBind(intent: Intent?): IBinder? = null

    private fun acquireWakeLock() {
        if (wakeLock?.isHeld == true) return
        val pm = getSystemService(Context.POWER_SERVICE) as PowerManager
        wakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, WAKELOCK_TAG).apply {
            setReferenceCounted(false)
            acquire(WAKELOCK_TIMEOUT_MS)
        }
    }

    private fun releaseWakeLock() {
        runCatching { if (wakeLock?.isHeld == true) wakeLock?.release() }
        wakeLock = null
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                "IVANNA Audio Processing",
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = "Canal para procesamiento de audio en tiempo real"
                setSound(null, null); enableVibration(false)
            }
            (getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager)
                .createNotificationChannel(channel)
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
            .setSmallIcon(R.drawable.ic_launcher_foreground)
            .setContentIntent(pendingIntent)
            .setOngoing(true)
            .build()
    }
}
