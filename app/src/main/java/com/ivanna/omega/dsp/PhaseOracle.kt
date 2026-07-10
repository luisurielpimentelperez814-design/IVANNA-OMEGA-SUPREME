package com.ivanna.omega.dsp

import kotlin.math.*

/**
 * Phase Oracle — Corrección de fase en 10 bandas usando filtros all-pass.
 * Ajusta la coherencia de fase para mejorar la imagen estéreo y la claridad.
 */
class PhaseOracle(private val sampleRate: Float = 48000f) {
    private val filters = mutableListOf<AllpassFilter>()
    private val centerFreqs = floatArrayOf(32f, 64f, 125f, 250f, 500f, 1000f, 2000f, 4000f, 8000f, 16000f)

    init {
        for (freq in centerFreqs) {
            filters.add(AllpassFilter(sampleRate, freq, q = 1.0f))
        }
    }

    /**
     * Procesa un buffer estéreo intercalado (L,R,L,R,...)
     * @param buffer FloatArray intercalado
     * @param gains FloatArray de 10 valores entre -1 y 1 (desfase)
     */
    fun process(buffer: FloatArray, gains: FloatArray) {
        require(gains.size == filters.size) { "Se necesitan ${filters.size} ganancias" }
        // Procesar solo el canal izquierdo (índices pares) para mantener la coherencia
        for (i in filters.indices) {
            val gain = gains[i].coerceIn(-1f, 1f)
            filters[i].setGain(gain)
            // Aplicar all-pass solo al canal L (índices pares)
            for (j in 0 until buffer.size step 2) {
                buffer[j] = filters[i].processSample(buffer[j])
            }
        }
    }

    private class AllpassFilter(sampleRate: Float, centerFreq: Float, q: Float) {
        private var a0: Float = 0f
        private var a1: Float = 0f
        private var a2: Float = 0f
        private var b1: Float = 0f
        private var b2: Float = 0f
        private var x1 = 0f
        private var x2 = 0f
        private var y1 = 0f
        private var y2 = 0f
        private var gain = 0f

        init {
            updateCoefficients(centerFreq, q)
        }

        private fun updateCoefficients(freq: Float, q: Float) {
            val omega = 2f * PI.toFloat() * freq / sampleRate
            val alpha = sin(omega) / (2f * q)
            a0 = 1f + alpha
            a1 = -2f * cos(omega)
            a2 = 1f - alpha
            b1 = a1
            b2 = a2
        }

        fun setGain(g: Float) { gain = g.coerceIn(-1f, 1f) }

        fun processSample(x: Float): Float {
            val y = (x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) / a0
            // Mezcla: salida = entrada + gain * (all-pass - entrada)
            val result = x + gain * (y - x)
            x2 = x1; x1 = x
            y2 = y1; y1 = y
            return result
        }
    }
}
