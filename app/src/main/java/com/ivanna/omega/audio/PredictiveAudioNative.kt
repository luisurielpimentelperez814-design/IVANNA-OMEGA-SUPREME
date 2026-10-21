package com.ivanna.omega.audio

import com.ivanna.omega.core.NativeLibraryLoader

/** Puente JNI crudo del motor predictivo (ver cpp/jni/ivanna_predictive_jni.cpp). */
object PredictiveAudioNative {
    val isLoaded: Boolean = NativeLibraryLoader.ensureLoaded()

    @JvmStatic external fun nativeCreate(sampleRate: Float): Long
    @JvmStatic external fun nativeDestroy(handle: Long)
    @JvmStatic external fun nativeReset(handle: Long)
    @JvmStatic external fun nativeProcessInterleaved(handle: Long, interleaved: FloatArray, frames: Int): Boolean
    @JvmStatic external fun nativeGetFrame(handle: Long, out: FloatArray): Boolean
}

/** Resultado del último pronóstico. `valid=false` = el modelo no es fiable (no usar `confidence`). */
data class PredictiveSnapshot(
    val valid: Boolean = false,
    val confidence: Float = 0f,
    val predictionGainDb: Float = 0f,
    val transient: Boolean = false
)

/**
 * Observador de solo lectura sobre el audio estéreo intercalado que ya produce el reproductor.
 * Un solo hilo (el del decode loop). No altera el audio; si la librería nativa no carga, queda inerte.
 */
class PredictiveAudioAnalyzer(sampleRate: Int) : AutoCloseable {
    private var handle: Long =
        if (PredictiveAudioNative.isLoaded) {
            try { PredictiveAudioNative.nativeCreate(sampleRate.toFloat()) } catch (_: Throwable) { 0L }
        } else 0L
    private val fields = FloatArray(FRAME_FIELDS)

    val isActive: Boolean get() = handle != 0L

    fun feed(interleaved: FloatArray, frames: Int) {
        if (handle != 0L && frames > 0) PredictiveAudioNative.nativeProcessInterleaved(handle, interleaved, frames)
    }

    /** Descarta el historial (p. ej. tras un seek: la señal previa ya no precede a la nueva). */
    fun reset() {
        if (handle != 0L) PredictiveAudioNative.nativeReset(handle)
    }

    fun snapshot(): PredictiveSnapshot {
        if (handle == 0L || !PredictiveAudioNative.nativeGetFrame(handle, fields)) return PredictiveSnapshot()
        return PredictiveSnapshot(
            valid = fields[0] > 0.5f,
            confidence = fields[1].coerceIn(0f, 1f),
            predictionGainDb = fields[4],
            transient = fields[8] > 0.5f
        )
    }

    override fun close() {
        val h = handle
        handle = 0L
        if (h != 0L) PredictiveAudioNative.nativeDestroy(h)
    }

    private companion object { const val FRAME_FIELDS = 9 }
}
