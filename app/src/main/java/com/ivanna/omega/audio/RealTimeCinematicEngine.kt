package com.ivanna.omega.audio

import android.util.Log
import com.ivanna.omega.ai.AntiDolbyCrnnClassifier
import com.ivanna.omega.audio.effects.*
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger
import kotlin.concurrent.thread
import kotlin.math.abs

/**
 * RealTimeCinematicEngine — Clasificador CRNN + cadena DSP cinematográfica.
 * Arquitectura de audio de latencia ultra-baja y cero asignaciones en el hilo de audio:
 * - Comunicación hilo RT -> clasificador sin bloqueos (lock-free double exchange con AtomicBoolean).
 * - Procesado in-place sobre buffers estáticos preasignados (sin creación de FloatArray ni GC).
 * - Ventana deslizante de 5472 muestras en hilo de fondo para inferencia continua del CRNN.
 */
class RealTimeCinematicEngine(
    private val classifier: AntiDolbyCrnnClassifier,
    private val sampleRate: Int = 44100
) {
    enum class AudioMode { NONE, SCIFI, COSMIC, HORROR, VOID }

    private val reverb       = CinematicReverb()
    private val delay        = ModulatingDelay()
    private val formant      = FormantShifter()
    private val subHarmonic  = SubHarmonicGenerator()

    @Volatile private var targetMode:  AudioMode = AudioMode.NONE
    @Volatile private var currentMode: AudioMode = AudioMode.NONE

    val activeMode: AudioMode get() = currentMode

    private val transLen = (sampleRate * 0.05).toInt().coerceAtLeast(1)
    private var transCounter = 0

    private data class Params(
        var reverbRoom: Float = 0.8f, var reverbDamp: Float = 0.5f,
        var delayMs:    Float = 100f, var subMix:     Float = 0.5f)
    private val cur = Params(); private val tgt = Params()

    @Volatile private var bypassGain = 1.0f
    private var limEnv       = 0f
    private val limAttack    = 0.995f
    private val limRelease   = 0.9995f
    private val limThreshold = 0.95f

    // Buffers preasignados de procesado DSP para evitar asignaciones en el hot path
    private val pingBuf = FloatArray(4096)
    private val pongBuf = FloatArray(4096)

    // Intercambio lock-free entre el hilo de audio y el hilo de clasificación
    private val exchangeReady = AtomicBoolean(false)
    private val exchangeCount = AtomicInteger(0)
    private val exchangeBuf   = FloatArray(4096)

    // Buffers dedicados al hilo clasificador
    private val classifierWorkBuf = FloatArray(4096)
    private val resampleWorkBuf   = FloatArray(2048)
    private val ring16k           = FloatArray(AntiDolbyCrnnClassifier.INPUT_LENGTH)
    private var ring16kFilled     = 0

    @Volatile private var running = false
    private var thread: Thread? = null

    /** Invocado cuando cambia el modo — útil para actualizar UI / scores nativos */
    var onModeChanged: ((AudioMode, AntiDolbyCrnnClassifier.ClassificationResult) -> Unit)? = null

    companion object { private const val TAG = "CinematicEngine" }

    fun start() {
        if (running) return
        running = true
        ring16kFilled = 0
        exchangeReady.set(false)
        thread = thread(name = "CinematicClassifier", isDaemon = true, priority = Thread.MIN_PRIORITY) {
            while (running) {
                try {
                    Thread.sleep(50)
                } catch (_: InterruptedException) {
                    break
                }
                if (exchangeReady.get()) {
                    val count = exchangeCount.get().coerceIn(0, classifierWorkBuf.size)
                    System.arraycopy(exchangeBuf, 0, classifierWorkBuf, 0, count)
                    exchangeReady.set(false)

                    // Downsample mono a 16kHz hacia resampleWorkBuf
                    val step = (sampleRate / AntiDolbyCrnnClassifier.SAMPLE_RATE.toFloat()).coerceAtLeast(1f)
                    var outIdx = 0
                    var srcIdx = 0f
                    while (srcIdx < count && outIdx < resampleWorkBuf.size) {
                        resampleWorkBuf[outIdx++] = classifierWorkBuf[srcIdx.toInt().coerceIn(0, count - 1)]
                        srcIdx += step
                    }

                    // Acumulación en ventana deslizante ring16k (5472 muestras para CRNN)
                    val newCount = outIdx
                    if (newCount >= ring16k.size) {
                        System.arraycopy(resampleWorkBuf, newCount - ring16k.size, ring16k, 0, ring16k.size)
                        ring16kFilled = ring16k.size
                    } else if (newCount > 0) {
                        val shift = ring16k.size - newCount
                        System.arraycopy(ring16k, newCount, ring16k, 0, shift)
                        System.arraycopy(resampleWorkBuf, 0, ring16k, shift, newCount)
                        ring16kFilled = minOf(ring16k.size, ring16kFilled + newCount)
                    }

                    if (ring16kFilled >= ring16k.size) {
                        val r = classifier.classify(ring16k)
                        if (r.isValid) updateMode(r)
                    }
                }
            }
        }
        Log.i(TAG, "Engine iniciado")
    }

    fun stop() {
        running = false
        thread?.interrupt(); thread = null
        reverb.reset(); delay.reset(); formant.reset(); subHarmonic.reset()
        Log.i(TAG, "Engine detenido")
    }

    fun processBlock(input: FloatArray, output: FloatArray = input, count: Int = input.size): FloatArray {
        val n = minOf(count, input.size, output.size, pingBuf.size)
        if (n <= 0) return output

        // Enlace asíncrono lock-free con el clasificador
        if (!exchangeReady.get()) {
            val copyCount = minOf(n, exchangeBuf.size)
            System.arraycopy(input, 0, exchangeBuf, 0, copyCount)
            exchangeCount.set(copyCount)
            exchangeReady.set(true)
        }

        val mode = currentMode
        if (mode == AudioMode.NONE && bypassGain >= 0.999f && transCounter <= 0) {
            if (output !== input) {
                System.arraycopy(input, 0, output, 0, n)
            }
            return output
        }

        // Aplicar la cadena DSP usando buffers ping-pong preasignados (cero GC)
        val chainOut = applyEffectChainZeroAlloc(input, n, mode)

        if (transCounter > 0) {
            interpolateParams()
            transCounter--
        }

        val bg = bypassGain
        val wet = 1f - bg
        for (i in 0 until n) {
            output[i] = input[i] * bg + chainOut[i] * wet
        }

        applyLimiter(output, n)
        return output
    }

    private fun applyEffectChainZeroAlloc(input: FloatArray, count: Int, mode: AudioMode): FloatArray {
        return when (mode) {
            AudioMode.NONE -> input
            AudioMode.SCIFI -> {
                formant.process(input, pingBuf, count)
                reverb.process(pingBuf, pongBuf, count)
                pongBuf
            }
            AudioMode.COSMIC -> {
                delay.process(input, pingBuf, count)
                reverb.process(pingBuf, pongBuf, count)
                pongBuf
            }
            AudioMode.HORROR -> {
                subHarmonic.process(input, pingBuf, count)
                reverb.process(pingBuf, pongBuf, count)
                pongBuf
            }
            AudioMode.VOID -> {
                subHarmonic.process(input, pingBuf, count)
                delay.process(pingBuf, pongBuf, count)
                reverb.process(pongBuf, pingBuf, count)
                pingBuf
            }
        }
    }

    private fun interpolateParams() {
        fun lerp(a: Float, b: Float) = a + (b - a) * (1f - transCounter.toFloat() / transLen)
        cur.reverbRoom = lerp(cur.reverbRoom, tgt.reverbRoom)
        cur.reverbDamp = lerp(cur.reverbDamp, tgt.reverbDamp)
        reverb.updateParameters(cur.reverbRoom, cur.reverbDamp)
    }

    private fun applyLimiter(buf: FloatArray, count: Int) {
        for (i in 0 until count) {
            val a = abs(buf[i])
            limEnv = if (a > limEnv) 1f - (1f - a) * limAttack else limEnv * limRelease
            if (limEnv > limThreshold) {
                buf[i] *= limThreshold / limEnv
            }
        }
    }

    private fun updateMode(r: AntiDolbyCrnnClassifier.ClassificationResult) {
        val new = when {
            r.speech  > 0.50f -> AudioMode.SCIFI
            r.music   > 0.50f -> AudioMode.COSMIC
            r.bass    > 0.50f -> AudioMode.HORROR
            r.silence > 0.70f -> AudioMode.NONE
            else               -> AudioMode.VOID
        }
        if (new != targetMode) {
            targetMode = new
            currentMode = new
            bypassGain = if (new == AudioMode.NONE) 1f else 0f
            transCounter = transLen
            Log.d(TAG, "Modo → $new [sp=${r.speech} mu=${r.music} ba=${r.bass} si=${r.silence}]")
            onModeChanged?.invoke(new, r)
        }
    }
}