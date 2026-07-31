package com.ivanna.omega.audio

import android.content.Context
import android.media.session.MediaController
import android.media.session.MediaSessionManager
import android.os.Build
import android.util.Log
import com.ivanna.omega.core.IVANNAApplication
import com.ivanna.omega.ai.UserProfileManager as AiUserProfileManager
import kotlinx.coroutines.*

class AppMetadataListener(private val context: Context) {
    private val tag = "AppMetadataListener"
    private var job: Job? = null
    private val scope = CoroutineScope(Dispatchers.Main + SupervisorJob())

    fun startListening() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.LOLLIPOP) return
        job = scope.launch {
            val sessionManager = context.getSystemService(Context.MEDIA_SESSION_SERVICE) as MediaSessionManager
            while (isActive) {
                try {
                    val controllers = sessionManager.getActiveSessions(null)
                    for (controller in controllers) {
                        val metadata = controller.metadata
                        val packageName = controller.packageName
                        if (packageName != null && (packageName.contains("spotify") || packageName.contains("youtube") || packageName.contains("music"))) {
                            Log.i(tag, "App activa: $packageName")
                            val profileManager = AiUserProfileManager(context)
                            val app = context.applicationContext as? IVANNAApplication
                            val preset = when {
                                packageName.contains("spotify") -> "Warm"
                                packageName.contains("youtube") -> "Spatial"
                                else -> getPresetForTimeOfDay()
                            }
                            val profile = IvannaEffectProfile.byName[preset] ?: IvannaEffectProfile.WARM
                            app?.globalEffectManager?.applyProfile(profile)
                            // Guardar preferencia
                            profileManager.saveProfile(profileManager.loadProfile())
                        }
                    }
                } catch (e: Exception) {
                    Log.e(tag, "Error escuchando sesiones: ${e.message}")
                }
                delay(5000) // Revisar cada 5 segundos
            }
        }
    }

    fun stopListening() {
        job?.cancel()
        job = null
    }

    private fun getPresetForTimeOfDay(): String {
        val hour = java.util.Calendar.getInstance().get(java.util.Calendar.HOUR_OF_DAY)
        return when (hour) {
            in 6..10  -> "Warm"
            in 11..17 -> "Balanced"
            in 18..22 -> "Spatial"
            else      -> "Relaxed"
        }
    }
}
