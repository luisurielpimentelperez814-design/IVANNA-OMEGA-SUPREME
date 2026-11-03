package com.ivanna.omega

import com.ivanna.omega.ai.PerceptualSnapshot
import com.ivanna.omega.core.IvannaNativeLib

// Extracto literal de MainActivity.kt en v2.5.339; no es código inventado.
/**
 * PerceptualDspRecommendations - Computed DSP output parameters from Perceptual Brain evaluation.
 */
data class PerceptualDspRecommendations(
    val compressorAmount: Float = 0.35f,
    val exciterReduction: Float = 0.15f,
    val eqHighCut: Float = 18000.0f,
    val spatialWidth: Float = 1.20f,
    val loudnessTarget: Float = -14.0f
)

/**
 * PerceptualDecisionEngine - Real-time Psychoacoustic & TinyML decision controller.
 * Evaluates fatigue, immersion, ISO 226 loudness, masking efficiency, dynamic range,
 * and ConvNeXt INT8 confidence to produce real-time native DSP adjustments.
 */
class PerceptualDecisionEngine {
    fun evaluate(snapshot: PerceptualSnapshot): PerceptualDspRecommendations {
        val fatigueFactor = snapshot.fatigue.coerceIn(0f, 1f)
        val immersionFactor = snapshot.immersion.coerceIn(0f, 1f)
        val loudnessIso226 = snapshot.iso226LoudnessDb
        val maskingEff = snapshot.maskingEfficiency
        val dynRange = snapshot.dynamicRangeDb
        val confidence = snapshot.convNextConfidence

        // Psychoacoustic rules calculation
        val exciterRed = (fatigueFactor * 0.5f + (1.0f - confidence) * 0.2f).coerceIn(0f, 0.8f)
        val highCut = if (fatigueFactor > 0.5f) (18000f - fatigueFactor * 4000f) else 20000f
        val compAmount = (0.2f + (1.0f - (dynRange / 24f).coerceIn(0f, 1f)) * 0.5f + fatigueFactor * 0.2f).coerceIn(0.1f, 0.9f)
        val width = (immersionFactor * 1.4f * (1.0f - fatigueFactor * 0.3f)).coerceIn(0.5f, 1.8f)
        val targetLoudness = (-14.0f + (loudnessIso226 - 84f) * 0.1f + maskingEff * 2.0f).coerceIn(-24.0f, -8.0f)

        return PerceptualDspRecommendations(
            compressorAmount = compAmount,
            exciterReduction = exciterRed,
            eqHighCut = highCut,
            spatialWidth = width,
            loudnessTarget = targetLoudness
        )
    }

    fun dispatchRecommendations(recommendations: PerceptualDspRecommendations) {
        if (IvannaNativeLib.isLoaded) {
            runCatching {
                IvannaNativeLib.nativeSetSpatialWidthDirect(recommendations.spatialWidth)
                IvannaNativeLib.nativeSetHarmonicGain(1.0f - recommendations.exciterReduction)
                IvannaNativeLib.nativeSetAntiDolbyIntensity(recommendations.compressorAmount)
            }
        }
    }
}
