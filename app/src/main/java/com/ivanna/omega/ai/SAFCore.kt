package com.ivanna.omega.ai

import kotlin.math.sqrt
import kotlin.math.max
import kotlin.math.min

/**
 * SAF Φ∞
 *
 * Φ = ΠS^Gt(
 * p + ΔE/(ΔE+||Δ||Gt²+λM+ε) Gt^-1Δ
 * )
 */
object SAFCore {

    private var memory = 0.0

    private var lastDeltaEnergy = 0.0
    private var lastMetricNorm = 0.0
    private var lastGain = 0.0

    private const val LAMBDA = 0.05
    private const val EPSILON = 0.00001

    fun update(
        current: DoubleArray,
        target: DoubleArray,
        metric: DoubleArray
    ): DoubleArray {

        val delta = DoubleArray(current.size)

        var deltaEnergy = 0.0
        var normGt = 0.0

        for(i in delta.indices){
            delta[i] = target[i] - current[i]

            deltaEnergy += kotlin.math.abs(delta[i])

            normGt +=
                delta[i] *
                metric[i] *
                delta[i]
        }

        memory =
            0.95 * memory +
            0.05 * sqrt(normGt)

        lastDeltaEnergy = deltaEnergy
        lastMetricNorm = normGt

        val gain =
            deltaEnergy /
            (
                deltaEnergy +
                normGt +
                LAMBDA * memory +
                EPSILON
            )

        lastGain = gain

        return DoubleArray(current.size){

            val corrected =
                current[it] +
                gain *
                delta[it] /
                max(metric[it], EPSILON)

            project(corrected, it)
        }
    }


    private fun project(
        value: Double,
        index: Int
    ): Double {

        return when(index){

            0 -> min(max(value,0.0),1.0)
            1 -> min(max(value,0.0),1.0)
            2 -> min(max(value,2000.0),20000.0)
            3 -> min(max(value,0.0),2.0)

            else -> value
        }
    }

    fun getState(): DoubleArray {
        return doubleArrayOf(
            lastDeltaEnergy,
            lastMetricNorm,
            memory,
            lastGain
        )
    }

}
