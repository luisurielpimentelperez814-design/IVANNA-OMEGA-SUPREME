package com.ivanna.omega.audio

import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.sin

/**
 * Remuestreador estéreo intercalado L/R de sinc enventanada (Blackman-Harris),
 * 32 taps, 256 fases con interpolación lineal entre fases.
 *
 * FIX (distorsión constante a cualquier volumen): la versión anterior repetía
 * la muestra más cercana (vecino más próximo). Con 44.1/48 → 96 kHz eso crea
 * imágenes espectrales y jitter de fase (patrón de repetición irregular en
 * 44.1→96), y además descartaba la fracción de muestra en cada trozo
 * ((frames*ratio).toInt()) reiniciando la fase = discontinuidad por trozo.
 * Ahora el filtro es de banda limitada y conserva posición fraccionaria e
 * historial entre llamadas: la salida es continua entre trozos.
 *
 * Latencia: HALF muestras de entrada (16). No es thread-safe: un hilo por instancia.
 */
class StereoAudioResampler(
    private val targetRate: Int = 96000
) {
    private companion object {
        const val TAPS = 32
        const val HALF = TAPS / 2
        const val PHASES = 256
    }

    private var inputRate = 96000
    private var step = 1.0           // muestras de entrada por muestra de salida
    private var table = buildTable(1.0)

    // Historial (TAPS frames) por canal y posición fraccionaria en coordenadas
    // del bloque combinado [historial | entrada nueva].
    private var histL = FloatArray(TAPS)
    private var histR = FloatArray(TAPS)
    private var pos = TAPS.toDouble()

    fun setInputSampleRate(rate: Int) {
        if (rate <= 0) return
        if (rate != inputRate) {
            inputRate = rate
            step = rate.toDouble() / targetRate.toDouble()
            table = buildTable(if (step > 1.0) 1.0 / step else 1.0)
            histL.fill(0f); histR.fill(0f)
            pos = TAPS.toDouble()
        }
    }

    private fun buildTable(cutoffScale: Double): FloatArray {
        val fc = 0.97 * cutoffScale
        val t = FloatArray((PHASES + 1) * TAPS)
        for (p in 0..PHASES) {
            val frac = p.toDouble() / PHASES
            var sum = 0.0
            val row = DoubleArray(TAPS)
            for (k in 0 until TAPS) {
                val x = (k - HALF + 1) - frac
                val s = if (abs(x) < 1e-9) fc else sin(PI * fc * x) / (PI * x)
                val u = x / HALF
                val w = if (abs(u) >= 1.0) 0.0 else
                    0.35875 + 0.48829 * cos(PI * u) + 0.14128 * cos(2 * PI * u) + 0.01168 * cos(3 * PI * u)
                row[k] = s * w
                sum += row[k]
            }
            // Ganancia DC unitaria en cada fase (sin modulación de nivel por fase).
            for (k in 0 until TAPS) t[p * TAPS + k] = (row[k] / sum).toFloat()
        }
        return t
    }

    fun process(input: FloatArray): FloatArray {
        if (inputRate == targetRate) return input
        val n = input.size / 2
        if (n == 0) return FloatArray(0)

        val total = TAPS + n
        val l = FloatArray(total)
        val r = FloatArray(total)
        System.arraycopy(histL, 0, l, 0, TAPS)
        System.arraycopy(histR, 0, r, 0, TAPS)
        for (i in 0 until n) {
            l[TAPS + i] = input[2 * i]
            r[TAPS + i] = input[2 * i + 1]
        }

        val maxOut = (((total - HALF - pos) / step).toInt() + 2).coerceAtLeast(0)
        val out = FloatArray(maxOut * 2)
        var o = 0
        val coef = FloatArray(TAPS)
        var p = pos
        while (true) {
            val ip = p.toInt()
            if (ip + HALF > total - 1) break
            val phaseF = (p - ip) * PHASES
            val ph = phaseF.toInt()
            val a = (phaseF - ph).toFloat()
            val b0 = ph * TAPS
            val b1 = (ph + 1) * TAPS
            for (k in 0 until TAPS) coef[k] = table[b0 + k] * (1f - a) + table[b1 + k] * a
            val start = ip - HALF + 1
            var accL = 0f
            var accR = 0f
            for (k in 0 until TAPS) {
                val c = coef[k]
                accL += c * l[start + k]
                accR += c * r[start + k]
            }
            if (o + 1 >= out.size) break
            out[o++] = accL
            out[o++] = accR
            p += step
        }

        System.arraycopy(l, n, histL, 0, TAPS)
        System.arraycopy(r, n, histR, 0, TAPS)
        pos = p - n
        return if (o == out.size) out else out.copyOf(o)
    }
}
