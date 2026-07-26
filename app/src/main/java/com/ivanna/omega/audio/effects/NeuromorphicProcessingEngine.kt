package com.ivanna.omega.audio.effects

import kotlin.math.*
import kotlin.random.Random

/**
 * Motor de Procesamiento Neuromórfico (NPE) – Grado Absoluto.
 *
 * Simula un ecosistema neuronal con plasticidad STDP, homeostasis, banco de
 * resonadores caóticos y ecualizador de formantes neuronales. Cada muestra de
 * audio es absorbida por una población heterogénea de neuronas LIF, resonadoras,
 * bursting y adaptativas, generando una firma acústica imposible de replicar
 * con DSP clásico.
 *
 * Nuevas capacidades sobre la versión anterior:
 * - Control de caos independiente en resonadores.
 * - Modo de salida seleccionable: spikes, membrana o mixto.
 * - Congelación de estado (freeze) para drones infinitos.
 * - Ecualizador vocal neuronal: filtros de formantes sintetizados por neuronas.
 *
 * Parámetros:
 * @param neuronCount         Número de neuronas (16-256).
 * @param spectralRadius      Radio espectral recurrente (<1 estable, >1 caótico).
 * @param inputScaling        Escala de entrada.
 * @param leakRate            Tasa de fuga (memoria).
 * @param threshold           Umbral de disparo.
 * @param outputScaling       Nivel de salida.
 * @param plasticityRate      Tasa de aprendizaje STDP.
 * @param homeostasisRate     Velocidad de homeostasis.
 * @param resonanceBankSize   Resonadores no lineales (0-32).
 * @param chaos               No linealidad de resonadores (0-1).
 * @param outputMode          0=spikes, 1=membrane, 2=mixed.
 * @param freeze              Congela el estado (no actualiza membrana).
 * @param vocalEqEnabled      Activa el ecualizador de formantes.
 * @param seed                Semilla.
 */
class NeuromorphicProcessingEngine(
    var neuronCount: Int = 64,
    var spectralRadius: Float = 0.9f,
    var inputScaling: Float = 0.5f,
    var leakRate: Float = 0.1f,
    var threshold: Float = 1.0f,
    var outputScaling: Float = 0.3f,
    var plasticityRate: Float = 0.005f,
    var homeostasisRate: Float = 0.001f,
    var resonanceBankSize: Int = 8,
    var chaos: Float = 0.3f,
    var outputMode: Int = 0, // 0 spikes, 1 membrane, 2 mixed
    var freeze: Boolean = false,
    var vocalEqEnabled: Boolean = false,
    var seed: Long = 42
) : AudioEffect {

    // Tipos de neurona
    enum class NeuronType { LIF, RESONATOR, BURSTING, ADAPTIVE }

    // Estado neuronal
    private var membrane = FloatArray(neuronCount)
    private var spikes = FloatArray(neuronCount)
    private var adaptation = FloatArray(neuronCount)
    private var neuronType = Array(neuronCount) {
        NeuronType.values()[Random.nextInt(4)]
    }
    private var intrinsicThreshold = FloatArray(neuronCount) { threshold * (0.8f + Random.nextFloat() * 0.4f) }
    private var targetRate = FloatArray(neuronCount) { Random.nextFloat() * 0.1f }

    // Pesos sinápticos
    private lateinit var recurrentWeights: Array<FloatArray>
    private lateinit var inputWeights: FloatArray
    private lateinit var readoutWeights: FloatArray
    private lateinit var inputResonatorWeights: FloatArray
    private lateinit var resonatorFeedbackWeights: FloatArray

    // Resonadores
    class Resonator(var x: Float = 0f, var y: Float = 0f) {
        fun step(input: Float, damping: Float, nonlinearity: Float, dt: Float): Float {
            val dx = y
            val dy = -x - damping * (x * x - 1f) * y + nonlinearity * x * x * x + input
            x += dx * dt
            y += dy * dt
            return x
        }
    }
    private var resonators: Array<Resonator?> = arrayOfNulls(resonanceBankSize)
    private var resonatorDamping = FloatArray(resonanceBankSize) { 0.2f + Random.nextFloat() * 0.8f }
    private var resonatorNonlin = FloatArray(resonanceBankSize) { chaos * Random.nextFloat() }

    // Trazas STDP
    private var preTrace = FloatArray(neuronCount)
    private var postTrace = FloatArray(neuronCount)
    private val stdpTauPre = 20f
    private val stdpTauPost = 20f
    private val stdpAplus = 0.01f
    private val stdpAminus = 0.012f

    // Ecualizador vocal (5 formantes típicos)
    private val vocalFreqs = floatArrayOf(270f, 2300f, 3000f, 3500f, 4000f)
    private val vocalGains = FloatArray(5) { 0f }
    private val vocalPhases = FloatArray(5)
    private var vocalMix = 0.2f

    // Buffers pre‑allocados
    private var blockSize = 2048
    private var tempSpikeOut = FloatArray(blockSize)
    private var tempMemOut = FloatArray(blockSize)
    private var tempResOut = FloatArray(blockSize)

    // Estadísticas
    var meanFiringRate: Float = 0f; private set
    var spectralEntropy: Float = 0f; private set

    init { initializeWeights() }

    override fun process(input: FloatArray): FloatArray {
        val output = FloatArray(input.size)
        val dt = 1f / 44100f

        for (t in input.indices) {
            if (freeze) {
                // Mantener la última salida (drone)
                output[t] = if (t > 0) output[t - 1] else 0f
                continue
            }

            val sample = input[t] * inputScaling

            // Resonadores
            var resonatorOutput = 0f
            for (i in 0 until resonanceBankSize) {
                resonators[i]?.let {
                    resonatorOutput += it.step(
                        sample * inputResonatorWeights[i],
                        resonatorDamping[i],
                        resonatorNonlin[i],
                        dt
                    )
                }
            }

            // Corrientes neuronales
            var spikeCount = 0
            for (i in 0 until neuronCount) {
                var current = inputWeights[i] * sample
                for (j in 0 until neuronCount) current += recurrentWeights[i][j] * spikes[j]
                if (resonanceBankSize > 0) current += resonatorFeedbackWeights[i] * resonatorOutput
                current -= adaptation[i]

                when (neuronType[i]) {
                    NeuronType.LIF -> membrane[i] = membrane[i] * (1f - leakRate) + current
                    NeuronType.RESONATOR -> {
                        val v = membrane[i]; val u = adaptation[i]
                        val dv = 0.04f * v * v + 5f * v + 140f - u + current
                        val du = 0.02f * (0.2f * v - u)
                        membrane[i] += dv.toFloat() * dt
                        adaptation[i] += du.toFloat() * dt
                    }
                    NeuronType.BURSTING -> membrane[i] += current * 2f
                    NeuronType.ADAPTIVE -> membrane[i] = membrane[i] * (1f - leakRate) + current
                }

                if (membrane[i] >= intrinsicThreshold[i]) {
                    spikes[i] = 1f; spikeCount++
                    membrane[i] = resetPotential
                    if (neuronType[i] == NeuronType.ADAPTIVE) intrinsicThreshold[i] += 0.1f
                    if (neuronType[i] == NeuronType.BURSTING) membrane[i] = intrinsicThreshold[i] * 0.5f
                } else spikes[i] = 0f

                intrinsicThreshold[i] += homeostasisRate * (targetRate[i] - 0.01f * spikes[i])
                adaptation[i] *= 0.995f
                if (neuronType[i] == NeuronType.ADAPTIVE) intrinsicThreshold[i] += (threshold - intrinsicThreshold[i]) * 0.001f
            }

            // STDP
            if (plasticityRate > 0f && spikeCount > 0) updatePlasticity()

            // Modos de salida
            val spikeContrib = (0 until neuronCount).sumOf { readoutWeights[it] * spikes[it].toDouble() }.toFloat()
            val memContrib = (0 until neuronCount).sumOf { readoutWeights[it] * tanh(membrane[it].toDouble()).toFloat() * 0.1f }.toFloat()

            var out = when (outputMode) {
                0 -> spikeContrib
                1 -> memContrib
                else -> spikeContrib * 0.7f + memContrib * 0.3f
            } + resonatorOutput * 0.2f

            // Ecualizador vocal (inyección de formantes)
            if (vocalEqEnabled) {
                var vocalOut = 0f
                for (k in 0 until 5) {
                    vocalPhases[k] += 2f * PI.toFloat() * vocalFreqs[k] / 44100f
                    vocalOut += sin(vocalPhases[k]) * vocalGains[k]
                }
                out += vocalOut * vocalMix
            }

            output[t] = (out * outputScaling).coerceIn(-1f, 1f)

            if (t % 100 == 0) meanFiringRate = spikeCount.toFloat() / neuronCount
        }
        return output
    }

    private fun updatePlasticity() {
        for (i in 0 until neuronCount) {
            preTrace[i] += (spikes[i] - preTrace[i]) / stdpTauPre
            postTrace[i] += (spikes[i] - postTrace[i]) / stdpTauPost
        }
        for (i in 0 until neuronCount) {
            if (spikes[i] > 0.5f) {
                for (j in 0 until neuronCount) {
                    val dw = stdpAplus * preTrace[j] - stdpAminus * postTrace[j]
                    recurrentWeights[i][j] = (recurrentWeights[i][j] + dw * plasticityRate).coerceIn(-2f, 2f)
                }
            }
        }
    }

    override fun reset() {
        membrane.fill(0f); spikes.fill(0f); adaptation.fill(0f)
        preTrace.fill(0f); postTrace.fill(0f)
        intrinsicThreshold = FloatArray(neuronCount) { threshold * (0.8f + Random.nextFloat() * 0.4f) }
        initializeWeights()
        resonators.forEach { it?.x = 0f; it?.y = 0f }
        vocalPhases.fill(0f)
    }

    private fun initializeWeights() {
        val rng = if (seed != 0L) Random(seed) else Random
        recurrentWeights = Array(neuronCount) { FloatArray(neuronCount) { rng.nextGaussian().toFloat() } }
        scaleSpectralRadius(rng)
        inputWeights = FloatArray(neuronCount) { rng.nextFloat() * 2f * inputScaling - inputScaling }
        readoutWeights = FloatArray(neuronCount) { rng.nextGaussian().toFloat() * 0.5f }
        if (resonanceBankSize > 0) {
            inputResonatorWeights = FloatArray(resonanceBankSize) { rng.nextFloat() * 0.1f }
            resonatorFeedbackWeights = FloatArray(neuronCount) { rng.nextFloat() * 0.2f - 0.1f }
            resonators = Array(resonanceBankSize) { Resonator() }
            resonatorDamping = FloatArray(resonanceBankSize) { 0.2f + rng.nextFloat() * 0.8f }
            resonatorNonlin = FloatArray(resonanceBankSize) { chaos * rng.nextFloat() }
        }
    }

    private fun scaleSpectralRadius(rng: Random) {
        var v = FloatArray(neuronCount) { rng.nextFloat() * 2f - 1f }
        var norm = sqrt(v.sumOf { it * it.toDouble() }.toFloat())
        v = v.map { it / norm }.toFloatArray()
        repeat(30) {
            val newV = FloatArray(neuronCount)
            for (i in 0 until neuronCount) {
                var sum = 0f
                for (j in 0 until neuronCount) sum += recurrentWeights[i][j] * v[j]
                newV[i] = sum
            }
            norm = sqrt(newV.sumOf { it * it.toDouble() }.toFloat())
            v = newV.map { it / (norm + 1e-8f) }.toFloatArray()
        }
        var lambda = 0f
        for (i in 0 until neuronCount) {
            var sum = 0f
            for (j in 0 until neuronCount) sum += recurrentWeights[i][j] * v[j]
            lambda += v[i] * sum
        }
        val factor = spectralRadius / (abs(lambda) + 1e-6f)
        for (i in 0 until neuronCount) for (j in 0 until neuronCount) recurrentWeights[i][j] *= factor
    }

    fun updateParameters(
        spectralRadius: Float = this.spectralRadius,
        inputScaling: Float = this.inputScaling,
        leakRate: Float = this.leakRate,
        threshold: Float = this.threshold,
        outputScaling: Float = this.outputScaling,
        plasticityRate: Float = this.plasticityRate,
        homeostasisRate: Float = this.homeostasisRate,
        chaos: Float = this.chaos,
        outputMode: Int = this.outputMode,
        freeze: Boolean = this.freeze,
        vocalEqEnabled: Boolean = this.vocalEqEnabled
    ) {
        this.spectralRadius = spectralRadius
        this.inputScaling = inputScaling
        this.leakRate = leakRate
        this.threshold = threshold
        this.outputScaling = outputScaling
        this.plasticityRate = plasticityRate
        this.homeostasisRate = homeostasisRate
        this.chaos = chaos
        this.outputMode = outputMode
        this.freeze = freeze
        this.vocalEqEnabled = vocalEqEnabled
    }

    companion object {
        fun Random.nextGaussian(): Double {
            var u1: Double; var u2: Double; var s: Double
            do {
                u1 = nextDouble() * 2.0 - 1.0
                u2 = nextDouble() * 2.0 - 1.0
                s = u1 * u1 + u2 * u2
            } while (s >= 1.0 || s == 0.0)
            return u1 * sqrt(-2.0 * ln(s) / s)
        }
    }
}
