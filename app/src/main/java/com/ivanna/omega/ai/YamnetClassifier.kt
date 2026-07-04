package com.ivanna.omega.ai

import android.content.Context
import android.util.Log
import org.tensorflow.lite.Interpreter
import java.io.FileInputStream
import java.nio.MappedByteBuffer
import java.nio.channels.FileChannel

/**
 * YamnetClassifier v1.5 — Clasificación de audio en tiempo real.
 *
 * FIX v1.5:
 * 1. Carga modelo TFLite desde assets sin comprimir
 * 2. Fallo graceful si falta modelo (no crash)
 * 3. Mapea 521 clases YAMNet a: Speech, Music, Bass
 * 4. Buffer de 0.96s @ 16kHz (input requerido por YAMNet)
 */
class YamnetClassifier(context: Context) {
    companion object {
        private const val TAG = "YamnetClassifier"
        private const val MODEL_PATH = "yamnet.tflite"
        private const val SAMPLE_RATE = 16000
        private const val INPUT_LENGTH = 15600  // 0.975s @ 16kHz

        // Índices YAMNet para clases relevantes (verificados contra CSV real del modelo YAMNet v1)
        // Estos índices corresponden a las clases reales en el ontology de 521 clases
        private const val IDX_SPEECH = 0      // "Speech" (índice 0)
        private const val IDX_MUSIC = 132     // "Music" (índice 132)
        private const val IDX_MUSICAL_INSTRUMENT = 133  // "Musical instrument" (índice 133)
        private const val IDX_BASS_DRUM = 163 // "Bass drum" (índice 163)
        private const val IDX_BASS_GUITAR = 137  // "Bass guitar" (índice 137)
        private const val IDX_DOUBLE_BASS = 189  // "Double bass" (índice 189)
    }

    private var interpreter: Interpreter? = null
    private var isAvailable = false

    data class ClassificationResult(
        val speech: Float,
        val music: Float,
        val bass: Float,
        val isValid: Boolean
    )

    init {
        try {
            interpreter = Interpreter(loadModelFile(context))
            isAvailable = true
            Log.i(TAG, "YAMNet cargado correctamente")
        } catch (e: Exception) {
            Log.w(TAG, "YAMNet no disponible: ${e.message}. Modo fallback activado.")
            isAvailable = false
        }
    }

    /**
     * Clasifica un frame de audio.
     * @param audioFrame FloatArray de INPUT_LENGTH samples @ 16kHz mono
     * @return ClassificationResult con scores 0.0-1.0
     */
    fun classify(audioFrame: FloatArray): ClassificationResult {
        val localInterpreter = interpreter
        if (!isAvailable || localInterpreter == null) {
            return ClassificationResult(0f, 0f, 0f, false)
        }

        try {
            // Validar tamaño
            if (audioFrame.size < INPUT_LENGTH) {
                Log.w(TAG, "Frame demasiado corto: ${audioFrame.size} < $INPUT_LENGTH")
                return ClassificationResult(0f, 0f, 0f, false)
            }

            // YAMNet input: [1, 15600]
            val input = Array(1) { FloatArray(INPUT_LENGTH) }
            System.arraycopy(audioFrame, 0, input[0], 0, INPUT_LENGTH)

            // YAMNet output: [1, 521] scores por clase
            val output = Array(1) { FloatArray(521) }
            localInterpreter.run(input, output)

            val scores = output[0]
            val speechScore = scores[IDX_SPEECH]
            val musicScore = maxOf(
                scores[IDX_MUSIC],
                scores[IDX_MUSICAL_INSTRUMENT]
            )
            val bassScore = maxOf(
                scores[IDX_BASS_DRUM],
                scores[IDX_BASS_GUITAR],
                scores[IDX_DOUBLE_BASS]
            )

            return ClassificationResult(
                speech = speechScore,
                music = musicScore,
                bass = bassScore,
                isValid = true
            )

        } catch (e: Exception) {
            Log.e(TAG, "Error en clasificación: ${e.message}")
            return ClassificationResult(0f, 0f, 0f, false)
        }
    }

    /**
     * Clasificación rápida para detección de voz.
     * Usada por el modo Anti-Dolby para ajustar widener/EQ dinámicamente.
     */
    fun isSpeechDominant(audioFrame: FloatArray, threshold: Float = 0.6f): Boolean {
        val result = classify(audioFrame)
        return result.isValid && result.speech > threshold
    }

    fun isBassDominant(audioFrame: FloatArray, threshold: Float = 0.6f): Boolean {
        val result = classify(audioFrame)
        return result.isValid && result.bass > threshold
    }

    fun release() {
        interpreter?.close()
        interpreter = null
        isAvailable = false
    }

    private fun loadModelFile(context: Context): MappedByteBuffer {
        val fileDescriptor = context.assets.openFd(MODEL_PATH)
        val inputStream = FileInputStream(fileDescriptor.fileDescriptor)
        val fileChannel = inputStream.channel
        val startOffset = fileDescriptor.startOffset
        val declaredLength = fileDescriptor.declaredLength
        return fileChannel.map(FileChannel.MapMode.READ_ONLY, startOffset, declaredLength)
    }
}
