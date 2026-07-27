package com.ivanna.omega.audio

import android.content.Context
import android.util.Log

object AudioEngineInitializer {
    private const val TAG = "AudioEngineInit"
    private var initialized = false
    private val lock = Any()

    fun ensureInitialized(context: Context, sampleRate: Int = 96000): Boolean =
        synchronized(lock) {
            if (initialized) return true
            
            return runCatching {
                AudioEngine().apply { 
                    initialize(sampleRate)
                    initialized = true
                }
                Log.i(TAG, "AudioEngine initialized @ ${sampleRate}Hz")
                true
            }.onFailure {
                Log.e(TAG, "AudioEngine init failed: $it")
                initialized = false
            }.getOrNull() ?: false
        }

    fun isInitialized(): Boolean = synchronized(lock) { initialized }
}
