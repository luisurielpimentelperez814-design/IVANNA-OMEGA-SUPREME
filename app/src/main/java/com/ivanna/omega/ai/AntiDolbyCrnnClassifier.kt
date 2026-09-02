package com.ivanna.omega.ai

import android.content.Context
import android.util.Log
import org.tensorflow.lite.Interpreter
import java.io.FileInputStream
import java.nio.MappedByteBuffer
import java.nio.channels.FileChannel
import kotlin.math.*

/**
 * AntiDolbyCrnnClassifier — CRNN Anti-Dolby entrenado in-house + refinamiento.
 *
 * Cambios respecto a la versión anterior:
 *   1. EMA temporal por clase: τ=200ms @ ~3 frames/s (evita cambios de clase
 *      a cada frame — la clase dominante ahora cambia suavemente).
 *   2. Detección de silencio por umbral de energía: si RMS < −60 dBFS,
 *      el frame se clasifica directamente como Silencio sin inferencia.
 *   3. Detección de transientes por onset (delta de energía de frame a frame):
 *      si el onset > umbral, la clase es Transitorio independientemente del
 *      resultado del CRNN (los transientes son subeventos de 10–50 ms que
 *      el CRNN de 342ms de ventana no puede detectar correctamente).
 *   4. El clasificador ahora produce 4 salidas canónicas con nombres en
 *      español consistentes con el C++ nativo.
 */
class AntiDolbyCrnnClassifier(context: Context) {

    companion object {
        private const val TAG = "AntiDolbyCRNN"
        private const val MODEL_PATH = "anti_dolby_crnn.tflite"

        const val SAMPLE_RATE  = 16000
        const val FRAME_LENGTH = 512
        const val HOP_LENGTH   = 160
        const val N_MELS       = 40
        const val TIME_FRAMES  = 32
        const val NUM_CLASSES  = 4
        const val INPUT_LENGTH = (TIME_FRAMES - 1) * HOP_LENGTH + FRAME_LENGTH  // 5472

        const val IDX_VOICE    = 0
        const val IDX_MUSIC    = 1
        const val IDX_BASS     = 2
        const val IDX_SILENCE  = 3

        val CLASS_NAMES = arrayOf("Voz", "Musica", "Bajos", "Silencio")

        private const val NUM_BINS = FRAME_LENGTH / 2 + 1  // 257

        // Umbral de silencio: −60 dBFS ≈ 0.001 de amplitud
        private const val SILENCE_RMS_THRESHOLD = 0.001f

        // Umbral de onset para detección de transientes
        // Empíricamente: un onset normalizado > 0.15 corresponde a un ataque
        // percutido (bombo, snare, palmada, efecto de sonido)
        private const val ONSET_TRANSIENT_THRESHOLD = 0.15f

        // EMA temporal: τ = 200ms ≈ coef 0.85 para ventanas de ~3 frames/s
        private const val EMA_ALPHA = 0.85f
    }

    data class ClassificationResult(
        val speech:  Float,
        val music:   Float,
        val bass:    Float,
        val silence: Float,
        val isValid: Boolean,
        // Clase dominante con EMA aplicada
        val dominantClass: String = "Voz"
    ) {
        // Constructor legacy para compatibilidad con callers anteriores
        constructor(speech: Float, music: Float, bass: Float, isValid: Boolean)
            : this(speech, music, bass, 0f, isValid)
    }

    private var interpreter: Interpreter? = null
    private var isAvailable = false

    // EMA por clase
    private val emaProbs = FloatArray(NUM_CLASSES) { 0.25f }

    // Estado para detección de onset
    private var prevMelEnergy = FloatArray(N_MELS) { 0f }
    private var prevRms = 0f

    // Tablas precomputadas
    private val hannWindow    = buildHannWindow(FRAME_LENGTH)
    private val melFilterbank = buildMelFilterbank()

    init {
        try {
            interpreter = Interpreter(loadModelFile(context),
                Interpreter.Options().apply { setNumThreads(1) })
            isAvailable = true
            Log.i(TAG, "CRNN Anti-Dolby cargado — INPUT_LENGTH=$INPUT_LENGTH")
        } catch (e: Exception) {
            Log.w(TAG, "CRNN no disponible: ${e.message}. Modo heurístico.")
            isAvailable = false
        }
    }

    fun classify(audioFrame: FloatArray): ClassificationResult {
        if (audioFrame.size < INPUT_LENGTH)
            return ClassificationResult(0f, 0f, 0f, 0f, false)

        // ── 1. Detección de silencio (sin inferencia) ─────────────────────
        val rms = computeRms(audioFrame, INPUT_LENGTH)
        if (rms < SILENCE_RMS_THRESHOLD) {
            return applyEma(floatArrayOf(0f, 0f, 0f, 1f), isValid = true)
        }

        // ── 2. Detección de transiente por onset ──────────────────────────
        val logMelFull = computeLogMelSpectrogram(audioFrame)  // [32][40]
        val lastMelFrame = logMelFull[TIME_FRAMES - 1]
        val onset = computeOnset(lastMelFrame)

        if (onset > ONSET_TRANSIENT_THRESHOLD && rms > SILENCE_RMS_THRESHOLD * 5) {
            // Transitorio detectado — IDX_BASS se usa como "Transitorio" en el
            // contexto del clasificador (el CRNN no tiene clase transitorio).
            // El JNI mapea IDX_BASS → acción de transitorio en el control adaptativo.
            val transientProbs = floatArrayOf(0.05f, 0.10f, 0.75f, 0.10f)
            return applyEma(transientProbs, isValid = true)
        }

        // ── 3. Inferencia CRNN ────────────────────────────────────────────
        if (!isAvailable || interpreter == null)
            return heuristicClassify(logMelFull)

        return try {
            val input = Array(1) {
                Array(TIME_FRAMES) { t ->
                    Array(N_MELS) { m -> floatArrayOf(logMelFull[t][m]) }
                }
            }
            val output = Array(1) { FloatArray(NUM_CLASSES) }
            interpreter!!.run(input, output)
            applyEma(output[0], isValid = true)
        } catch (e: Exception) {
            Log.e(TAG, "Error en inferencia: ${e.message}")
            heuristicClassify(logMelFull)
        }
    }

    fun isSpeechDominant(audioFrame: FloatArray, threshold: Float = 0.55f): Boolean {
        val r = classify(audioFrame)
        return r.isValid && r.speech > threshold
    }

    fun isBassDominant(audioFrame: FloatArray, threshold: Float = 0.55f): Boolean {
        val r = classify(audioFrame)
        return r.isValid && r.bass > threshold
    }

    fun release() {
        interpreter?.close()
        interpreter = null
        isAvailable = false
    }

    // ── EMA temporal ─────────────────────────────────────────────────────────
    private fun applyEma(rawProbs: FloatArray, isValid: Boolean): ClassificationResult {
        for (i in 0 until NUM_CLASSES) {
            emaProbs[i] = EMA_ALPHA * emaProbs[i] + (1f - EMA_ALPHA) * rawProbs[i]
        }
        val dom = emaProbs.indices.maxByOrNull { emaProbs[it] } ?: 0
        return ClassificationResult(
            speech  = emaProbs[IDX_VOICE],
            music   = emaProbs[IDX_MUSIC],
            bass    = emaProbs[IDX_BASS],
            silence = emaProbs[IDX_SILENCE],
            isValid = isValid,
            dominantClass = CLASS_NAMES[dom]
        )
    }

    // ── RMS de los primeros n samples ─────────────────────────────────────────
    private fun computeRms(audio: FloatArray, n: Int): Float {
        var sum = 0.0
        for (i in 0 until n) sum += audio[i] * audio[i].toDouble()
        return sqrt(sum / n).toFloat()
    }

    // ── Onset por delta de energía Mel ────────────────────────────────────────
    private fun computeOnset(melFrame: FloatArray): Float {
        var positiveDelta = 0f
        for (m in 0 until N_MELS) {
            val d = melFrame[m] - prevMelEnergy[m]
            if (d > 0f) positiveDelta += d
        }
        prevMelEnergy = melFrame.copyOf()
        // Normalizar por número de bandas y rango típico de log-energía
        return positiveDelta / (N_MELS * 10f)
    }

    // ── Clasificador heurístico (sin modelo TFLite) ───────────────────────────
    private fun heuristicClassify(logMel: Array<FloatArray>): ClassificationResult {
        // Usar el último frame como representativo
        val frame = logMel[TIME_FRAMES - 1]

        var eBass = 0f; var eMid = 0f; var ePresence = 0f; var eAir = 0f
        for (m in 0 until 8)  eBass     += frame[m]
        for (m in 8 until 20) eMid      += frame[m]
        for (m in 20 until 32) ePresence += frame[m]
        for (m in 32 until N_MELS) eAir  += frame[m]
        eBass /= 8f; eMid /= 12f; ePresence /= 12f; eAir /= 8f

        val harmonicity = eMid - eAir
        val logits = floatArrayOf(
            2.0f * harmonicity + 0.5f * eMid,        // Voice
            1.2f * eBass + 0.8f * ePresence,          // Music
            0.0f,                                      // Transient (ya detectado arriba)
            -1.0f                                      // Noise
        )

        // Softmax
        val maxL = logits.max()
        val exps = FloatArray(NUM_CLASSES) { exp((logits[it] - maxL).toDouble()).toFloat() }
        val sum = exps.sum()
        val probs = FloatArray(NUM_CLASSES) { exps[it] / sum }

        return applyEma(probs, isValid = false)
    }

    // ── Feature extraction ────────────────────────────────────────────────────
    private fun buildHannWindow(size: Int): FloatArray {
        val w = FloatArray(size)
        val denom = (size - 1).toDouble()
        for (k in 0 until size)
            w[k] = (0.5 * (1.0 - cos(2.0 * PI * k / denom))).toFloat()
        return w
    }

    private fun hzToMel(hz: Double) = 2595.0 * log10(1.0 + hz / 700.0)
    private fun melToHz(mel: Double) = 700.0 * (10.0.pow(mel / 2595.0) - 1.0)

    private fun buildMelFilterbank(): Array<FloatArray> {
        val fMin = 0.0; val fMax = SAMPLE_RATE / 2.0
        val melMin = hzToMel(fMin); val melMax = hzToMel(fMax)
        val step = (melMax - melMin) / (N_MELS + 1)
        val melPoints = DoubleArray(N_MELS + 2) { melMin + it * step }
        val binPoints = IntArray(N_MELS + 2) {
            floor((FRAME_LENGTH + 1) * melToHz(melPoints[it]) / SAMPLE_RATE).toInt()
        }
        val filters = Array(N_MELS) { FloatArray(NUM_BINS) }
        for (m in 1..N_MELS) {
            val fL = binPoints[m - 1]; val fC = binPoints[m]; val fR = binPoints[m + 1]
            for (k in fL until fC)
                if (k in 0 until NUM_BINS && fC > fL)
                    filters[m - 1][k] = (k - fL).toFloat() / (fC - fL)
            for (k in fC until fR)
                if (k in 0 until NUM_BINS && fR > fC)
                    filters[m - 1][k] = (fR - k).toFloat() / (fR - fC)
        }
        return filters
    }

    private fun computeLogMelSpectrogram(audio: FloatArray): Array<FloatArray> {
        val result = Array(TIME_FRAMES) { FloatArray(N_MELS) }
        val re = FloatArray(FRAME_LENGTH); val im = FloatArray(FRAME_LENGTH)
        val power = FloatArray(NUM_BINS)
        for (t in 0 until TIME_FRAMES) {
            val start = t * HOP_LENGTH
            for (i in 0 until FRAME_LENGTH) {
                val idx = start + i
                re[i] = (if (idx < audio.size) audio[idx] else 0f) * hannWindow[i]
                im[i] = 0f
            }
            fft512(re, im)
            for (k in 0 until NUM_BINS) power[k] = re[k] * re[k] + im[k] * im[k]
            for (m in 0 until N_MELS) {
                val fb = melFilterbank[m]
                var energy = 0f
                for (k in 0 until NUM_BINS) energy += fb[k] * power[k]
                result[t][m] = ln(max(energy, 1e-10f))
            }
        }
        return result
    }

    private fun fft512(re: FloatArray, im: FloatArray) {
        val n = FRAME_LENGTH
        var j = 0
        for (i in 1 until n) {
            var bit = n shr 1
            while (j and bit != 0) { j = j xor bit; bit = bit shr 1 }
            j = j xor bit
            if (i < j) {
                var t = re[i]; re[i] = re[j]; re[j] = t
                t = im[i]; im[i] = im[j]; im[j] = t
            }
        }
        var len = 2
        while (len <= n) {
            val ang = -2.0 * PI / len
            val wRe = cos(ang).toFloat(); val wIm = sin(ang).toFloat()
            var i = 0
            while (i < n) {
                var cRe = 1f; var cIm = 0f
                val half = len / 2
                for (k in 0 until half) {
                    val uRe = re[i+k]; val uIm = im[i+k]
                    val vRe = re[i+k+half]*cRe - im[i+k+half]*cIm
                    val vIm = re[i+k+half]*cIm + im[i+k+half]*cRe
                    re[i+k] = uRe+vRe; im[i+k] = uIm+vIm
                    re[i+k+half] = uRe-vRe; im[i+k+half] = uIm-vIm
                    val nr = cRe*wRe - cIm*wIm; cIm = cRe*wIm + cIm*wRe; cRe = nr
                }
                i += len
            }
            len = len shl 1
        }
    }

    private fun loadModelFile(context: Context): MappedByteBuffer {
        val fd = context.assets.openFd(MODEL_PATH)
        return FileInputStream(fd.fileDescriptor).channel
            .map(FileChannel.MapMode.READ_ONLY, fd.startOffset, fd.declaredLength)
    }
}
