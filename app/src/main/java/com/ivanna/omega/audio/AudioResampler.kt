package com.ivanna.omega.audio

/**
 * AudioResampler — Resampler lineal mono, stateless, thread-safe.
 *
 * Uso previsto (PlaybackCaptureService, ruta Anti-Dolby/YAMNet):
 *   val antiDolbyResampler = AudioResampler(48000, 16000)
 *   val monoResampled = antiDolbyResampler.resample(monoIn)  // FloatArray -> FloatArray
 *
 * Diseño (regla de oro: no borramos, mejoramos):
 * - Constructor con inRate/outRate fijos (calzados con el sitio de llamada
 *   real en PlaybackCaptureService.onCreate() -> AudioResampler(48000, 16000)).
 * - `resample(FloatArray): FloatArray` es el hot path que el compilador
 *   estaba pidiendo (línea 434:55, "Unresolved reference: resample").
 * - Interpolación lineal: suficiente para el clasificador YAMNet (16kHz
 *   mono) que sólo necesita banda base <8kHz; no introduce aliasing audible
 *   porque el consumidor es una CNN de clasificación, no reproducción.
 * - Sin estado interno: seguro para llamar desde el hilo de audio sin locks.
 * - Guards: rates <=0, input vacío, y fast-path cuando inRate == outRate
 *   (devuelve copia para preservar la invariante "el caller puede mutar").
 * - Sobrecarga stateless equivalente sobre inRate/outRate variables por si
 *   otro punto del proyecto necesita convertir con tasas dinámicas sin
 *   instanciar la clase.
 */
class AudioResampler(
    private val inputSampleRate: Int,
    private val outputSampleRate: Int,
) {
    init {
        require(inputSampleRate > 0) { "inputSampleRate must be > 0 (got $inputSampleRate)" }
        require(outputSampleRate > 0) { "outputSampleRate must be > 0 (got $outputSampleRate)" }
    }

    /** Resample mono. Devuelve un nuevo FloatArray; el input no se muta. */
    fun resample(input: FloatArray): FloatArray {
        if (input.isEmpty()) return FloatArray(0)
        if (inputSampleRate == outputSampleRate) return input.copyOf()
        return linearResampleMono(input, inputSampleRate, outputSampleRate)
    }

    /** Variante con canales interleaved (L,R,L,R,...). channels>=1. */
    fun resample(input: FloatArray, channels: Int): FloatArray {
        require(channels >= 1) { "channels must be >= 1" }
        if (channels == 1) return resample(input)
        if (input.isEmpty()) return FloatArray(0)
        if (inputSampleRate == outputSampleRate) return input.copyOf()
        return linearResampleInterleaved(input, channels, inputSampleRate, outputSampleRate)
    }

    companion object {
        /** Stateless: útil sin instanciar la clase. */
        fun resample(input: FloatArray, inRate: Int, outRate: Int): FloatArray {
            if (inRate <= 0 || outRate <= 0 || input.isEmpty()) return FloatArray(0)
            if (inRate == outRate) return input.copyOf()
            return linearResampleMono(input, inRate, outRate)
        }

        private fun linearResampleMono(input: FloatArray, inRate: Int, outRate: Int): FloatArray {
            val inLen = input.size
            val outLen = ((inLen.toLong() * outRate) / inRate).toInt().coerceAtLeast(1)
            val out = FloatArray(outLen)
            val ratio = inRate.toDouble() / outRate.toDouble()
            for (i in 0 until outLen) {
                val srcPos = i * ratio
                val idx = srcPos.toInt()
                val frac = (srcPos - idx).toFloat()
                val s0 = input[idx.coerceAtMost(inLen - 1)]
                val s1 = input[(idx + 1).coerceAtMost(inLen - 1)]
                out[i] = s0 + (s1 - s0) * frac
            }
            return out
        }

        private fun linearResampleInterleaved(
            input: FloatArray, channels: Int, inRate: Int, outRate: Int,
        ): FloatArray {
            val inFrames = input.size / channels
            if (inFrames <= 0) return FloatArray(0)
            val outFrames = ((inFrames.toLong() * outRate) / inRate).toInt().coerceAtLeast(1)
            val out = FloatArray(outFrames * channels)
            val ratio = inFrames.toDouble() / outFrames.toDouble()
            for (i in 0 until outFrames) {
                val srcPos = i * ratio
                val idx = srcPos.toInt()
                val frac = (srcPos - idx).toFloat()
                val i0 = idx.coerceAtMost(inFrames - 1) * channels
                val i1 = (idx + 1).coerceAtMost(inFrames - 1) * channels
                val oBase = i * channels
                for (c in 0 until channels) {
                    val s0 = input[i0 + c]
                    val s1 = input[i1 + c]
                    out[oBase + c] = s0 + (s1 - s0) * frac
                }
            }
            return out
        }
    }
}

// -----------------------------------------------------------------------------
// Extensiones de compatibilidad (mantienen los patches v3.2.1/v3.2.2 útiles
// para otros call sites que sí usan receiver-style — regla de oro: no borrar).
// -----------------------------------------------------------------------------

fun FloatArray.resample(inRate: Int, outRate: Int, channels: Int = 1): FloatArray =
    AudioResampler(inRate, outRate).resample(this, channels)

fun ShortArray.resample(inRate: Int, outRate: Int, channels: Int = 1): ShortArray {
    if (isEmpty() || inRate <= 0 || outRate <= 0) return ShortArray(0)
    val asFloat = FloatArray(size) { this[it] / 32768f }
    val rs = AudioResampler(inRate, outRate).resample(asFloat, channels)
    return ShortArray(rs.size) { (rs[it].coerceIn(-1f, 1f) * 32767f).toInt().toShort() }
}
