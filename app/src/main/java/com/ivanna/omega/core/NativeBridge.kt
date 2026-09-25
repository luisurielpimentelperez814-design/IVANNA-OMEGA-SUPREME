package com.ivanna.omega.core

/**
 * NativeBridge — Puente JNI directo para el motor CochlearActiveInverseEngine.
 * Vinculado contra libivanna_omega.so con operaciones lock-free.
 */
object NativeBridge {
    val isLoaded: Boolean = NativeLibraryLoader.ensureLoaded()

    @JvmStatic external fun setCochlearInverseEnabled(enabled: Boolean)
    @JvmStatic external fun setCochlearIntensity(intensity: Float)
    @JvmStatic external fun isCochlearActive(): Boolean
}
