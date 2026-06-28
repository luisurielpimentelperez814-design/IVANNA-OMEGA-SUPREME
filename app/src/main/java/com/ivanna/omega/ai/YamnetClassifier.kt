package com.ivanna.omega.ai

/**
 * YamnetClassifier — stub object compatible con AudioEngine.kt y PlaybackCaptureService.kt.
 * YAMNet (Yet Another Mobile Network) clasifica audio de 16kHz en 521 clases AudioSet.
 * La inferencia real requiere TFLite + modelo yamnet.tflite; aquí retorna stub "unknown"
 * hasta que el modelo esté disponible en /data/data/.../files/yamnet.tflite.
 */
object YamnetClassifier {

    /** Muestras de entrada que espera YAMNet @ 16kHz (975 ms de contexto) */
    const val INPUT_SAMPLES = 15600

    /** Frecuencia de muestreo esperada por YAMNet */
    const val SAMPLE_RATE_HZ = 16000

    /** true cuando el modelo TFLite está cargado y listo */
    @Volatile
    var isLoaded: Boolean = false
        private set

    /** Resultado de una clasificación */
    data class Result(
        val label: String,
        val score: Float,
        val topK: List<Pair<String, Float>> = emptyList()
    )

    /**
     * Clasifica un bloque de [INPUT_SAMPLES] muestras mono @ 16kHz.
     * Retorna null si el modelo no está cargado o la entrada es inválida.
     */
    fun classify(audioBuffer: FloatArray): Result? {
        if (!isLoaded || audioBuffer.size < INPUT_SAMPLES) return null
        // Stub: en producción, invocar TFLite interpreter aquí
        return Result(label = "unknown", score = 0f)
    }

    /**
     * Libera recursos (intérprete TFLite, buffers).
     * Seguro llamar aunque el modelo no esté cargado.
     */
    fun release() {
        isLoaded = false
        // Stub: en producción, cerrar intérprete TFLite aquí
    }
}
