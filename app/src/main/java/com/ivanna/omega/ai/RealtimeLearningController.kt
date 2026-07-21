package com.ivanna.omega.ai

import android.content.Context
import android.util.Log
import kotlin.math.abs

class RealtimeLearningController(
    context: Context,
    private val learningBias: LearningBias = LearningBias(context).also { it.load() },
    private val adaptiveLearning: AdaptiveLearning = AdaptiveLearning(context),
    private val modelManager: ModelManager = ModelManager(context),
    private val aiInferenceEngine: AIInferenceEngine = AIInferenceEngine(modelManager, adaptiveLearning)
) {
    companion object {
        private const val TAG = "IVANNA.Learning"
    }

    enum class Parameter(val wireName: String, val min: Float, val max: Float) {
        EXCITER("exciter", 0f, 1f),
        EQ("eq", -18f, 18f),
        WIDTH("width", 0f, 1.5f),
        COMP_THRESHOLD("comp_threshold", 0f, 1f),
        COMP_RATIO("comp_ratio", 0f, 1f),
        NHO_HARMONIC("nho_harmonic", 0f, 1f),
        SPATIAL_ANGLE("spatial_angle", 0f, 1.33f),
        SPATIAL_WIDTH("spatial_width", 0f, 1.5f)
    }

    private val autonomousValues = linkedMapOf<Parameter, Float>()

    init {
        Log.i(
            TAG,
            "learning controller route=${if (modelManager.hasUsableFineTuningModel()) "B" else "A"} model=${modelManager.describeCurrentModel()}"
        )
        if (!modelManager.hasUsableFineTuningModel()) {
            Log.i(TAG, "route A activa: bias EMA auditable via learning_bias.jsonl; fine-tuning real no disponible")
        }
        // Mantiene instanciado el engine decorativo para que deje trazas explícitas
        // sobre el estado real del pipeline de IA en vez de quedar completamente huérfano.
        Log.i(TAG, "aiInferenceEngine mode=${aiInferenceEngine.engineModeTag()} active=${aiInferenceEngine.isActive.value}")
    }

    fun resolveAutonomousValue(parameter: Parameter, baseValue: Float, genre: String?, preset: String?): Float {
        val base = clamp(parameter, baseValue)
        val ctx = contextKey(genre, preset)
        learningBias.setActiveContext(ctx)
        val bias = learningBias.getBiasForContext(ctx, parameter.wireName)
        val resolved = clamp(parameter, base + bias)
        autonomousValues[parameter] = resolved
        Log.i(
            TAG,
            "bias.apply param=${parameter.wireName} base=${"%.4f".format(base)} bias=${"%.4f".format(bias)} final=${"%.4f".format(resolved)} genre=${genre?.ifBlank { "-" } ?: "-"} preset=${preset?.ifBlank { "-" } ?: "-"}"
        )
        return resolved
    }

    fun captureUserCorrectionIfNeeded(parameter: Parameter, userValue: Float, genre: String?, preset: String?) {
        val autonomous = autonomousValues[parameter] ?: return
        val clampedUser = clamp(parameter, userValue)
        if (abs(clampedUser - autonomous) < 0.0001f) return
        val ctx = contextKey(genre, preset)
        learningBias.captureCorrection(ctx, parameter.wireName, autonomous, clampedUser)
        autonomousValues[parameter] = clampedUser
    }

    fun forceAutonomousAnchor(parameter: Parameter, value: Float) {
        autonomousValues[parameter] = clamp(parameter, value)
    }

    fun hasAutonomousAnchor(parameter: Parameter): Boolean = autonomousValues.containsKey(parameter)

    fun dumpBiases(): String = "routeA=learning_bias.jsonl model=${modelManager.describeCurrentModel()}"

    fun release() {
        learningBias.release()
    }

    private fun contextKey(genre: String?, preset: String?): String {
        val g = genre?.trim()?.lowercase().orEmpty()
        return if (g.isNotBlank() && g != "unknown" && g != "null") {
            "genre:$g"
        } else {
            "preset:${preset?.ifBlank { "Flat" } ?: "Flat"}"
        }
    }

    private fun clamp(parameter: Parameter, value: Float): Float = value.coerceIn(parameter.min, parameter.max)
}
