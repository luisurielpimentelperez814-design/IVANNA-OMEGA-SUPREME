package com.ivanna.omega.ai

import android.content.Context
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.io.File

class ModelManager(private val context: Context) {
    private val modelsDir = File(context.filesDir, "ai_models").apply { mkdirs() }
    private val _currentModelVersion = MutableStateFlow(1)
    val currentModelVersion: StateFlow<Int> = _currentModelVersion.asStateFlow()
    private val _currentModelPath = MutableStateFlow<String?>(null)
    val currentModelPath: StateFlow<String?> = _currentModelPath.asStateFlow()

    init { initializeModel() }

    private fun initializeModel() {
        val baseModel = File(modelsDir, "base_model_v1.tflite")
        if (!baseModel.exists()) createSyntheticModel(baseModel)
        _currentModelPath.value = baseModel.absolutePath
    }

    private fun createSyntheticModel(file: File) {
        file.writeText("TFL3 - IVANNA Speech Enhancer v1 - RNNoise-inspired - 2.1MB INT8")
    }

    fun saveFineTunedModel(data: ByteArray, version: Int): String {
        val file = File(modelsDir, "model_v${version}.tflite")
        file.writeBytes(data)
        _currentModelPath.value = file.absolutePath
        _currentModelVersion.value = version
        return file.absolutePath
    }

    fun cleanupOldModels() {
        val current = _currentModelVersion.value
        modelsDir.listFiles()?.filter { it.name.startsWith("model_v") }?.forEach {
            val v = it.nameWithoutExtension.removePrefix("model_v").toIntOrNull()
            if (v != null && v < current - 1) it.delete()
        }
    }
}
