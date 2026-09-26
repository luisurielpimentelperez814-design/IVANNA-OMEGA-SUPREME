package com.ivanna.omega.core

import android.util.Log

/**
 * NativeBridge — Canonical JNI bridge for IVANNA-OMEGA-SUPREME.
 * Single source of truth for the Cochlear Active Inverse Engine and spatial plane.
 */
object NativeBridge {
    private const val TAG = "NativeBridge"
    val isLoaded: Boolean = NativeLibraryLoader.ensureLoaded()

    @JvmStatic external fun setCochlearInverseEnabled(enabled: Boolean)
    @JvmStatic external fun setCochlearIntensity(intensity: Float)
    @JvmStatic external fun isCochlearActive(): Boolean
    @JvmStatic external fun getCochlearIntensity(): Float

    fun safeSetCochlearInverseEnabled(enabled: Boolean) {
        if (!isLoaded) {
            Log.w(TAG, "Native library not loaded; ignoring setCochlearInverseEnabled")
            return
        }
        try {
            setCochlearInverseEnabled(enabled)
        } catch (t: Throwable) {
            Log.e(TAG, "Failed to call setCochlearInverseEnabled", t)
        }
    }

    fun safeSetCochlearIntensity(intensity: Float) {
        if (!isLoaded) {
            Log.w(TAG, "Native library not loaded; ignoring setCochlearIntensity")
            return
        }
        try {
            setCochlearIntensity(intensity)
        } catch (t: Throwable) {
            Log.e(TAG, "Failed to call setCochlearIntensity", t)
        }
    }

    fun safeIsCochlearActive(): Boolean {
        if (!isLoaded) return false
        return try {
            isCochlearActive()
        } catch (t: Throwable) {
            false
        }
    }

    fun safeGetCochlearIntensity(): Float {
        if (!isLoaded) return 0.35f
        return try {
            getCochlearIntensity()
        } catch (t: Throwable) {
            0.35f
        }
    }
}
