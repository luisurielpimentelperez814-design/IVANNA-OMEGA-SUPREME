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

    @Volatile private var successfulFrames: Long = 0L
    @Volatile private var attemptedTicks: Long = 0L
    @Volatile private var lastFailure: String? = null

    val isRunning: Boolean get() = job?.isActive == true
    val appliedFrameCount: Long get() = successfulFrames
    val tickCount: Long get() = attemptedTicks
    val lastError: String? get() = lastFailure

    fun start() {
        if (job?.isActive == true) return
        lastFailure = null
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
                        val updates = IvannaNativeLib.nativeApplyControlFrame()
                        attemptedTicks++
                        if (updates > 0) successfulFrames++
                        if (updates < 0) {
                            lastFailure = "nativeApplyControlFrame devolvió " + updates
                            Log.w(TAG, lastFailure!!)
                        } else if (updates > 0) {
                            lastFailure = null
                        }
                    } catch (e: Throwable) {
                        lastFailure = e.javaClass.simpleName + ": " + (e.message ?: "error nativo")
                        Log.e(TAG, "tick fallido: " + lastFailure, e)
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
