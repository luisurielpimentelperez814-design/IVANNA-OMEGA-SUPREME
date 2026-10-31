package com.ivanna.omega.audio

/**
 * Measurement-free robust compromise for the Genezi AUX route.
 *
 * Focus and immersion are competing policy targets, not measured room responses:
 * focusTarget preserves the stable center (unity route multiplier), while
 * immersionTarget is the existing conservative AUX expansion proposal.
 *
 * The unique minimizer of
 *   min_w max((w - focusTarget)^2, (w - immersionTarget)^2)
 * over [lowerBound, upperBound] is the midpoint of the two targets projected
 * onto the allowed interval. This is a minimax policy compromise, not room
 * inversion, HRTF calibration, or a claim of physical source relocation.
 */
object AtomicSpatialPolicy {
    const val FOCUS_TARGET = 1.0f
    const val IMMERSION_TARGET = 1.08f
    const val LOWER_BOUND = 1.0f
    const val UPPER_BOUND = 1.08f

    fun geneziSpatialMultiplier(): Float = minimaxCompromise(
        focusTarget = FOCUS_TARGET,
        immersionTarget = IMMERSION_TARGET,
        lowerBound = LOWER_BOUND,
        upperBound = UPPER_BOUND
    )

    fun minimaxCompromise(
        focusTarget: Float,
        immersionTarget: Float,
        lowerBound: Float,
        upperBound: Float
    ): Float {
        if (!focusTarget.isFinite() || !immersionTarget.isFinite() ||
            !lowerBound.isFinite() || !upperBound.isFinite() ||
            lowerBound > upperBound
        ) return FOCUS_TARGET

        val midpoint = focusTarget + (immersionTarget - focusTarget) * 0.5f
        return midpoint.coerceIn(lowerBound, upperBound)
    }

    fun worstCaseSquaredError(value: Float, focusTarget: Float, immersionTarget: Float): Float {
        if (!value.isFinite() || !focusTarget.isFinite() || !immersionTarget.isFinite()) {
            return Float.POSITIVE_INFINITY
        }
        val focusError = value - focusTarget
        val immersionError = value - immersionTarget
        return maxOf(focusError * focusError, immersionError * immersionError)
    }
}
