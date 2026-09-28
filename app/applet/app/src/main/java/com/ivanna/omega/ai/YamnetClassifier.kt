package com.ivanna.omega.ai

import android.content.Context

/**
 * YamnetClassifier — SHIM DE COMPATIBILIDAD (v2.1).
 *
 * El modelo real ahora es [AntiDolbyCrnnClassifier] (CRNN entrenado in-house,
 * 4 clases: Voz/Musica/Bajos/Silencio, features log-mel 32×40 @ 16 kHz).
 * Esta clase queda como una fina fachada delegando en el nuevo clasificador
 * para no romper los callers históricos (AntiDolbyController, VoiceController,
 * VoiceProtectionController, AudioPipeline).
 *
 * MIGRACIÓN RECOMENDADA:
 *   - Nuevos módulos: usar AntiDolbyCrnnClassifier directamente.
 *   - Antiguos que ya compilan: siguen funcionando sin tocar nada; internamente
 *     este shim invoca a AntiDolbyCrnnClassifier.
 *
 * NOTA sobre INPUT_LENGTH:
 *   YAMNet requería 15600 samples (~0.975 s @ 16 kHz). El CRNN nuevo requiere
 *   5472 (~0.342 s). Como el CRNN acepta buffers MÁS LARGOS (usa solo los
 *   primeros 5472), los callers que sigan enviando 15600 samples siguen
 *   funcionando sin cambio.
 */
class YamnetClassifier(context: Context) {

    companion object {
        // Compatibilidad: los callers antiguos leen INPUT_LENGTH desde acá.
        // Mantenemos 15600 para que los buffers existentes sigan siendo válidos
        // (el CRNN acepta cualquier tamaño >= 5472).
        const val INPUT_LENGTH = 15600
        const val SAMPLE_RATE  = 16000
    }

    // Mismo shape de datos que antes, ahora respaldado por el CRNN.
    data class ClassificationResult(
        val speech: Float,
        val music: Float,
        val bass: Float,
        val isValid: Boolean
    )

    private val impl = AntiDolbyCrnnClassifier(context)

    fun classify(audioFrame: FloatArray): ClassificationResult {
        val r = impl.classify(audioFrame)
        return ClassificationResult(
            speech  = r.speech,
            music   = r.music,
            bass    = r.bass,
            isValid = r.isValid
        )
    }

    fun isSpeechDominant(audioFrame: FloatArray, threshold: Float = 0.6f): Boolean =
        impl.isSpeechDominant(audioFrame, threshold)

    fun isBassDominant(audioFrame: FloatArray, threshold: Float = 0.6f): Boolean =
        impl.isBassDominant(audioFrame, threshold)

    fun release() = impl.release()
}
