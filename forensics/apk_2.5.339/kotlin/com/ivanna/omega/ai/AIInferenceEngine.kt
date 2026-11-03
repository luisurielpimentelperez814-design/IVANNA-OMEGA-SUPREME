package com.ivanna.omega.ai

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * API de compatibilidad para el futuro ejecutor de modelos personalizados.
 * Actualmente no existe un contrato de tensores ni un intérprete conectado a esta
 * ruta genérica. Por eso esta clase no se declara activa ni cuenta copias passthrough
 * como inferencias. El clasificador AntiDolby CRNN es una ruta TFLite distinta.
 */
class AIInferenceEngine(
    private val modelManager: ModelManager,
    private val adaptiveLearning: AdaptiveLearning
) {
    private val _isActive = MutableStateFlow(false)
    val isActive: StateFlow<Boolean> = _isActive.asStateFlow()
    private val _inferenceCount = MutableStateFlow(0L)
    val inferenceCount: StateFlow<Long> = _inferenceCount.asStateFlow()

    /** Sin un ejecutor TFLite real, conserva el audio y no incrementa el contador de inferencia. */
    fun processAudioBlock(audioInput: FloatArray): FloatArray = audioInput.copyOf()

    /** No activa un motor que todavía no puede ejecutar inferencia. */
    fun startSession() {
        _isActive.value = false
    }

    fun endSession(input: FloatArray, output: FloatArray, userAdjusted: Boolean, params: Map<String, Float>) {
        adaptiveLearning.captureExperience(input, output, userAdjusted, params)
        _isActive.value = false
    }
}

fun AIInferenceEngine.engineModeTag(): String = "bias-ema (sin inferencia genérica conectada)"
