package com.ivanna.omega.ai

import android.content.Context
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.io.File
import java.nio.file.AtomicMoveNotSupportedException
import java.nio.file.Files
import java.nio.file.StandardCopyOption

class ModelManager(private val context: Context) {
    private val modelsDir = File(context.filesDir, "ai_models").apply { mkdirs() }
    private val _currentModelVersion = MutableStateFlow(1)
    val currentModelVersion: StateFlow<Int> = _currentModelVersion.asStateFlow()
    private val _currentModelPath = MutableStateFlow<String?>(null)
    val currentModelPath: StateFlow<String?> = _currentModelPath.asStateFlow()

    init { initializeModel() }

    private fun initializeModel() {
        // Solo indexa archivos que al menos tienen el identificador FlatBuffer TFL3.
        // La firma no prueba compatibilidad de entrada/salida ni conexión al DSP.
        val existing = modelsDir.listFiles { f ->
            f.extension == "tflite" && runCatching { isTfliteModelBytes(f.readBytes()) }.getOrDefault(false)
        }
        val newest = existing?.maxByOrNull { it.lastModified() }
        _currentModelPath.value = newest?.absolutePath
        // La ruta genérica de inferencia permanece desactivada hasta que haya
        // un ejecutor real compatible con la forma de tensor del modelo.
    }

    fun saveFineTunedModel(data: ByteArray, version: Int): String {
        require(version > 0) { "La versión del modelo debe ser positiva." }
        require(isTfliteModelBytes(data)) {
            "Modelo rechazado: no contiene el identificador FlatBuffer TFL3 en bytes 4..7."
        }
        val file = File(modelsDir, "model_v${version}.tflite")
        val temp = File(modelsDir, ".model_v${version}.tflite.tmp")
        try {
            temp.writeBytes(data)
            try {
                Files.move(temp.toPath(), file.toPath(), StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE)
            } catch (_: AtomicMoveNotSupportedException) {
                Files.move(temp.toPath(), file.toPath(), StandardCopyOption.REPLACE_EXISTING)
            }
        } catch (t: Throwable) {
            temp.delete()
            throw t
        }
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

internal fun isTfliteModelBytes(data: ByteArray): Boolean =
    data.size >= 8 &&
        data[4] == 'T'.code.toByte() &&
        data[5] == 'F'.code.toByte() &&
        data[6] == 'L'.code.toByte() &&
        data[7] == '3'.code.toByte()

fun ModelManager.hasUsableFineTuningModel(): Boolean = false
fun ModelManager.describeCurrentModel(): String =
    currentModelPath.value?.let {
        "stored v${currentModelVersion.value}; runtime inference not connected (route A bias-EMA)"
    } ?: "none (sin modelo TFLite — RealtimeLearningController en modo bias-EMA)"
