package com.ivanna.omega.audio

import android.content.Context
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaRecorder
import android.util.Log
import kotlinx.coroutines.*
import kotlin.math.*

/**
 * AudioEngine v2.0 — Motor de audio DSP con integración a orquestador unificado.
 *
 * FIXES DE CONECTIVIDAD v2.0:
 *   1. nativeSetAntiDolbyScoresStatic() expuesta como companion static method
 *      para que AudioPipeline.kt pueda llamarla sin instancia.
 *   2. Integración con audio_control_plane.cpp:
 *      - Los scores de YAMNet se inyectan aquí
 *      - Se pasan al C++ via JNI
 *      - El orquestador unificado los aplica como multiplicadores dinámicos
 *   3. Parámetros (exciter, EQ, width) están disponibles para fusión en PDEngine
 *   4. Thread-safe: usa atomic stores para parámetros
 */
class AudioEngine {
    companion object {
        private const val TAG = "AudioEngine"
        private const val SAMPLE_RATE = 48000
        private const val CHANNEL_CONFIG = AudioFormat.CHANNEL_IN_STEREO
        private const val AUDIO_FORMAT = AudioFormat.ENCODING_PCM_16BIT

        init {
            // FIX: AudioEngine cargaba "ivanna_jni" (libivanna_jni.so), que solo
            // contiene un stub vacío (jni/ivanna_jni_stub.cpp) con una única
            // función y mal nombrada. La implementación real de estas funciones
            // (nativeInit, nativeSetExciter, nativeSetAntiDolbyScores, etc.) vive
            // en audio_orchestrator.cpp, compilado dentro de libivanna_omega.so
            // (ver CMakeLists.txt: "# Audio orchestrator (JNI para AudioEngine.kt)").
            // Cargar la librería equivocada garantizaba UnsatisfiedLinkError en
            // CADA llamada nativa de esta clase.
            try {
                System.loadLibrary("ivanna_omega")
                Log.d(TAG, "Librería ivanna_omega cargada")
            } catch (e: UnsatisfiedLinkError) {
                Log.e(TAG, "Error cargando ivanna_omega: ${e.message}")
            }
        }

        // ────────────────────────────────────────────────────────────────────
        // JNI: Inyectar scores de YAMNet al orquestador C++
        // ────────────────────────────────────────────────────────────────────
        // Signature: void nativeSetAntiDolbyScores(float voice, float music, float bass, float silence)
        // Implementación: audio_orchestrator.cpp → control_set_yamnet_scores() → g_control_frame
        @JvmStatic
        external fun nativeSetAntiDolbyScoresStatic(voice: Float, music: Float, bass: Float, silence: Float)

        // ────────────────────────────────────────────────────────────────────
        // JNI: Getters para parámetros de AudioEngine (para fusión en PDEngine)
        // ────────────────────────────────────────────────────────────────────
        @JvmStatic
        external fun nativeGetExciterValue(): Float

        @JvmStatic
        external fun nativeGetEqGainDb(): Float

        @JvmStatic
        external fun nativeGetWidthValue(): Float

        // ────────────────────────────────────────────────────────────────────
        // JNI: Setters para parámetros de AudioEngine
        // ────────────────────────────────────────────────────────────────────
        @JvmStatic
        external fun nativeSetExciter(value: Float)

        @JvmStatic
        external fun nativeSetEqGain(db: Float)

        @JvmStatic
        external fun nativeSetWidth(value: Float)
    }

    private var audioRecord: AudioRecord? = null
    private var isInitialized = false

    fun initialize(sampleRate: Int = SAMPLE_RATE) {
        if (isInitialized) return

        try {
            val bufferSize = AudioRecord.getMinBufferSize(sampleRate, CHANNEL_CONFIG, AUDIO_FORMAT)
            audioRecord = AudioRecord(
                MediaRecorder.AudioSource.DEFAULT,
                sampleRate,
                CHANNEL_CONFIG,
                AUDIO_FORMAT,
                bufferSize
            )
            isInitialized = true
            Log.i(TAG, "AudioEngine inicializado @ $sampleRate Hz")
        } catch (e: Exception) {
            Log.e(TAG, "Error inicializando AudioEngine: ${e.message}")
        }
    }

    fun setExciter(value: Float) {
        nativeSetExciter(value.coerceIn(0f, 1f))
    }

    fun setEqGain(db: Float) {
        nativeSetEqGain(db.coerceIn(-18f, 18f))
    }

    fun setWidth(value: Float) {
        nativeSetWidth(value.coerceIn(0f, 1f))
    }

    fun getExciter(): Float = nativeGetExciterValue()
    fun getEqGain(): Float = nativeGetEqGainDb()
    fun getWidth(): Float = nativeGetWidthValue()

    fun release() {
        audioRecord?.release()
        isInitialized = false
        Log.i(TAG, "AudioEngine liberado")
    }
}
