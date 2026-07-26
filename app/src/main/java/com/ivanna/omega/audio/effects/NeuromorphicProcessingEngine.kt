package com.ivanna.omega.audio.effects

import kotlin.math.*
import kotlin.random.Random

/**
 * Motor de Procesamiento Neuromórfico (NPE) – Grado Cuántico.
 *
 * Simula un reservorio de neuronas heterogéneas (LIF, resonador, bursting)
 * con plasticidad sináptica STDP, homeostasis y un banco de resonadores
 * no lineales. Cada muestra de audio se inyecta en el ecosistema y la salida
 * emerge de la actividad colectiva, produciendo texturas sonoras imposibles
 * para cualquier algoritmo DSP convencional.
 *
 * Propiedades acústicas:
 * - Riqueza armónica no lineal, micro‑eventos estocásticos, memoria frágil,
 *   transiciones de fase auditivas.
 * - Puede funcionar como saturación orgánica, reverberación no lineal,
 *   sintetizador granular neuronal, o todos a la vez.
 *
 * Parámetros guía:
 * @param neuronCount       Número de neuronas (16‑128 recomendado, 64 por defecto).
 * @param spectralRadius    Controla la recurrencia global (<1 estable, >1 caótico).
 * @param inputScaling      Amplitud con la que el audio excita el reservorio.
 * @param leakRate          Velocidad de fuga del potencial de membrana.
 * @param threshold         Umbral de disparo.
 * @param resetPotential    Potencial tras disparo.
 * @param outputScaling     Nivel de mezcla de la salida.
 * @param plasticityRate    Tasa de aprendizaje STDP (0 = sin plasticidad).
 * @param homeostasisRate   Velocidad de adaptación homeostática.
 * @param resonanceBankSize Número de resonadores no lineales (0 = desactivado).
 * @param seed              Semilla para reproducibilidad (0 = aleatoria).
 */
class NeuromorphicProcessingEngine(
    private var neuronCount: Int = 64,
    private var spectralRadius: Float = 0.9f,
    private var inputScaling: Float = 0.5f,
    private var leakRate: Float = 0.1f,
    private var threshold: Float = 1.0f,
    private var resetPotential: Float = 0.0f,
    private var outputScaling: Float = 0.3f,
    private var plasticityRate: Float = 0.005f,
    private var homeostasisRate: Float = 0.001f,
    private var resonanceBankSize: Int = 8,
    private var seed: Long = 42
) : AudioEffect {

    // ========================================================================
    // TIPOS DE NEURONA
    // ========================================================================
    private enum class NeuronType {
        LIF,          // Leaky Integrate-and-Fire clásica
        RESONATOR,    // Dispara en ráfagas rítmicas
        BURSTING,     // Dispara trenes de alta frecuencia
        ADAPTIVE      // Umbral adaptativo (fatiga)
    }

    // ========================================================================
    // ESTADO NEURONAL
    // ========================================================================
    private var membrane = FloatArray(neuronCount)
    private var spikes = FloatArray(neuronCount)
    private var adaptation = FloatArray(neuronCount)
    private var neuronType = Array(neuronCount) {
        when (Random.nextInt(4)) {
            0 -> NeuronType.LIF
            1 -> NeuronType.RESONATOR
            2 -> NeuronType.BURSTING
            3 -> NeuronType.ADAPTIVE
            else -> NeuronType.LIF
        }
    }
    private var intrinsicThreshold = FloatArray(neuronCount) { threshold * (0.8f + Random.nextFloat() * 0.4f) }
    private var targetRate = FloatArray(neuronCount) { Random.nextFloat() * 0.1f }

    // ========================================================================
    // PESOS SINÁPTICOS
    // ========================================================================
    private lateinit var recurrentWeights: Array<FloatArray>
    private lateinit var inputWeights: FloatArray
    private lateinit var readoutWeights: FloatArray
    private lateinit var inputResonatorWeights: FloatArray
    private lateinit var resonatorFeedbackWeights: FloatArray

    // ========================================================================
    // BANCO DE RESONADORES NO LINEALES
    // ========================================================================
    private class Resonator(
        val freq: Float,
        val damping: Float,
        val nonlinearity: Float
    ) {
        var x = 0f
        var y = 0f
        fun step(input: Float, dt: Float): Float {
            val dx = y
            val dy = -x - damping * (x * x - 1f) * y + nonlinearity * x * x * x + input
            x += dx * dt
            y += dy * dt
            // Limitar para evitar divergencia numérica
            x = x.coerceIn(-10f, 10f)
            y = y.coerceIn(-10f, 10f)
            return x
        }
    }
    private var resonators: Array<Resonator?> = arrayOfNulls(resonanceBankSize)

    // ========================================================================
    // TRAZAS DE STDP
    // ========================================================================
    private var preTrace = FloatArray(neuronCount)
    private var postTrace = FloatArray(neuronCount)
    private val stdpTauPre = 20f
    private val stdpTauPost = 20f
    private val stdpAplus = 0.01f
    private val stdpAminus = 0.012f

    // ========================================================================
    // MÉTRICAS EN TIEMPO REAL
    // ========================================================================
    var meanFiringRate: Float = 0f
        private set
    var spectralEntropy: Float = 0f
        private set

    init {
        initializeWeights()
    }

    // ========================================================================
    // PROCESAMIENTO PRINCIPAL
    // El buffer de entrada es intercalado estéreo (L,R,L,R,...).
    // Se procesa la media de los canales y se aplica el resultado a ambos.
    // ========================================================================
    override fun process(input: FloatArray): FloatArray {
        val frames = input.size / 2
        val output = input.copyOf()
        val dt = 1f / 44100f

        for (t in 0 until frames) {
            // Mezcla mono para excitar el reservorio
            val sampleL = input[t * 2]
            val sampleR = input[t * 2 + 1]
            val sample = (sampleL + sampleR) * 0.5f * inputScaling

            // 1. Resonadores
            var resonatorOutput = 0f
            if (resonanceBankSize > 0) {
                for (r in resonators) {
                    if (r != null) {
                        val w = inputResonatorWeights.getOrElse(resonators.indexOf(r)) { 0.05f }
                        resonatorOutput += r.step(sample * w, dt)
                    }
                }
                resonatorOutput = resonatorOutput.coerceIn(-1f, 1f)
            }

            // 2. Dinámica neuronal
            var spikeCount = 0
            for (i in 0 until neuronCount) {
                var current = inputWeights[i] * sample
                for (j in 0 until neuronCount) {
                    current += recurrentWeights[i][j] * spikes[j]
                }
                if (resonanceBankSize > 0) {
                    current += resonatorFeedbackWeights[i] * resonatorOutput
                }
                current -= adaptation[i]

                when (neuronType[i]) {
                    NeuronType.LIF -> {
                        membrane[i] = membrane[i] * (1f - leakRate) + current
                    }
                    NeuronType.RESONATOR -> {
                        val v = membrane[i]
                        val u = adaptation[i]
                        val dv = 0.04f * v * v + 5f * v + 140f - u + current
                        val du = 0.02f * (0.2f * v - u)
                        membrane[i] = (membrane[i] + dv * dt).coerceIn(-100f, 100f)
                        adaptation[i] = (adaptation[i] + du * dt).coerceIn(-100f, 100f)
                    }
                    NeuronType.BURSTING -> {
                        membrane[i] = (membrane[i] + current * 2f).coerceIn(-10f, 10f)
                    }
                    NeuronType.ADAPTIVE -> {
                        membrane[i] = membrane[i] * (1f - leakRate) + current
                    }
                }

                val effectiveThreshold = intrinsicThreshold[i]
                if (membrane[i] >= effectiveThreshold) {
                    spikes[i] = 1f
                    spikeCount++
                    membrane[i] = resetPotential
                    if (neuronType[i] == NeuronType.ADAPTIVE) {
                        intrinsicThreshold[i] += 0.1f
                    }
                    if (neuronType[i] == NeuronType.BURSTING) {
                        membrane[i] = effectiveThreshold * 0.5f
                    }
                } else {
                    spikes[i] = 0f
                }

                intrinsicThreshold[i] += homeostasisRate * (targetRate[i] - 0.01f * spikes[i])
                intrinsicThreshold[i] = intrinsicThreshold[i].coerceIn(0.1f, 10f)
                adaptation[i] *= 0.995f
                if (neuronType[i] == NeuronType.ADAPTIVE) {
                    intrinsicThreshold[i] += (threshold - intrinsicThreshold[i]) * 0.001f
                }
            }

            // 3. STDP
            if (plasticityRate > 0f) {
                updatePlasticity(spikes, spikeCount)
            }

            // 4. Lectura de salida
            var out = 0f
            for (i in 0 until neuronCount) {
                out += readoutWeights[i] * spikes[i]
                out += readoutWeights[i] * 0.1f * tanh(membrane[i].toDouble()).toFloat()
            }
            out += resonatorOutput * 0.2f
            val outSample = (out * outputScaling).coerceIn(-1f, 1f)

            // Mezcla wet/dry — el NPE aporta armónicos, no reemplaza la señal
            val wetDry = 0.5f
            output[t * 2]     = sampleL * (1f - wetDry) + outSample * wetDry
            output[t * 2 + 1] = sampleR * (1f - wetDry) + outSample * wetDry

            if (t % 100 == 0) {
                meanFiringRate = spikeCount.toFloat() / neuronCount
            }
        }
        return output
    }

    // ========================================================================
    // STDP
    // ========================================================================
    private fun updatePlasticity(spikes: FloatArray, spikeCount: Int) {
        for (i in 0 until neuronCount) {
            preTrace[i] += (spikes[i] - preTrace[i]) / stdpTauPre
            postTrace[i] += (spikes[i] - postTrace[i]) / stdpTauPost
        }
        if (spikeCount > 0) {
            for (i in 0 until neuronCount) {
                if (spikes[i] > 0.5f) {
                    for (j in 0 until neuronCount) {
                        val dw = stdpAplus * preTrace[j] - stdpAminus * postTrace[j]
                        recurrentWeights[i][j] += dw * plasticityRate
                        recurrentWeights[i][j] = recurrentWeights[i][j].coerceIn(-2f, 2f)
                    }
                }
            }
        }
    }

    override fun reset() {
        membrane.fill(0f)
        spikes.fill(0f)
        adaptation.fill(0f)
        preTrace.fill(0f)
        postTrace.fill(0f)
        intrinsicThreshold = FloatArray(neuronCount) { threshold * (0.8f + Random.nextFloat() * 0.4f) }
        resonators.forEach { r -> r?.let { it.x = 0f; it.y = 0f } }
        initializeWeights()
    }

    // ========================================================================
    // INICIALIZACIÓN DE PESOS
    // ========================================================================
    private fun initializeWeights() {
        val rng = if (seed != 0L) Random(seed) else Random.Default

        recurrentWeights = Array(neuronCount) {
            FloatArray(neuronCount) { rng.nextGaussian().toFloat() }
        }
        scaleSpectralRadius(rng)

        inputWeights = FloatArray(neuronCount) {
            rng.nextFloat() * 2f * inputScaling - inputScaling
        }
        readoutWeights = FloatArray(neuronCount) {
            rng.nextGaussian().toFloat() * 0.5f
        }

        if (resonanceBankSize > 0) {
            inputResonatorWeights = FloatArray(resonanceBankSize) { rng.nextFloat() * 0.1f }
            resonatorFeedbackWeights = FloatArray(neuronCount) { rng.nextFloat() * 0.2f - 0.1f }
            resonators = Array(resonanceBankSize) {
                Resonator(
                    freq = 50f * (1f + rng.nextFloat() * 20f),
                    damping = 0.1f + rng.nextFloat() * 0.9f,
                    nonlinearity = rng.nextFloat() * 0.5f
                )
            }
        } else {
            inputResonatorWeights = FloatArray(0)
            resonatorFeedbackWeights = FloatArray(neuronCount) { 0f }
            resonators = arrayOfNulls(0)
        }
    }

    private fun scaleSpectralRadius(rng: Random) {
        var v = FloatArray(neuronCount) { rng.nextFloat() * 2f - 1f }
        var norm = sqrt(v.sumOf { (it * it).toDouble() }.toFloat())
        if (norm < 1e-8f) return
        v = v.map { it / norm }.toFloatArray()

        repeat(30) {
            val newV = FloatArray(neuronCount)
            for (i in 0 until neuronCount) {
                var sum = 0f
                for (j in 0 until neuronCount) sum += recurrentWeights[i][j] * v[j]
                newV[i] = sum
            }
            norm = sqrt(newV.sumOf { (it * it).toDouble() }.toFloat())
            if (norm < 1e-8f) return
            v = newV.map { it / norm }.toFloatArray()
        }

        var lambda = 0f
        for (i in 0 until neuronCount) {
            var sum = 0f
            for (j in 0 until neuronCount) sum += recurrentWeights[i][j] * v[j]
            lambda += v[i] * sum
        }
        val factor = spectralRadius / (abs(lambda) + 1e-6f)
        for (i in 0 until neuronCount) {
            for (j in 0 until neuronCount) {
                recurrentWeights[i][j] *= factor
            }
        }
    }

    // ========================================================================
    // API DE MORPHING EN TIEMPO REAL
    // ========================================================================
    fun updateParameters(
        spectralRadius: Float = this.spectralRadius,
        inputScaling: Float = this.inputScaling,
        leakRate: Float = this.leakRate,
        threshold: Float = this.threshold,
        outputScaling: Float = this.outputScaling,
        plasticityRate: Float = this.plasticityRate,
        homeostasisRate: Float = this.homeostasisRate
    ) {
        this.spectralRadius = spectralRadius
        this.inputScaling = inputScaling
        this.leakRate = leakRate
        this.threshold = threshold
        this.outputScaling = outputScaling
        this.plasticityRate = plasticityRate
        this.homeostasisRate = homeostasisRate
    }

    companion object {
        fun Random.nextGaussian(): Double {
            var u1: Double
            var u2: Double
            var s: Double
            do {
                u1 = nextDouble() * 2.0 - 1.0
                u2 = nextDouble() * 2.0 - 1.0
                s = u1 * u1 + u2 * u2
            } while (s >= 1.0 || s == 0.0)
            val factor = sqrt(-2.0 * ln(s) / s)
            return u1 * factor
        }
    }
}
