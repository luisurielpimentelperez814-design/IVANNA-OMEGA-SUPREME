package com.ivanna.omega.preferences

import android.content.Context

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

    fun load(context: Context): PerceptualBrainConfig {
        val prefs = context.getSharedPreferences(
            PREFS_NAME,
            Context.MODE_PRIVATE
        )

        return PerceptualBrainConfig(
            perceptualIntelligence = prefs.getFloat(
                "perceptualIntelligence",
                0.85f
            ),
            neuralAdaptation = prefs.getFloat(
                "neuralAdaptation",
                0.80f
            ),
            spatialImmersion = prefs.getFloat(
                "spatialImmersion",
                0.90f
            ),
            harmonicReconstruction = prefs.getFloat(
                "harmonicReconstruction",
                0.75f
            ),
            antiDolbyBlend = prefs.getFloat(
                "antiDolbyBlend",
                1.00f
            ),
            humanLoudnessCompensation = prefs.getFloat(
                "humanLoudnessCompensation",
                0.82f
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
