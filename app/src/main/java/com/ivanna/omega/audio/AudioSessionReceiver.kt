package com.ivanna.omega.audio

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.util.Log

class AudioSessionReceiver : BroadcastReceiver() {

    companion object {
        private const val TAG = "AudioSessionReceiver"
        const val ACTION_OPEN = "android.media.action.OPEN_AUDIO_EFFECT_CONTROL_SESSION"
        const val ACTION_CLOSE = "android.media.action.CLOSE_AUDIO_EFFECT_CONTROL_SESSION"
        const val EXTRA_SESSION = "android.media.extra.AUDIO_SESSION"
        const val EXTRA_PACKAGE = "android.media.extra.PACKAGE_NAME"
    }

    override fun onReceive(context: Context, intent: Intent) {
        val sessionId = intent.getIntExtra(EXTRA_SESSION, 0)
        val packageName = intent.getStringExtra(EXTRA_PACKAGE)
        val action = intent.action

        val app = try {
            context.applicationContext as? com.ivanna.omega.core.IVANNAApplication
        } catch (e: Exception) {
            Log.w(TAG, "No se pudo obtener IVANNAApplication: ${e.message}")
            null
        } ?: return

        when (action) {
            ACTION_OPEN -> {
                Log.i(TAG, "Sesión abierta: id=$sessionId pkg=$packageName")
                app.globalEffectManager.openSession(sessionId, packageName)
            }
            ACTION_CLOSE -> {
                Log.i(TAG, "Sesión cerrada: id=$sessionId")
                app.globalEffectManager.closeSession(sessionId)
            }
        }
    }
}
