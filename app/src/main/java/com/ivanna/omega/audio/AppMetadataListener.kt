package com.ivanna.omega.audio

import android.content.Context
import android.media.session.MediaController
import android.media.session.MediaSessionManager
import android.os.Build
import android.util.Log
import com.ivanna.omega.core.IVANNAApplication
import com.ivanna.omega.core.UserProfileManager
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
                            val profileManager = UserProfileManager(context)
                            val app = context.applicationContext as? IVANNAApplication
                            val preset = when {
                                packageName.contains("spotify") -> "Warm"
                                packageName.contains("youtube") -> "Spatial"
                                else -> profileManager.getPresetForTimeOfDay()
                            }
                            val profile = IvannaEffectProfile.byName[preset] ?: IvannaEffectProfile.WARM
                            app?.globalEffectManager?.applyProfile(profile)
                            // Guardar preferencia
                            profileManager.saveCurrentProfile(preset, packageName)
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
}
