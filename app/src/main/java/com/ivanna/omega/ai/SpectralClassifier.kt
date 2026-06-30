/*
 * IVANNA-FUSION — SpectralClassifier
 * Clasificador real de audio vía FFT: Music/Habla/Silencio/Electrónica + BPM real
 * Sin dependencias de modelos externos — DSP puro en Kotlin.
 * © 2025 Luis Uriel Pimentel Pérez. Todos los derechos reservados.
 */
package com.ivanna.omega.ai

import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaRecorder
import android.util.Log
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*
import kotlin.math.*

object SpectralClassifier {
    private const val TAG = "SpectralClassifier"
    private const val SAMPLE_RATE = 44100
    const val BLOCK_SIZE  = 4096      // ~93ms a 44100 Hz
    private const val FFT_SIZE    = 1024

    data class Classification(
        val label: String,           // "Música" | "Habla" | "Silencio" | "Electrónica"
        val labelEn: String,         // "Music"  | "Speech" | "Silence" | "Electronic"
        val confidence: Float,
        val bpm: Float,
        val centroidHz: Float,
        val bassEnergy: Float,       // 0–1
        val midEnergy: Float,        // 0–1
        val highEnergy: Float,       // 0–1
        val rmsDb: Float,            // dBFS aprox.
        val zcr: Float,              // zero-crossing rate
        val spectrum: FloatArray     // 32 bins para visualización
    )

    private val _results = MutableSharedFlow<Classification>(replay = 1, extraBufferCapacity = 8)
    val results: SharedFlow<Classification> = _results.asSharedFlow()

    @Volatile private var running = false
    private var job: Job? = null
    private val scope = CoroutineScope(Dispatchers.IO + SupervisorJob())

    // BPM: historial de energía para detección de beat
    private val energyHistory = ArrayDeque<Float>(256)
    private var lastBpm = 120f
    private var lastBpmUpdateMs = 0L

    fun start() {
        if (running) return
        running = true
        job = scope.launch {
            val minBuf = AudioRecord.getMinBufferSize(
                SAMPLE_RATE,
                AudioFormat.CHANNEL_IN_MONO,
                AudioFormat.ENCODING_PCM_FLOAT
            ).let { if (it < BLOCK_SIZE * 4) BLOCK_SIZE * 4 else it }

            val recorder = try {
                AudioRecord(
                    MediaRecorder.AudioSource.MIC,
                    SAMPLE_RATE,
                    AudioFormat.CHANNEL_IN_MONO,
                    AudioFormat.ENCODING_PCM_FLOAT,
                    minBuf
                ).also { if (it.state != AudioRecord.STATE_INITIALIZED) throw IllegalStateException("AudioRecord init failed") }
            } catch (e: Exception) {
                Log.w(TAG, "No se pudo inicializar AudioRecord: ${e.message}")
                running = false
                return@launch
            }

            recorder.startRecording()
            Log.i(TAG, "SpectralClassifier iniciado @ ${SAMPLE_RATE}Hz")

            val buffer = FloatArray(BLOCK_SIZE)
            try {
                while (running && isActive) {
                    val read = recorder.read(buffer, 0, BLOCK_SIZE, AudioRecord.READ_BLOCKING)
                    if (read > 0) {
                        val result = analyze(buffer.copyOf(read))
                        _results.emit(result)
                    }
                    yield()
                }
            } finally {
                recorder.stop()
                recorder.release()
                Log.i(TAG, "SpectralClassifier detenido")
            }
        }
    }

    fun stop() {
        running = false
        job?.cancel()
        job = null
    }

    fun analyze(samples: FloatArray): Classification {
        val n = minOf(samples.size, BLOCK_SIZE)

        // ── RMS ──────────────────────────────────────────────────────────────
        var sumSq = 0.0
        for (i in 0 until n) sumSq += samples[i] * samples[i]
        val rms = sqrt(sumSq / n).toFloat()
        val rmsDb = if (rms > 1e-9f) 20f * log10(rms) else -96f

        // ── Silencio ─────────────────────────────────────────────────────────
        if (rms < 0.0015f) {
            return Classification("Silencio","Silence",0.97f,lastBpm,0f,0f,0f,0f,rmsDb,0f, FloatArray(32))
        }

        // ── ZCR ──────────────────────────────────────────────────────────────
        var zcCount = 0
        for (i in 1 until n) if ((samples[i-1] < 0f) != (samples[i] < 0f)) zcCount++
        val zcr = zcCount.toFloat() / n

        // ── FFT (ventana Hann + DFT radix-2) ─────────────────────────────────
        val fftN  = FFT_SIZE
        val re    = FloatArray(fftN)
        val im    = FloatArray(fftN)
        val hann  = FloatArray(fftN) { i -> (0.5f * (1f - cos(2.0 * PI * i / (fftN-1)))).toFloat() }
        for (i in 0 until minOf(n, fftN)) re[i] = samples[i] * hann[i]
        fft(re, im)

        val half = fftN / 2
        val mags = FloatArray(half) { i -> sqrt(re[i]*re[i] + im[i]*im[i]) }

        // ── Centroide espectral ───────────────────────────────────────────────
        val bw = SAMPLE_RATE.toFloat() / fftN
        var wSum = 0.0; var mSum = 0.0
        for (i in mags.indices) { wSum += i * bw * mags[i]; mSum += mags[i] }
        val centroid = if (mSum > 0) (wSum / mSum).toFloat() else 1000f

        // ── Energía por bandas ────────────────────────────────────────────────
        fun bandE(lo: Float, hi: Float): Float {
            val a = (lo / bw).toInt().coerceIn(0, half-1)
            val b = (hi / bw).toInt().coerceIn(0, half-1)
            if (a >= b) return 0f
            var s = 0f; for (i in a..b) s += mags[i]
            return s / (b - a + 1)
        }
        val bassE = bandE(20f, 250f)
        val midE  = bandE(250f, 4000f)
        val highE = bandE(4000f, 16000f)
        val maxE  = maxOf(bassE, midE, highE, 1e-6f)

        // ── Espectro de visualización (32 bins log) ───────────────────────────
        val vis = FloatArray(32)
        for (i in 0 until 32) {
            val fLo = 20f * (20000f / 20f).pow(i.toFloat() / 32f)
            val fHi = 20f * (20000f / 20f).pow((i+1).toFloat() / 32f)
            vis[i] = (bandE(fLo, fHi) / maxE).coerceIn(0f, 1f)
        }

        // ── BPM vía energía de baja frecuencia ───────────────────────────────
        energyHistory.addLast(bassE + midE * 0.3f)
        if (energyHistory.size > 200) energyHistory.removeFirst()
        val nowMs = System.currentTimeMillis()
        if (nowMs - lastBpmUpdateMs > 2000L && energyHistory.size >= 32) {
            lastBpm = estimateBpm(energyHistory.toFloatArray())
            lastBpmUpdateMs = nowMs
        }

        // ── Clasificación ─────────────────────────────────────────────────────
        val (label, labelEn, conf) = when {
            // Silencio ya tratado arriba
            zcr > 0.18f && centroid in 200f..4000f ->
                Triple("Habla", "Speech", (0.55f + zcr * 1.2f).coerceAtMost(0.95f))
            bassE > midE * 1.8f && centroid < 1500f && lastBpm > 110f ->
                Triple("Electrónica", "Electronic", 0.82f)
            bassE > midE * 1.4f && centroid < 2500f ->
                Triple("Música", "Music", (0.60f + (bassE/maxE) * 0.3f).coerceAtMost(0.92f))
            midE > bassE && centroid in 800f..5000f ->
                Triple("Música", "Music", (0.55f + (midE/maxE) * 0.35f).coerceAtMost(0.93f))
            else ->
                Triple("Música", "Music", 0.55f)
        }

        return Classification(
            label, labelEn, conf, lastBpm, centroid,
            (bassE/maxE).coerceIn(0f,1f),
            (midE/maxE).coerceIn(0f,1f),
            (highE/maxE).coerceIn(0f,1f),
            rmsDb, zcr, vis
        )
    }

    // ── BPM estimado via autocorrelación de energía ───────────────────────────
    private fun estimateBpm(energies: FloatArray): Float {
        val n = energies.size
        val mean = energies.average().toFloat()
        val centered = FloatArray(n) { energies[it] - mean }
        // Autocorrelación para lags de 30 BPM a 200 BPM en frames de ~93ms
        // 1 frame = BLOCK_SIZE/SAMPLE_RATE ≈ 93ms → 60/(lag*0.093) BPM
        val frameS = BLOCK_SIZE.toFloat() / SAMPLE_RATE
        var bestLag = 8; var bestAC = Float.NEGATIVE_INFINITY
        for (lag in 4..40) {
            var ac = 0f
            for (i in 0 until n - lag) ac += centered[i] * centered[i + lag]
            if (ac > bestAC) { bestAC = ac; bestLag = lag }
        }
        val bpm = (60f / (bestLag * frameS)).coerceIn(60f, 200f)
        // Suavizado temporal
        return lastBpm * 0.6f + bpm * 0.4f
    }

    // ── FFT radix-2 in-place Cooley-Tukey ────────────────────────────────────
    private fun fft(re: FloatArray, im: FloatArray) {
        val n = re.size
        var j = 0
        for (i in 1 until n) {
            var bit = n shr 1
            while (j and bit != 0) { j = j xor bit; bit = bit shr 1 }
            j = j xor bit
            if (i < j) {
                var t = re[i]; re[i] = re[j]; re[j] = t
                t = im[i];     im[i] = im[j]; im[j] = t
            }
        }
        var len = 2
        while (len <= n) {
            val ang = 2.0 * PI / len
            val wRe = cos(ang).toFloat()
            val wIm = sin(ang).toFloat()
            var i = 0
            while (i < n) {
                var uRe = 1f; var uIm = 0f
                for (k in 0 until len / 2) {
                    val tRe = re[i+k+len/2]*uRe - im[i+k+len/2]*uIm
                    val tIm = re[i+k+len/2]*uIm + im[i+k+len/2]*uRe
                    re[i+k+len/2] = re[i+k] - tRe;  im[i+k+len/2] = im[i+k] - tIm
                    re[i+k] += tRe;                   im[i+k] += tIm
                    val nuRe = uRe*wRe - uIm*wIm
                    uIm = uRe*wIm + uIm*wRe;         uRe = nuRe
                }
                i += len
            }
            len = len shl 1
        }
    }
}
