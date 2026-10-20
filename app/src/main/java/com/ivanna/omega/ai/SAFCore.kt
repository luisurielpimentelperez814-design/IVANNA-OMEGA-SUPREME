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


    // ── Φ_SAF-Room^∞ — Room-aware step ─────────────────────────────────────
    // Aplica M_t = G_t + λ_t·I con λ_t adaptativo según sala/HRTF/campo.
    // Wrapper de alto nivel sobre SaFRoomBridge para uso desde AdaptiveBackend
    // y PerceptualCortex sin importar el bridge directamente.
    //
    //   J(p) = 1/2 ||p - τ||² + λ/2 ||p - p₀||²
    //   λ = λ₀ exp(0.15·RT60 + HRTF_mismatch_energy + 0.40·diffuseness)
    //   p* = clip((τ + λ p₀)/(1 + λ), p_min, p_max)
    //
    // @return peso efectivo del target 1/(1+λ), ó 0f si el bridge falla.
    fun stepRoom(
        current:      DoubleArray,
        target:       DoubleArray,
        metric:       DoubleArray,
        rt60:         Float = 0.3f,   // T60 [0,5]s
        hMismatch:    Float = 0.0f,   // ‖H_error‖ [0,1]
        diffuseness:  Float = 0.0f    // campo sonoro [0,1]
    ): Pair<DoubleArray, Float> {
        // 1. Actualizar contexto en C++
        com.ivanna.omega.saf.SaFRoomBridge.setRoomState(rt60, 6.0f, 0.0f)
        com.ivanna.omega.saf.SaFRoomBridge.setHrtfState(hMismatch, 0.0f)
        com.ivanna.omega.saf.SaFRoomBridge.setSoundFieldState(diffuseness, 0.0f)

        // 2. Fijar target en C++
        com.ivanna.omega.saf.SaFRoomBridge.setTarget(target.map { it.toFloat() }.toFloatArray())

        // 3. Solución MAP cuadrática cerrada en C++
        val alphaRoom = com.ivanna.omega.saf.SaFRoomBridge.step()

        // 4. También actualizar estado Kotlin (getState() sigue válido)
        val updated = update(current, target, metric)

        return Pair(updated, alphaRoom)
    }

}