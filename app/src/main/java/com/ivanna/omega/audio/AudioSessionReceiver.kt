package com.ivanna.omega.audio

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.media.audiofx.AudioEffect
import android.util.Log

/**
 * AudioSessionReceiver — Receptor de sesiones de audio globales.
 *
 * FIX: Este receptor faltaba completamente. Sin él, IvannaGlobalEffectManager
 * nunca recibía las sesiones de otras apps (Spotify, YouTube, etc.) y los
 * efectos globales no se aplicaban.
 *
 * Funcionamiento (idéntico a Wavelet EQ y Poweramp Equalizer):
 *   1. Android emite OPEN_AUDIO_EFFECT_CONTROL_SESSION cuando cualquier app
 *      abre una sesión de audio.
 *   2. Este receptor captura el audioSessionId y el packageName de la app.
 *   3. Delega a IvannaGlobalEffectManager para aplicar el perfil activo.
 *   4. Cuando la sesión se cierra, libera los efectos sin memory leak.
 */
class AudioSessionReceiver : BroadcastReceiver() {

    companion object {
        private const val TAG = "AudioSessionReceiver"
    }

    override fun onReceive(context: Context, intent: Intent) {
        val sessionId = intent.getIntExtra(AudioEffect.EXTRA_AUDIO_SESSION, 0)
        val packageName = intent.getStringExtra("android.media.extra.PACKAGE_NAME")

        when (intent.action) {
            AudioEffect.ACTION_OPEN_AUDIO_EFFECT_CONTROL_SESSION -> {
                Log.i(TAG, "Nueva sesión de audio: id=$sessionId pkg=$packageName")
                // Acceder al manager global via Application
                val app = context.applicationContext
                if (app is com.ivanna.omega.core.IVANNAApplication) {
                    app.globalEffectManager.openSession(sessionId, packageName)
                }
            }
            AudioEffect.ACTION_CLOSE_AUDIO_EFFECT_CONTROL_SESSION -> {
                Log.i(TAG, "Cerrando sesión de audio: id=$sessionId")
                val app = context.applicationContext
                if (app is com.ivanna.omega.core.IVANNAApplication) {
                    app.globalEffectManager.closeSession(sessionId)
                }
            }
        }
    }
}
