package com.ivanna.omega.audio

import android.util.Log
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.neuromorphic.IvannaNpeEngine
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

// ─────────────────────────────────────────────────────────────────────────────
// IvannaControlLoop — FASE 3C. nativeSetLearningContext() y
// nativeApplyControlFrame() estaban declaradas e implementadas en JNI
// (ivanna_omega_jni.cpp:1133-1146, control_apply_frame() + LearningBias)
// pero ninguna se invocaba nunca: el "control frame" del motor de aprendizaje
// nunca se aplicaba. Corre a 20Hz (50ms) — mismo orden de magnitud que el
// resto de loops de control lentos de la app (AdaptiveBackend es 10Hz).
// Género real desde IvannaNpeEngine.getDetectedGenre() (NPE ya corriendo),
// no un placeholder.
// ─────────────────────────────────────────────────────────────────────────────
object IvannaControlLoop {

    private const val TAG = "IvannaControlLoop"
    private const val INTERVAL_MS = 50L // 20Hz

    private val scope = CoroutineScope(Dispatchers.Default + SupervisorJob())
    private var job: Job? = null

    fun start() {
        if (job?.isActive == true) return
        job = scope.launch {
            var lastGenre: String? = null
            while (isActive) {
                if (IvannaNativeLib.isLoaded) {
                    try {
                        val genre = IvannaNpeEngine.getDetectedGenre()
                        if (genre.isNotBlank() && genre != "\u2014" && genre != lastGenre) {
                            IvannaNativeLib.nativeSetLearningContext(genre)
                            lastGenre = genre
                        }
                        IvannaNativeLib.nativeApplyControlFrame()
                    } catch (e: Throwable) {
                        Log.w(TAG, "tick: $e")
                    }
                }
                delay(INTERVAL_MS)
            }
        }
    }

    fun stop() {
        job?.cancel()
        job = null
    }
}
