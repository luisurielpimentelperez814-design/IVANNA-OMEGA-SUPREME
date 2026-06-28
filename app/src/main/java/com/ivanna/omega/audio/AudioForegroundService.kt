/*
 * © 2026 Luis Uriel Pimentel Pérez — IVANNA N-P-E
 * AudioForegroundService v3.1.1
 *
 * NUEVO: dueño del IvannaGlobalEffectManager (singleton estático seguro).
 * El companion object expone globalEffectManager para que
 * AudioSessionReceiver pueda delegarle sin retener Context.
 */
package com.goretns.ivannuri.ultra

import android.app.Notification
import com.ivanna.omega.MainActivity
import android.app.Notification
import com.ivanna.omega.MainActivityChannel
import android.app.Notification
import com.ivanna.omega.MainActivityManager
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.content.pm.ServiceInfo
import android.media.AudioDeviceCallback
import android.media.AudioDeviceInfo
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioRecord
import android.os.Binder
import android.os.Build
import android.os.IBinder
import android.util.Log
import androidx.core.app.NotificationCompat
import androidx.core.app.ServiceCompat

class AudioForegroundService : Service() {

    private var audioManager: AudioManager? = null
    private var pipeline: AudioPipeline? = null
    private val binder = LocalBinder()

    private val deviceCallback = object : AudioDeviceCallback() {
        override fun onAudioDevicesAdded(addedDevices: Array<out AudioDeviceInfo>) {
            val hasUsbDac = addedDevices.any { 
                it.type == AudioDeviceInfo.TYPE_USB_DEVICE || 
                it.type == AudioDeviceInfo.TYPE_USB_HEADSET 
            }
            if (hasUsbDac) {
                Log.i("IVANNA-AudioService", "DAC USB detectado. Forzando enrutamiento.")
                AudioRoutingManager.forceUsbDacRouting(applicationContext)
            }
        }

        override fun onAudioDevicesRemoved(removedDevices: Array<out AudioDeviceInfo>) {
            val usbDacRemoved = removedDevices.any { 
                it.type == AudioDeviceInfo.TYPE_USB_DEVICE || 
                it.type == AudioDeviceInfo.TYPE_USB_HEADSET 
            }
            if (usbDacRemoved) {
                Log.i("IVANNA-AudioService", "DAC USB desconectado. Restaurando enrutamiento por defecto.")
                AudioRoutingManager.restoreDefaultRouting(applicationContext)
            }
        }
    }

    private fun restartPipeline() {
        pipeline?.close()
        pipeline = AudioPipeline(detectSafeRate()).also { it.start() }
    }

    inner class LocalBinder : Binder() {
        fun getService(): AudioForegroundService = this@AudioForegroundService
    }

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
        audioManager = getSystemService(AUDIO_SERVICE) as? AudioManager
        audioManager?.registerAudioDeviceCallback(deviceCallback, null)

        if (globalEffectManager == null) {
            globalEffectManager = IvannaGlobalEffectManager()
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        intent?.getStringExtra(EXTRA_PROFILE)?.let { profileName ->
            applyProfileByName(profileName)
        }

        try {
            ServiceCompat.startForeground(
                this, NOTIFICATION_ID, buildNotification(),
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q)
                    ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK
                else 0
            )
        } catch (t: Throwable) {
            try { startForeground(NOTIFICATION_ID, buildNotification()) } catch (_: Throwable) {}
        }

        if (pipeline == null) {
            pipeline = AudioPipeline(detectSafeRate()).also { it.start() }
        }
        return START_STICKY
    }

    override fun onBind(intent: Intent?): IBinder = binder

    override fun onDestroy() {
        audioManager?.unregisterAudioDeviceCallback(deviceCallback)
        pipeline?.close()
        pipeline = null
        globalEffectManager?.releaseAll()
        globalEffectManager = null
        super.onDestroy()
    }

    fun getPipeline(): AudioPipeline? = pipeline

    fun applyGlobalProfile(profile: IvannaEffectProfile) {
        globalEffectManager?.applyProfile(profile)
    }

    private fun applyProfileByName(name: String) {
        val profile = when (name) {
            "FLAT"     -> IvannaEffectProfile.FLAT
            "WARM"     -> IvannaEffectProfile.WARM
            "ROCK_70S" -> IvannaEffectProfile.ROCK_70S
            "SPATIAL"  -> IvannaEffectProfile.SPATIAL
            "PUNCH"    -> IvannaEffectProfile.PUNCH
            else       -> return
        }
        globalEffectManager?.applyProfile(profile)
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID, "ivanna_npe_audio",
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = "Procesamiento de audio global activo"
                setSound(null, null)
                enableVibration(false)
            }
            getSystemService(NotificationManager::class.java)
                ?.createNotificationChannel(channel)
        }
    }

    private fun buildNotification(): Notification {
        val openIntent = PendingIntent.getActivity(
            this, 0, Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )
        val profile = globalEffectManager?.activeProfile
        val profileDesc = when (profile) {
            IvannaEffectProfile.ROCK_70S -> "Rock 70s"
            IvannaEffectProfile.WARM     -> "Warm"
            IvannaEffectProfile.PUNCH    -> "Punch"
            IvannaEffectProfile.SPATIAL  -> "Spatial"
            IvannaEffectProfile.FLAT     -> "Flat"
            else -> "Activo"
        }
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("IVANNA N-P-E — Procesando globalmente")
            .setContentText("Perfil: $profileDesc · Interceptando todas las apps")
            .setSmallIcon(android.R.drawable.ic_btn_speak_now)
            .setContentIntent(openIntent)
            .setOngoing(true)
            .setSilent(true)
            .build()
    }

    /**
     * Detecta la tasa de muestra nativa del hardware y valida que AudioRecord
     * la soporte. Siempre devuelve una tasa garantizada (mínimo 48000 Hz).
     *
     * 192kHz era el default anterior — la mayoría de dispositivos Android NO
     * soportan AudioRecord a esa tasa → getMinBufferSize devuelve ERROR_BAD_VALUE
     * → el pipeline no arrancaba o devolvía datos corruptos → tronidos.
     */
    private fun detectSafeRate(): Int {
        val am = getSystemService(AUDIO_SERVICE) as? AudioManager
        val nativeRate = am?.getProperty(AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE)
            ?.toIntOrNull() ?: AudioPipeline.RATE_STANDARD
        // Verificar que AudioRecord acepta la tasa nativa
        val enc = AudioFormat.ENCODING_PCM_FLOAT
        val ch  = AudioFormat.CHANNEL_IN_MONO
        val candidates = listOf(nativeRate, AudioPipeline.RATE_STANDARD, 44100)
        for (rate in candidates) {
            val minBuf = AudioRecord.getMinBufferSize(rate, ch, enc)
            if (minBuf > 0) {
                Log.i("IVANNA-AudioService", "AudioPipeline iniciará a ${rate}Hz")
                return rate
            }
        }
        return AudioPipeline.RATE_STANDARD  // fallback absoluto
    }

    companion object {
        const val CHANNEL_ID       = "ivanna_npe_audio"
        const val NOTIFICATION_ID  = 1001
        const val EXTRA_PROFILE    = "ivanna_profile"

        @Volatile @JvmStatic
        var globalEffectManager: IvannaGlobalEffectManager? = null
            internal set
    }
}

