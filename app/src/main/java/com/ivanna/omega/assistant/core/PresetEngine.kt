package com.ivanna.omega.assistant.core

import android.util.Log
import com.ivanna.omega.magisk.OmegaDaemon

object PresetEngine {
    private const val TAG = "PresetEngine"

    fun applyAudiophileReference() {
        OmegaDaemon.setPFParams(0.0f, 0.1f, 1.0f, 0.5f, 0.5f, 0.5f, 1000f, 0.7f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f)
        Log.d(TAG, "Applied Audiophile Reference")
    }

    fun applyCinemaImmersive() {
        OmegaDaemon.setPFParams(0.2f, 0.5f, 1.0f, 0.8f, 0.7f, 0.6f, 800f, 0.5f, 3.0f, 0.0f, 1.5f, 2.0f, 1.0f)
        Log.d(TAG, "Applied Cinema Immersive")
    }

    fun applyDeepBass() {
        OmegaDaemon.setPFParams(0.3f, 0.2f, 1.0f, 0.6f, 0.5f, 0.4f, 80f, 0.9f, 6.0f, -1.0f, 0.0f, 0.0f, 1.0f)
        Log.d(TAG, "Applied Deep Bass")
    }

    fun applyVocalClarity() {
        OmegaDaemon.setPFParams(0.0f, 0.1f, 1.0f, 0.5f, 0.5f, 0.5f, 3000f, 0.6f, -2.0f, 3.0f, 2.0f, 4.0f, 1.0f)
        Log.d(TAG, "Applied Vocal Clarity")
    }

    fun applyHearingComfort() {
        // Reduces high frequencies and resonance
        OmegaDaemon.setPFParams(0.0f, 0.0f, 1.0f, 0.4f, 0.4f, 0.3f, 500f, 0.3f, 0.0f, 0.0f, -4.0f, -2.0f, 0.8f)
        Log.d(TAG, "Applied Hearing Comfort")
    }
    
    fun applyDynamic(low: Float, mid: Float, high: Float, presence: Float) {
        OmegaDaemon.setPFParams(0.1f, 0.2f, 1.0f, 0.5f, 0.5f, 0.5f, 1000f, 0.5f, low, mid, high, presence, 1.0f)
    }
}
