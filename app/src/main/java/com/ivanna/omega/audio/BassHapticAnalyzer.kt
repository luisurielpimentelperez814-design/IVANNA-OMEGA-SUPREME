package com.ivanna.omega.audio

import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.exp
import kotlin.math.sin
import kotlin.math.sqrt

/**
 * BassHapticAnalyzer — detecta golpes de graves (bombo, sub, impactos) en el audio ya procesado y devuelve la
 * amplitud con la que debe vibrar el teléfono. Lógica pura (sin Android) para poder probarla en JVM.
 *
 * Cadena: mezcla mono → paso-bajo Butterworth 2º orden (100 Hz) → envolvente (ataque ~2 ms, release ~25 ms)
 * → referencia lenta (~300 ms). Hay golpe cuando la envolvente supera `ONSET_RATIO` veces la referencia y un
 * piso absoluto, con refractario de 80 ms. Un tono grave sostenido NO dispara (la referencia lo alcanza): la
 * háptica sigue el ataque, no el nivel, que es lo que el cuerpo percibe como "golpe".
 *
 * Sin asignaciones en process(): se llama desde el hilo de audio.
 */
class BassHapticAnalyzer(private val sampleRate: Int = 48_000) {

    // Biquad paso-bajo RBJ, fc = 100 Hz, Q = 1/sqrt(2)
    private val b0: Float
    private val b1: Float
    private val b2: Float
    private val a1: Float
    private val a2: Float
    private var z1 = 0f
    private var z2 = 0f

    private val atk = coeff(0.002f)
    private val rel = coeff(0.025f)
    private val refC = coeff(0.300f)
    private val refractoryFrames = (0.080f * sampleRate).toInt()

    private var env = 0f
    private var ref = 0f
    private var cooldown = 0
    private var armed = true

    init {
        val w0 = 2.0 * PI * CUTOFF_HZ / sampleRate
        val alpha = sin(w0) / (2.0 * Q)
        val c = cos(w0)
        val a0 = 1.0 + alpha
        b0 = (((1.0 - c) / 2.0) / a0).toFloat()
        b1 = ((1.0 - c) / a0).toFloat()
        b2 = b0
        a1 = ((-2.0 * c) / a0).toFloat()
        a2 = ((1.0 - alpha) / a0).toFloat()
    }

    private fun coeff(tauSec: Float): Float = (1.0 - exp(-1.0 / (tauSec * sampleRate))).toFloat()

    /** Última envolvente de graves (0..1 aprox.), útil para mostrar un medidor real en la UI. */
    val level: Float get() = env

    fun reset() { z1 = 0f; z2 = 0f; env = 0f; ref = 0f; cooldown = 0; armed = true }

    /**
     * Analiza un bloque PCM float estéreo intercalado. Devuelve 0 si no hay golpe, o la amplitud 1..255 del pulso
     * (ya escalada por `strength` 0..1; los motores no responden por debajo de ~[MIN_AMPLITUDE]).
     */
    fun process(buffer: FloatArray, frames: Int, strength: Float): Int {
        val n = minOf(frames, buffer.size / 2)
        for (i in 0 until n) {
            val x = 0.5f * (buffer[2 * i] + buffer[2 * i + 1])
            val y = b0 * x + z1
            z1 = b1 * x - a1 * y + z2
            z2 = b2 * x - a2 * y
            val a = abs(y)
            env += (a - env) * (if (a > env) atk else rel)
            ref += (env - ref) * refC
        }
        if (cooldown > 0) cooldown -= n
        // Re-armado con histéresis: un golpe (o un sub sostenido) dispara UNA vez y no vuelve a disparar hasta que
        // la envolvente cae cerca de la referencia.
        if (!armed && (env < FLOOR || env < ref * REARM_RATIO)) armed = true
        if (cooldown > 0 || !armed || strength <= 0f) return 0
        if (env < FLOOR || env < ref * ONSET_RATIO) return 0
        armed = false
        cooldown = refractoryFrames
        val norm = ((env - FLOOR) / (FULL_SCALE - FLOOR)).coerceIn(0f, 1f)
        val amp = sqrt(norm) * strength.coerceIn(0f, 1f)
        return (MIN_AMPLITUDE + amp * (255 - MIN_AMPLITUDE)).toInt().coerceIn(MIN_AMPLITUDE, 255)
    }

    companion object {
        const val CUTOFF_HZ = 100.0
        const val Q = 0.7071067811865476
        const val ONSET_RATIO = 1.8f
        const val REARM_RATIO = 1.2f
        const val FLOOR = 0.03f
        const val FULL_SCALE = 0.45f
        const val MIN_AMPLITUDE = 40
    }
}
