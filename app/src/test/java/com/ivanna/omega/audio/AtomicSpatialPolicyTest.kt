package com.ivanna.omega.audio

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class AtomicSpatialPolicyTest {
    @Test fun geneziPolicyIsExactMinimaxCompromiseOfFocusAndImmersion() {
        val result = AtomicSpatialPolicy.geneziSpatialMultiplier()
        assertEquals(1.04f, result, 1.0e-6f)
        assertEquals(
            0.0016f,
            AtomicSpatialPolicy.worstCaseSquaredError(
                result,
                AtomicSpatialPolicy.FOCUS_TARGET,
                AtomicSpatialPolicy.IMMERSION_TARGET
            ),
            1.0e-6f
        )
    }

    @Test fun midpointMinimizesWorstCaseSquaredErrorOnTheAllowedInterval() {
        val focus = 1.0f
        val immersion = 1.08f
        val result = AtomicSpatialPolicy.minimaxCompromise(focus, immersion, 1.0f, 1.08f)
        for (i in 0..800) {
            val candidate = 1.0f + i * 0.0001f
            assertTrue(
                AtomicSpatialPolicy.worstCaseSquaredError(result, focus, immersion) <=
                    AtomicSpatialPolicy.worstCaseSquaredError(candidate, focus, immersion) + 1.0e-7f
            )
        }
    }

    @Test fun projectedSolutionRespectsHardBounds() {
        assertEquals(1.02f, AtomicSpatialPolicy.minimaxCompromise(0.8f, 1.24f, 1.0f, 1.08f), 1.0e-6f)
        assertEquals(1.08f, AtomicSpatialPolicy.minimaxCompromise(1.08f, 1.2f, 1.0f, 1.08f), 1.0e-6f)
    }

    @Test fun invalidInputsFailClosedToUnity() {
        assertEquals(1.0f, AtomicSpatialPolicy.minimaxCompromise(Float.NaN, 1.08f, 1.0f, 1.08f), 0f)
        assertEquals(1.0f, AtomicSpatialPolicy.minimaxCompromise(1.0f, 1.08f, 2.0f, 1.0f), 0f)
        assertEquals(
            Float.POSITIVE_INFINITY,
            AtomicSpatialPolicy.worstCaseSquaredError(Float.NaN, 1.0f, 1.08f),
            0f
        )
    }
}
