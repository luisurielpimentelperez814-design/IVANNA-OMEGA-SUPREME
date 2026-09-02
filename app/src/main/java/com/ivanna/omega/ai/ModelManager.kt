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
        // FIX: createSyntheticModel() escribía el texto "TFL3 - IVANNA Speech Enhancer..."
        // en un archivo .tflite — un archivo de texto plano que fingía ser un modelo.
        // AIInferenceEngine ya no aplica inferencia sin modelo real (ver fix allí),
        // así que no necesitamos crear un placeholder. El directorio existe por si
        // un modelo fine-tuned real llega vía saveFineTunedModel().
        val existing = modelsDir.listFiles { f -> f.extension == "tflite" }
        val newest = existing?.maxByOrNull { it.lastModified() }
        _currentModelPath.value = newest?.absolutePath
        // _currentModelVersion permanece en 1 (default) hasta que haya modelo real
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

fun ModelManager.hasUsableFineTuningModel(): Boolean = false
fun ModelManager.describeCurrentModel(): String =
    currentModelPath.value?.let { "fine-tuned v${currentModelVersion.value} ($it)" }
        ?: "none (sin modelo TFLite — RealtimeLearningController en modo bias-EMA)"
