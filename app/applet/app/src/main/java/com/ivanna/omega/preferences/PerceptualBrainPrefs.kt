package com.ivanna.omega.preferences

import android.content.Context

/**
 * Configuración del "cerebro perceptual" — intensidades 0.0..1.0 por eje.
 *
 * Los defaults viven UNA sola vez aquí (fuente única de verdad). Antes estaban
 * duplicados literalmente en esta data class y en PerceptualBrainPrefs.load():
 * cambiar uno y olvidar el otro dejaba al sistema inconsistente (la data class
 * decía X pero load() devolvía Y). Ahora load() lee los defaults de la propia
 * data class — es imposible que diverjan.
 */
data class PerceptualBrainConfig(
    val perceptualIntelligence: Float = 0.85f,
    val neuralAdaptation: Float = 0.80f,
    val spatialImmersion: Float = 0.90f,
    val harmonicReconstruction: Float = 0.75f,
    val antiDolbyBlend: Float = 1.00f,
    val humanLoudnessCompensation: Float = 0.82f
)

object PerceptualBrainPrefs {

    private const val PREFS_NAME = "perceptual_brain_prefs"

    // Instancia con todos los defaults — única fuente de verdad.
    private val DEFAULTS = PerceptualBrainConfig()

    fun load(context: Context): PerceptualBrainConfig {
        val prefs = context.getSharedPreferences(
            PREFS_NAME,
            Context.MODE_PRIVATE
        )

        // Cada getFloat usa como default el valor de la propia data class —
        // no un literal duplicado que pueda quedar desactualizado.
        return PerceptualBrainConfig(
            perceptualIntelligence = prefs.getFloat(
                "perceptualIntelligence",
                DEFAULTS.perceptualIntelligence
            ),
            neuralAdaptation = prefs.getFloat(
                "neuralAdaptation",
                DEFAULTS.neuralAdaptation
            ),
            spatialImmersion = prefs.getFloat(
                "spatialImmersion",
                DEFAULTS.spatialImmersion
            ),
            harmonicReconstruction = prefs.getFloat(
                "harmonicReconstruction",
                DEFAULTS.harmonicReconstruction
            ),
            antiDolbyBlend = prefs.getFloat(
                "antiDolbyBlend",
                DEFAULTS.antiDolbyBlend
            ),
            humanLoudnessCompensation = prefs.getFloat(
                "humanLoudnessCompensation",
                DEFAULTS.humanLoudnessCompensation
            )
        )
    }

    fun save(
        context: Context,
        config: PerceptualBrainConfig
    ) {
        context.getSharedPreferences(
            PREFS_NAME,
            Context.MODE_PRIVATE
        )
            .edit()
            .putFloat(
                "perceptualIntelligence",
                config.perceptualIntelligence
            )
            .putFloat(
                "neuralAdaptation",
                config.neuralAdaptation
            )
            .putFloat(
                "spatialImmersion",
                config.spatialImmersion
            )
            .putFloat(
                "harmonicReconstruction",
                config.harmonicReconstruction
            )
            .putFloat(
                "antiDolbyBlend",
                config.antiDolbyBlend
            )
            .putFloat(
                "humanLoudnessCompensation",
                config.humanLoudnessCompensation
            )
            .apply()
    }
}
