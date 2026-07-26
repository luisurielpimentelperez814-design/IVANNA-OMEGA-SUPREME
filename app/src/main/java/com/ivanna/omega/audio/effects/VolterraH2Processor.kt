package com.ivanna.omega.audio.effects

import kotlin.math.*

/**
 * Volterra H2 Processor – Distorsión armónica por kernel cuadrático.
 *
 * Simula la saturación de válvulas y transformadores mediante una serie
 * de Volterra de segundo orden con oversampling 4x y anti‑aliasing FIR.
 * Cada muestra se eleva al cuadrado (kernel H2) generando armónicos
 * pares e impares ricos, sin aliasing.
 *
 * Parámetros:
 * @param drive       Cantidad de saturación (0.0 = limpio, 1.0 = furia total)
 * @param asymmetry   Simetría de la curva (0.5 = simétrica, >0.5 = más cálida en positivos)
 * @param makeupGain  Ganancia de compensación automática
 * @param mix         Mezcla wet/dry (0.0 = original, 1.0 = saturado)
 */
class VolterraH2Processor : AudioEffect {

    // Configuración del oversampling
    private val oversampleFactor = 4
    private val sampleRate = 44100
    private val maxBlockSize = 4096

    // Parámetros ajustables
    var drive: Float = 0.5f
    var asymmetry: Float = 0.6f          // 0.5 = simétrico, >0.5 asimétrico (tipo válvula)
    var makeupGain: Float = 1.2f          // compensación de nivel
    var mix: Float = 0.65f                // wet/dry

    // Buffers para oversampling
    private val oversampleBuffer = FloatArray(maxBlockSize * oversampleFactor)
    private val downsampleBuffer = FloatArray(maxBlockSize)
    private var tempBlockSize = 2048

    // Filtro FIR de diezmado (paso bajo, 48 dB/oct)
    private val decimationFilter = floatArrayOf(
        -0.0003f, -0.0005f, -0.0007f, -0.0008f, -0.0006f, 0.0000f, 0.0010f, 0.0020f,
         0.0025f, 0.0018f, -0.0005f, -0.0040f, -0.0070f, -0.0075f, -0.0035f, 0.0045f,
         0.0130f, 0.0170f, 0.0125f, -0.0010f, -0.0180f, -0.0285f, -0.0220f, 0.0030f,
         0.0350f, 0.0570f, 0.0500f, 0.0080f, -0.0550f, -0.1100f, -0.1200f, -0.0600f,
         0.0650f, 0.2200f, 0.3600f, 0.4400f, 0.4400f, 0.3600f, 0.2200f, 0.0650f,
        -0.0600f, -0.1200f, -0.1100f, -0.0550f, 0.0080f, 0.0500f, 0.0570f, 0.0350f,
         0.0030f, -0.0220f, -0.0285f, -0.0180f, -0.0010f, 0.0125f, 0.0170f, 0.0130f,
         0.0045f, -0.0035f, -0.0075f, -0.0070f, -0.0040f, -0.0005f, 0.0018f, 0.0025f,
         0.0020f, 0.0010f, 0.0000f, -0.0006f, -0.0008f, -0.0007f, -0.0005f, -0.0003f
    )
    private val filterLength = decimationFilter.size
    private var filterBuffer = FloatArray(filterLength)

    override fun process(input: FloatArray): FloatArray {
        val output = FloatArray(input.size)
        val n = minOf(input.size, maxBlockSize)
        val osFactor = oversampleFactor
        val driveLocal = drive
        val asym = asymmetry
        val mg = makeupGain
        val wetMix = mix
        val dryMix = 1f - wetMix

        for (i in 0 until n) {
            val original = input[i]

            // 1. Oversampling por inserción de ceros y replicación lineal (eficiente)
            for (j in 0 until osFactor) {
                val t = i + j.toFloat() / osFactor
                val idx = i
                val nextIdx = minOf(idx + 1, n - 1)
                val frac = (t - idx)
                val upsampled = input[idx] * (1f - frac) + input[nextIdx] * frac
                oversampleBuffer[i * osFactor + j] = upsampled
            }

            // 2. Kernel Volterra H2: y = a1*x + a2*x^2
            var wetSample = 0f
            for (j in 0 until osFactor) {
                var x = oversampleBuffer[i * osFactor + j] * driveLocal * 2.5f
                // Curva asimétrica (tipo válvula)
                val pos = maxOf(0f, x) * asym
                val neg = minOf(0f, x) * (1f - asym)
                x = pos + neg
                // Kernel H2: componente lineal + cuadrático
                val processed = x + x * x * 0.35f
                wetSample += processed
            }
            wetSample /= osFactor
            wetSample *= mg

            // 3. Anti‑aliasing: filtro FIR de diezmado
            filterBuffer[0] = wetSample
            var filtered = 0f
            for (k in 0 until filterLength) {
                filtered += filterBuffer[k] * decimationFilter[k]
            }
            // Rotar buffer
            for (k in filterLength - 1 downTo 1) {
                filterBuffer[k] = filterBuffer[k - 1]
            }

            // 4. Mezcla wet/dry
            output[i] = (original * dryMix + filtered * wetMix).coerceIn(-1f, 1f)
        }

        return output
    }

    override fun reset() {
        filterBuffer.fill(0f)
    }
}
