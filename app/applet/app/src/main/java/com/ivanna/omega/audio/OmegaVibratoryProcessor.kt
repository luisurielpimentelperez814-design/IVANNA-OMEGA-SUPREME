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

    /** Procesa buffer PCM estéreo intercalado in-place. */
    fun process(audioData: FloatArray): FloatArray {
        val d = drive.coerceIn(0f, 12f)
        if (d < 0.01f) return audioData   // bypass si drive ≈ 0

        val invD = 1f / (tanh(d).toFloat().coerceAtLeast(0.001f))

        for (i in audioData.indices) {
            // 1. Saturación armónica suave
            var s = (tanh((audioData[i] * d).toDouble()) * invD).toFloat()

            // 2. Limitador de pico — detecta y aplica ganancia de reducción
            val level = abs(s)
            gain = if (level * gain > limitThreshold) {
                (limitThreshold / level).coerceAtMost(1f) * (1f - attackCoeff) + gain * attackCoeff
            } else {
                (releaseCoeff * gain + (1f - releaseCoeff)).coerceAtMost(1f)
            }
            audioData[i] = (s * gain).coerceIn(-1f, 1f)
        }
        return audioData
    }

    fun setDrive(d: Float)     { drive = d.coerceIn(0f, 12f) }
    fun setThreshold(t: Float) { limitThreshold = t.coerceIn(0.5f, 1f) }
    fun reset()                { gain = 1f }
}
