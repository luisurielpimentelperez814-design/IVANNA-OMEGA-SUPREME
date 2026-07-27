package com.ivanna.omega.audio

object ParameterValidator {
    fun Float.safe(default: Float, min: Float, max: Float): Float =
        if (isFinite()) coerceIn(min, max) else default

    fun validateCompressor(threshold: Float, ratio: Float, attack: Float, release: Float) =
        Triple(
            threshold.safe(-30f, -60f, 0f),
            ratio.safe(4f, 1f, 20f)
        ) to Pair(
            attack.safe(10f, 0.1f, 500f),
            release.safe(100f, 1f, 2000f)
        )

    fun validateEQ(bass: Float, mid: Float, treble: Float, master: Float) =
        listOf(
            bass.safe(0f, -18f, 18f),
            mid.safe(0f, -18f, 18f),
            treble.safe(0f, -18f, 18f),
            master.safe(1f, 0.1f, 2f)
        )

    fun validateGain(gain: Float) = gain.safe(0f, -24f, 24f)
    fun validateExciter(exciter: Float) = exciter.safe(0.3f, 0f, 1f)
    fun validateWidth(width: Float) = width.safe(1f, 0f, 2f)
    fun validateSpatial(spatial: Float) = spatial.safe(1f, 0f, 2f)

    fun normalizeAntiDolbyScores(speech: Float, bass: Float): Triple<Float, Float, Float> {
        val sp = speech.safe(0.33f, 0f, 1f)
        val ba = bass.safe(0.33f, 0f, 1f)
        val mu = (1f - sp - ba).coerceIn(0f, 1f)
        val total = sp + ba + mu
        return if (total > 0f) {
            Triple(sp / total, mu / total, ba / total)
        } else {
            Triple(0.34f, 0.33f, 0.33f)
        }
    }
}
