package com.ivanna.omega.ai

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

class AIInferenceEngine(
    private val modelManager: ModelManager,
    private val adaptiveLearning: AdaptiveLearning
) {
    private val _isActive = MutableStateFlow(false)
    val isActive: StateFlow<Boolean> = _isActive.asStateFlow()
    private val _inferenceCount = MutableStateFlow(0L)
    val inferenceCount: StateFlow<Long> = _inferenceCount.asStateFlow()

    fun processAudioBlock(audioInput: FloatArray): FloatArray {
        if (!_isActive.value) return audioInput.copyOf()
        _inferenceCount.value++
        // FIX: el código anterior aplicaba `it * 1.05f` a cada muestra —
        // un +0.4 dB silencioso en CADA bloque de audio sin ningún modelo real
        // cargado. Se etiquetaba como "Simulated inference" pero en producción
        // el efecto era un boost inesperado que compoundaba con el EQ/GainStage.
        // Sin un modelo TFLite real inferenciado, el bloque pasa sin modificar.
        return audioInput.copyOf()
    }

    fun startSession() { _isActive.value = true }
    fun endSession(input: FloatArray, output: FloatArray, userAdjusted: Boolean, params: Map<String, Float>) {
        adaptiveLearning.captureExperience(input, output, userAdjusted, params)
        _isActive.value = false
    }
}

fun AIInferenceEngine.engineModeTag(): String = "adaptive"
