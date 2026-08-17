package com.ivanna.omega.audio

import android.util.Log
import com.ivanna.omega.ai.AntiDolbyCrnnClassifier
import com.ivanna.omega.audio.effects.*
import kotlin.concurrent.thread
import kotlin.math.abs
import kotlin.math.floor

/**
 * RealTimeCinematicEngine — Clasificador CRNN + cadena DSP cinematográfica.
 * Corre clasificación asíncrona cada 50 ms y adapta modo + parámetros en tiempo real.
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

    private val transLen = (sampleRate * 0.05).toInt()
    private var transCounter = 0

    private data class Params(
        var reverbRoom: Float = 0.8f, var reverbDamp: Float = 0.5f,
        var delayMs:    Float = 100f, var subMix:     Float = 0.5f)
    private val cur = Params(); private val tgt = Params()

    private var bypassGain   = 1.0f
    private var limEnv       = 0f
    private val limAttack    = 0.995f
    private val limRelease   = 0.9995f
    private val limThreshold = 0.95f

    private val lock  = Any()
    private var latestBuf: FloatArray = FloatArray(1024)
    private var bufReady  = false
    @Volatile private var running = false
    private var thread: Thread? = null

    /** Invocado cuando cambia el modo — útil para actualizar UI / scores nativos */
    var onModeChanged: ((AudioMode, AntiDolbyCrnnClassifier.ClassificationResult) -> Unit)? = null

    companion object { private const val TAG = "CinematicEngine" }

    fun start() {
        if (running) return
        running = true
        thread = thread(name="CinematicClassifier", isDaemon=true) {
            while (running) {
                Thread.sleep(50)
                var toClassify: FloatArray? = null
                synchronized(lock) { if (bufReady) { toClassify = latestBuf.copyOf(); bufReady = false } }
                toClassify?.let { buf ->
                    val resamp = if (sampleRate != AntiDolbyCrnnClassifier.SAMPLE_RATE)
                        resampleTo16k(buf) else buf
                    if (resamp.size >= AntiDolbyCrnnClassifier.INPUT_LENGTH) {
                        val r = classifier.classify(resamp)
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

    fun processBlock(input: FloatArray): FloatArray {
        synchronized(lock) { if (!bufReady) { latestBuf = input.copyOf(); bufReady = true } }
        val processed = applyEffectChain(input, currentMode)
        if (transCounter > 0) { interpolateParams(); transCounter-- }
        val out = FloatArray(input.size) { i -> input[i]*bypassGain + processed[i]*(1f-bypassGain) }
        applyLimiter(out)
        return out
    }

    private fun applyEffectChain(input: FloatArray, mode: AudioMode) = when (mode) {
        AudioMode.NONE   -> input.copyOf()
        AudioMode.SCIFI  -> reverb.process(formant.process(input))
        AudioMode.COSMIC -> reverb.process(delay.process(input))
        AudioMode.HORROR -> reverb.process(subHarmonic.process(input))
        AudioMode.VOID   -> reverb.process(delay.process(subHarmonic.process(input)))
    }

    private fun interpolateParams() {
        fun lerp(a: Float, b: Float) = a + (b-a)*(1f - transCounter.toFloat()/transLen)
        cur.reverbRoom = lerp(cur.reverbRoom, tgt.reverbRoom)
        cur.reverbDamp = lerp(cur.reverbDamp, tgt.reverbDamp)
        reverb.updateParameters(cur.reverbRoom, cur.reverbDamp)
    }

    private fun applyLimiter(buf: FloatArray) {
        for (i in buf.indices) {
            val a = abs(buf[i])
            limEnv = if (a > limEnv) 1f-(1f-a)*limAttack else limEnv*limRelease
            if (limEnv > limThreshold) buf[i] *= limThreshold/limEnv
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
            targetMode = new; currentMode = new
            bypassGain = if (new == AudioMode.NONE) 1f else 0f
            transCounter = transLen
            Log.d(TAG, "Modo → $new [sp=${r.speech} mu=${r.music} ba=${r.bass} si=${r.silence}]")
            onModeChanged?.invoke(new, r)
        }
    }

    private fun resampleTo16k(input: FloatArray): FloatArray {
        val ratio = AntiDolbyCrnnClassifier.SAMPLE_RATE.toFloat() / sampleRate
        val outSz  = (input.size * ratio).toInt()
        return FloatArray(outSz) { i ->
            input[(i / ratio).toInt().coerceIn(0, input.size-1)]
        }
    }
}