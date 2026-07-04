package com.ivanna.omega.audio

import android.content.Context
import android.media.AudioFormat
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
        private const val MAX_FRAMES = 4096
        private const val BYTES_PER_FLOAT = 4

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
        // JNI: Procesamiento de audio (dos variantes: compatible y zero-copy)
        // ────────────────────────────────────────────────────────────────────
        // nativeProcessAudio: usa jfloatArray (GetFloatArrayElements),
        //   puede causar copias en ART. Mantenida por compatibilidad.
        @JvmStatic
        external fun nativeProcessAudio(
            inArray: FloatArray,
            outArray: FloatArray,
            frames: Int,
            channels: Int
        )

        // nativeProcessAudioDirect: usa jobject (GetDirectBufferAddress),
        //   zero-copy si el buffer es un ByteBuffer.allocateDirect().
        //   OPTIMIZACION (fricción NDK<->OS): mismo procesamiento, sin copies.
        @JvmStatic
        external fun nativeProcessAudioDirect(
            inBuffer: java.nio.ByteBuffer,
            outBuffer: java.nio.ByteBuffer,
            frames: Int,
            channels: Int
        )

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

    private var isInitialized = false

    // OPTIMIZACION: buffers de ByteBuffer.allocateDirect() reutilizables,
    // creados una sola vez durante inicializacion. Eliminan copia de heap
    // Java en cada callback de audio (GetFloatArrayElements puede copiar en ART).
    // Orden: nativeOrder() para que JNI los lea sin swap de bytes.
    private var inBufferDirect: java.nio.ByteBuffer? = null
    private var outBufferDirect: java.nio.ByteBuffer? = null

    fun initialize(sampleRate: Int = SAMPLE_RATE) {
        isInitialized = true
        // Pre-allocate direct buffers (máximo tamaño esperado: 4096 frames × 2 ch × 4 bytes/float)
        val bufferSizeBytes = MAX_FRAMES * 2 * BYTES_PER_FLOAT
        inBufferDirect = java.nio.ByteBuffer.allocateDirect(bufferSizeBytes)
            .order(java.nio.ByteOrder.nativeOrder())
        outBufferDirect = java.nio.ByteBuffer.allocateDirect(bufferSizeBytes)
            .order(java.nio.ByteOrder.nativeOrder())
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

    // OPTIMIZACION: procesamiento zero-copy usando ByteBuffer directo.
    // Remplaza nativeProcessAudio(FloatArray, FloatArray) cuando se llama
    // desde un callback de audio real (AAudio/OpenSL ES). Requiere que
    // inData y outData sean punteros válidos a memoria contígua de floats.
    // Evita GetFloatArrayElements (que puede copiar el heap Java en ART).
    fun processAudioDirect(
        inData: java.nio.FloatBuffer,
        outData: java.nio.FloatBuffer,
        frames: Int,
        channels: Int
    ) {
        if (!isInitialized) {
            Log.w(TAG, "AudioEngine no inicializado")
            return
        }
        if (inData == null || outData == null) {
            Log.w(TAG, "processAudioDirect: buffer nulo")
            return
        }
        if (frames <= 0 || frames > MAX_FRAMES) {
            Log.w(TAG, "processAudioDirect: frames inválido: $frames")
            return
        }

        val inBuf = inBufferDirect ?: return
        val outBuf = outBufferDirect ?: return

        // Copiar entrada al buffer directo (una sola vez, sin pin/unpin por JVM)
        inBuf.clear()
        inBuf.asFloatBuffer().put(inData.duplicate().apply { rewind() }).rewind()

        // Procesar (zero-copy: mismo puntero dentro de JNI)
        nativeProcessAudioDirect(inBuf, outBuf, frames, channels)

        // Copiar salida del buffer directo al destino
        outBuf.rewind()
        outData.clear()
        outData.put(outBuf.asFloatBuffer())
        outData.rewind()
    }

    fun release() {
        isInitialized = false
        inBufferDirect = null
        outBufferDirect = null
    }
}
