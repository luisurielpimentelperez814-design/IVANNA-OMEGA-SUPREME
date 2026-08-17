package com.ivanna.omega.ai

import android.content.Context
import android.util.Log
import org.tensorflow.lite.Interpreter
import java.io.FileInputStream
import java.nio.MappedByteBuffer
import java.nio.channels.FileChannel
import kotlin.math.cos
import kotlin.math.floor
import kotlin.math.ln
import kotlin.math.log10
import kotlin.math.max
import kotlin.math.PI

/**
 * AntiDolbyCrnnClassifier — Clasificador CRNN Anti-Dolby entrenado in-house.
 *
 * REEMPLAZA a YamnetClassifier. Mantiene una API compatible (classify(),
 * isSpeechDominant(), isBassDominant(), release()) para no romper los
 * callers existentes (AntiDolbyController, VoiceController,
 * VoiceProtectionController, AudioPipeline).
 *
 * Contrato de features — IDÉNTICO al notebook de entrenamiento (Untitled0.ipynb):
 *   - Sample rate     : 16000 Hz mono
 *   - FFT window      : 512 samples, ventaneo Hann
 *   - Hop length      : 160 samples
 *   - 40 filtros Mel triangulares (0-8000 Hz, 2595·log10(1+f/700))
 *   - 32 frames en tiempo
 *   - log(max(energia, 1e-10)) — SIN normalización adicional
 *   - Input model tensor : [1, 32, 40, 1]
 *   - Output model tensor: [1, 4] softmax → [Voz, Musica, Bajos, Silencio]
 *   - INPUT_LENGTH = (32-1)*160 + 512 = 5472 samples (~0.342 s @ 16 kHz)
 *
 * Fallback: si `anti_dolby_crnn.tflite` no está en assets, `classify()`
 * devuelve isValid=false y todos los callers ya manejan ese caso.
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

        // Índices canónicos de salida (mismos que CLASS_NAMES del notebook)
        const val IDX_VOICE    = 0
        const val IDX_MUSIC    = 1
        const val IDX_BASS     = 2
        const val IDX_SILENCE  = 3

        val CLASS_NAMES = arrayOf("Voz", "Musica", "Bajos", "Silencio")

        private const val NUM_BINS = FRAME_LENGTH / 2 + 1  // 257
    }

    // API compatible con YamnetClassifier
    data class ClassificationResult(
        val speech: Float,
        val music: Float,
        val bass: Float,
        val silence: Float,
        val isValid: Boolean
    ) {
        // Constructor legacy (voz/musica/bajos, sin silencio) para que compile
        // cualquier uso antiguo que instancie el data class con 4 args.
        constructor(speech: Float, music: Float, bass: Float, isValid: Boolean)
            : this(speech, music, bass, 0f, isValid)
    }

    private var interpreter: Interpreter? = null
    private var isAvailable = false

    // Ventana Hann y banco de filtros Mel — precomputados
    private val hannWindow: FloatArray
    private val melFilterbank: Array<FloatArray>

    init {
        hannWindow    = buildHannWindow(FRAME_LENGTH)
        melFilterbank = buildMelFilterbank()

        try {
            interpreter = Interpreter(loadModelFile(context))
            isAvailable = true
            Log.i(TAG, "CRNN Anti-Dolby cargado ($MODEL_PATH)")
        } catch (e: Exception) {
            Log.w(TAG, "CRNN no disponible: ${e.message}. Modo fallback activado.")
            isAvailable = false
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // API pública
    // ─────────────────────────────────────────────────────────────────────────

    /**
     * Clasifica un frame de audio.
     * @param audioFrame FloatArray de al menos INPUT_LENGTH samples @ 16 kHz mono.
     *                   Si viene más largo, solo se usan los primeros INPUT_LENGTH.
     *                   (Por compatibilidad con callers de YAMNet que envían 15600).
     */
    fun classify(audioFrame: FloatArray): ClassificationResult {
        if (!isAvailable || interpreter == null) {
            return ClassificationResult(0f, 0f, 0f, 0f, false)
        }
        if (audioFrame.size < INPUT_LENGTH) {
            Log.w(TAG, "Frame demasiado corto: ${audioFrame.size} < $INPUT_LENGTH")
            return ClassificationResult(0f, 0f, 0f, 0f, false)
        }

        return try {
            val logMel = computeLogMelSpectrogram(audioFrame)  // [32][40]
            // Tensor entrada: [1, 32, 40, 1]
            val input = Array(1) {
                Array(TIME_FRAMES) { t ->
                    Array(N_MELS) { m -> floatArrayOf(logMel[t][m]) }
                }
            }
            val output = Array(1) { FloatArray(NUM_CLASSES) }
            interpreter!!.run(input, output)
            val s = output[0]
            ClassificationResult(
                speech  = s[IDX_VOICE],
                music   = s[IDX_MUSIC],
                bass    = s[IDX_BASS],
                silence = s[IDX_SILENCE],
                isValid = true
            )
        } catch (e: Exception) {
            Log.e(TAG, "Error en clasificación: ${e.message}")
            ClassificationResult(0f, 0f, 0f, 0f, false)
        }
    }

    fun isSpeechDominant(audioFrame: FloatArray, threshold: Float = 0.6f): Boolean {
        val r = classify(audioFrame)
        return r.isValid && r.speech > threshold
    }

    fun isBassDominant(audioFrame: FloatArray, threshold: Float = 0.6f): Boolean {
        val r = classify(audioFrame)
        return r.isValid && r.bass > threshold
    }

    fun release() {
        interpreter?.close()
        interpreter = null
        isAvailable = false
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Extracción de features — MISMA lógica que el notebook Python
    // ─────────────────────────────────────────────────────────────────────────

    private fun buildHannWindow(size: Int): FloatArray {
        val w = FloatArray(size)
        // 0.5 * (1 - cos(2π k / (N-1)))
        val denom = (size - 1).toDouble()
        for (k in 0 until size) {
            w[k] = (0.5 * (1.0 - cos(2.0 * PI * k / denom))).toFloat()
        }
        return w
    }

    private fun hzToMel(hz: Double): Double = 2595.0 * log10(1.0 + hz / 700.0)
    private fun melToHz(mel: Double): Double = 700.0 * (Math.pow(10.0, mel / 2595.0) - 1.0)

    private fun buildMelFilterbank(): Array<FloatArray> {
        val fMin = 0.0
        val fMax = SAMPLE_RATE / 2.0
        val melMin = hzToMel(fMin)
        val melMax = hzToMel(fMax)

        // linspace(melMin, melMax, N_MELS + 2)
        val melPoints = DoubleArray(N_MELS + 2)
        val step = (melMax - melMin) / (N_MELS + 1)
        for (i in 0 until N_MELS + 2) melPoints[i] = melMin + i * step

        // hzPoints → binPoints = floor((FRAME_LENGTH + 1) * hz / SR)
        val binPoints = IntArray(N_MELS + 2)
        for (i in 0 until N_MELS + 2) {
            val hz = melToHz(melPoints[i])
            binPoints[i] = floor((FRAME_LENGTH + 1) * hz / SAMPLE_RATE).toInt()
        }

        val filters = Array(N_MELS) { FloatArray(NUM_BINS) }
        for (m in 1..N_MELS) {
            val fLeft   = binPoints[m - 1]
            val fCenter = binPoints[m]
            val fRight  = binPoints[m + 1]

            var k = fLeft
            while (k < fCenter) {
                if (k in 0 until NUM_BINS && fCenter > fLeft) {
                    filters[m - 1][k] = (k - fLeft).toFloat() / (fCenter - fLeft)
                }
                k++
            }
            k = fCenter
            while (k < fRight) {
                if (k in 0 until NUM_BINS && fRight > fCenter) {
                    filters[m - 1][k] = (fRight - k).toFloat() / (fRight - fCenter)
                }
                k++
            }
        }
        return filters
    }

    /**
     * Log-mel-spectrogram idéntico al de compute_log_mel_spectrogram() del
     * notebook. Devuelve [TIME_FRAMES][N_MELS].
     *
     * FFT: usa Cooley-Tukey radix-2 in-place sobre la ventana de 512 samples
     * (potencia de 2), equivalente numéricamente al np.fft.rfft del notebook
     * dentro de tolerancia de punto flotante.
     */
    private fun computeLogMelSpectrogram(audio: FloatArray): Array<FloatArray> {
        val result = Array(TIME_FRAMES) { FloatArray(N_MELS) }
        val re = FloatArray(FRAME_LENGTH)
        val im = FloatArray(FRAME_LENGTH)
        val power = FloatArray(NUM_BINS)

        for (t in 0 until TIME_FRAMES) {
            val start = t * HOP_LENGTH
            // Frame ventaneado
            for (i in 0 until FRAME_LENGTH) {
                val idx = start + i
                val s = if (idx < audio.size) audio[idx] else 0f
                re[i] = s * hannWindow[i]
                im[i] = 0f
            }

            // FFT radix-2 in-place (real→complejo)
            fft512(re, im)

            // Power spectrum de bins 0..N/2
            for (k in 0 until NUM_BINS) {
                power[k] = re[k] * re[k] + im[k] * im[k]
            }

            // Mel filterbank @ power spectrum
            for (m in 0 until N_MELS) {
                val fb = melFilterbank[m]
                var energy = 0f
                for (k in 0 until NUM_BINS) energy += fb[k] * power[k]
                result[t][m] = ln(max(energy, 1e-10f))
            }
        }
        return result
    }

    /**
     * FFT Cooley-Tukey radix-2 in-place, N=512. Reutiliza los arrays re/im.
     * Equivalente a np.fft.fft() del notebook para los primeros NUM_BINS bins.
     */
    private fun fft512(re: FloatArray, im: FloatArray) {
        val n = FRAME_LENGTH
        // Bit-reversal
        var j = 0
        for (i in 1 until n) {
            var bit = n shr 1
            while (j and bit != 0) {
                j = j xor bit
                bit = bit shr 1
            }
            j = j xor bit
            if (i < j) {
                var tmp = re[i]; re[i] = re[j]; re[j] = tmp
                tmp = im[i];    im[i] = im[j]; im[j] = tmp
            }
        }
        // Cooley-Tukey
        var len = 2
        while (len <= n) {
            val ang = -2.0 * PI / len
            val wRe = cos(ang).toFloat()
            val wIm = kotlin.math.sin(ang).toFloat()
            var i = 0
            while (i < n) {
                var curRe = 1f
                var curIm = 0f
                val half = len / 2
                for (k in 0 until half) {
                    val uRe = re[i + k]
                    val uIm = im[i + k]
                    val vRe = re[i + k + half] * curRe - im[i + k + half] * curIm
                    val vIm = re[i + k + half] * curIm + im[i + k + half] * curRe
                    re[i + k]        = uRe + vRe
                    im[i + k]        = uIm + vIm
                    re[i + k + half] = uRe - vRe
                    im[i + k + half] = uIm - vIm
                    val nextRe = curRe * wRe - curIm * wIm
                    curIm = curRe * wIm + curIm * wRe
                    curRe = nextRe
                }
                i += len
            }
            len = len shl 1
        }
    }

    private fun loadModelFile(context: Context): MappedByteBuffer {
        val fd = context.assets.openFd(MODEL_PATH)
        val fis = FileInputStream(fd.fileDescriptor)
        val ch = fis.channel
        return ch.map(FileChannel.MapMode.READ_ONLY, fd.startOffset, fd.declaredLength)
    }
}
