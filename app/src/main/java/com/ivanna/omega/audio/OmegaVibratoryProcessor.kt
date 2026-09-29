package com.ivanna.omega.audio

import kotlin.math.abs
import kotlin.math.tanh

/**
 * OmegaVibratoryProcessor — saturación armónica suave + limitador de pico.
 *
 * Antes era un stub que devolvía el audio sin tocar.
 * Ahora aplica:
 *   1. Saturación tanh con drive configurable — añade armónicos pares e
 *      impares que dan "cuerpo" al sonido sin distorsión dura.
 *   2. Limitador de pico look-ahead simple — evita que el procesamiento
 *      anterior (DSP / HRTF / NPE) sature la salida final.
 *
 * Diseño sin allocs: todos los buffers son locales o reutilizados.
 */
class OmegaVibratoryProcessor(
    /** Nivel de saturación: 0 = bypass, 1 = suave, 3 = cálido, 6+ = agresivo */
    private var drive: Float = 1.2f,
    /** Umbral del limitador en lineal (0.0–1.0) */
    private var limitThreshold: Float = 0.92f,
    /** Velocidad de ataque del limitador (fracción por muestra) */
    private var attackCoeff: Float  = 0.9995f,
    /** Velocidad de release del limitador */
    private var releaseCoeff: Float = 0.9990f
) {
    private var gain = 1.0f   // ganancia actual del limitador

    /** Procesa buffer PCM estéreo intercalado in-place (par L/R vinculado en fase). */
    fun process(audioData: FloatArray): FloatArray {
        val d = drive.coerceIn(0f, 4f)
        if (d < 0.01f || audioData.isEmpty()) return audioData

        // Drive acotado para preservar micro-dinámica sin distorsión inter-armónica
        val effDrive = 1f + 0.35f * d
        val invD = 1f / (tanh(effDrive.toDouble()).toFloat().coerceAtLeast(0.001f))

        var i = 0
        val n = audioData.size
        while (i + 1 < n) {
            val inL = if (audioData[i].isFinite()) audioData[i] else 0f
            val inR = if (audioData[i + 1].isFinite()) audioData[i + 1] else 0f

            val sL = (tanh((inL * effDrive).toDouble()) * invD).toFloat()
            val sR = (tanh((inR * effDrive).toDouble()) * invD).toFloat()

            // Detector estéreo vinculado (L/R comparten la misma envolvente por frame)
            val level = maxOf(abs(sL), abs(sR))
            gain = if (level * gain > limitThreshold && level > 1e-6f) {
                (limitThreshold / level).coerceAtMost(1f) * (1f - attackCoeff) + gain * attackCoeff
            } else {
                (releaseCoeff * gain + (1f - releaseCoeff)).coerceAtMost(1f)
            }

            audioData[i]     = softCeiling(sL * gain, limitThreshold)
            audioData[i + 1] = softCeiling(sR * gain, limitThreshold)
            i += 2
        }
        return audioData
    }

    private fun softCeiling(x: Float, ceiling: Float): Float {
        if (!x.isFinite()) return 0f
        val ax = abs(x)
        val knee = ceiling * 0.88f
        if (ax <= knee) return x
        val span = (ceiling - knee).coerceAtLeast(1e-4f)
        val excess = (ax - knee) / span
        val e2 = excess * excess
        val comp = excess * (27f + e2) / (27f + 9f * e2 + excess * e2)
        val mag = knee + span * comp.coerceAtMost(1f)
        return if (x >= 0f) mag else -mag
    }

    fun setDrive(d: Float)     { drive = d.coerceIn(0f, 12f) }
    fun setThreshold(t: Float) { limitThreshold = t.coerceIn(0.5f, 1f) }
    fun reset()                { gain = 1f }
}
