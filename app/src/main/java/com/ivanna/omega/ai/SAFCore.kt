package com.ivanna.omega.ai

/**
 * SAF - Self Adaptive Field Core
 *
 * Implementación del modelo:
 *
 * Φ_SAF =
 * Π_S^Gt(
 * p_t +
 * ΔE_t/(ΔE_t + ||Δ_t||Gt² + λM_t + ε)
 * Gt^-1 Δ_t
 * )
 *
 * Control adaptativo perceptual para IVANNA OMEGA.
 */
object SAFCore {

    private var memory = 0.0

    private const val LAMBDA = 0.05
    private const val EPSILON = 0.00001

    fun update(
        current: DoubleArray,
        target: DoubleArray,
        metric: DoubleArray
    ): DoubleArray {

        require(
            current.size == target.size &&
            target.size == metric.size
        )

        val delta = DoubleArray(current.size)

        for (i in current.indices) {
            delta[i] = target[i] - current[i]
        }

        var deltaEnergy = 0.0
        var metricNorm = 0.0

        for (i in delta.indices) {

            val weighted =
                delta[i] * metric[i] * delta[i]

            deltaEnergy += weighted
            metricNorm += weighted
        }

        memory =
            0.9 * memory +
            0.1 * kotlin.math.sqrt(metricNorm)

        val gain =
            deltaEnergy /
            (
                deltaEnergy +
                metricNorm +
                LAMBDA * memory +
                EPSILON
            )

        return DoubleArray(current.size) { i ->

            // Gt^-1 Δt
            val correction =
                if (metric[i] > 0)
                    delta[i] / metric[i]
                else
                    delta[i]

            current[i] + gain * correction
        }
    }


    fun reset() {
        memory = 0.0
    }


    fun getMemory(): Double {
        return memory
    }
}
