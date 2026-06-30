package com.ivanna.omega.ai

import android.content.Context
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.io.File
import java.util.concurrent.ConcurrentLinkedQueue

class AdaptiveLearning(private val context: Context) {
    data class AudioExperience(
        val timestamp: Long,
        val inputFeatures: FloatArray,
        val aiOutput: FloatArray,
        val userAdjusted: Boolean,
        val finalParams: Map<String, Float>,
        val confidence: Float
    )

    private val experienceBuffer = ConcurrentLinkedQueue<AudioExperience>()
    private val _totalExperiences = MutableStateFlow(0)
    val totalExperiences: StateFlow<Int> = _totalExperiences.asStateFlow()
    private val _modelVersion = MutableStateFlow(1)
    val modelVersion: StateFlow<Int> = _modelVersion.asStateFlow()

    fun captureExperience(input: FloatArray, output: FloatArray, userAdjusted: Boolean, params: Map<String, Float>) {
        val confidence = if (userAdjusted) 0.3f else 0.8f
        val exp = AudioExperience(System.currentTimeMillis(), input, output, userAdjusted, params, confidence)
        experienceBuffer.add(exp)
        _totalExperiences.value = experienceBuffer.size
        while (experienceBuffer.size > 1000) experienceBuffer.poll()
    }

    fun isReadyForTraining() = experienceBuffer.size >= 50
    fun getTrainingData() = experienceBuffer.toList()
    fun clearAfterTraining(newVersion: Int) {
        experienceBuffer.clear()
        _totalExperiences.value = 0
        _modelVersion.value = newVersion
    }
}
