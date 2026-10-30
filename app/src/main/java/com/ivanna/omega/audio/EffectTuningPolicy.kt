package com.ivanna.omega.audio

import kotlin.math.ln
import kotlin.math.roundToInt

/** Control-thread policy. Android effects may expose five bands, ten bands or another layout. */
object EffectTuningPolicy {
    private val frequenciesHz = doubleArrayOf(31.0, 63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0)
    private val speechMb = intArrayOf(-200, -150, 0, 50, 150, 250, 250, 150, 50, 0)

    data class Live(val eqGainDb: Float, val width: Float, val thresholdDb: Float, val ratio: Float)
    data class State(
        val isoMb: List<Int> = emptyList(),
        val live: Live? = null,
        val safSigma: Float? = null,
        val trebleMb: Int = 0,
        val bassStrength: Int? = null,
        val loudnessReliefMb: Int = 0
    )

    fun iso(gainsDb: FloatArray): List<Int> = List(10) { i ->
        (gainsDb.getOrElse(i) { 0f }.finite(0f).coerceIn(-12f, 12f) * 100f).roundToInt()
    }

    fun live(eqDb: Float, width: Float, thresholdDb: Float, ratio: Float): Live = Live(
        eqDb.finite(0f).coerceIn(-18f, 18f), width.finite(1f).coerceIn(0f, 2f),
        thresholdDb.finite(-18f).coerceIn(-40f, 0f), ratio.finite(2f).coerceIn(1f, 6f)
    )

    /** Sum each independent contribution once. Headroom is applied after device interpolation. */
    fun curve(baseMb: IntArray, state: State, video: Boolean): IntArray {
        val offset = ((state.live?.eqGainDb ?: 0f) * 100f).roundToInt()
        val nudge = (((state.safSigma ?: 0.5f) - 0.5f) * 240f).roundToInt()
        return IntArray(10) { i ->
            (baseMb.getOrElse(i) { 0 }.toLong() + state.isoMb.getOrElse(i) { 0 } + offset + nudge +
                (if (video) speechMb[i] else 0) + (if (i >= 8) state.trebleMb else 0))
                .coerceIn(-1500L, 1500L).toInt()
        }
    }

    /**
     * Curva para el EQ de 10 bandas del daemon/omega_effect (31 Hz..16 kHz, igual que [frequenciesHz]).
     * Misma politica de headroom que [deviceBands]: no hay banda objetivo positiva (el boost mayor se
     * resta de todas), asi el perfil no puede llevar al limitador. Salida en dB, acotada a +-12 dB.
     */
    fun omegaBandsDb(curveMb: IntArray): FloatArray {
        val headroom = (curveMb.maxOrNull() ?: 0).coerceAtLeast(0)
        return FloatArray(10) { i ->
            ((curveMb.getOrElse(i) { 0 } - headroom) / 100f).coerceIn(-12f, 0f)
        }
    }

    /** Millihertz is the unit returned by Equalizer.getCenterFreq(). No positive target band. */
    fun deviceBands(curveMb: IntArray, centersMilliHz: IntArray, minMb: Int, maxMb: Int): IntArray {
        val low = minOf(minMb, maxMb, 0)
        val high = maxOf(minMb, maxMb).coerceAtMost(0)
        val headroom = (curveMb.maxOrNull() ?: 0).coerceAtLeast(0)
        return IntArray(centersMilliHz.size) { band ->
            val hz = centersMilliHz[band] / 1000.0
            if (hz <= 0) return@IntArray 0.coerceIn(low, high)
            val gain = when {
                hz <= frequenciesHz.first() -> curveMb.getOrElse(0) { 0 }.toDouble()
                hz >= frequenciesHz.last() -> curveMb.getOrElse(9) { 0 }.toDouble()
                else -> {
                    val upper = frequenciesHz.indexOfFirst { it >= hz }
                    val lower = upper - 1
                    val t = ln(hz / frequenciesHz[lower]) / ln(frequenciesHz[upper] / frequenciesHz[lower])
                    curveMb.getOrElse(lower) { 0 } * (1.0 - t) + curveMb.getOrElse(upper) { 0 } * t
                }
            }
            (gain.roundToInt() - headroom).coerceIn(low, high)
        }
    }

    fun virtualizer(base: Int, state: State, allowed: Boolean, cap: Int = 220): Short {
        if (!allowed) return 0
        // Native width slider is [0,2]. Its full range must remain usable below the stock cap.
        val strength = state.live?.let { it.width / 2f * cap } ?: base.coerceIn(0, cap).toFloat()
        val nudge = ((state.safSigma ?: 0.5f) - 0.5f) * 160f
        return (strength + nudge).roundToInt().coerceIn(0, cap).toShort()
    }

    fun bass(base: Int, state: State, cap: Int = 300): Short =
        (state.bassStrength ?: base).coerceIn(0, cap).toShort()

    fun loudness(baseMb: Int, state: State, limiterAvailable: Boolean, capMb: Int = 150): Int =
        (baseMb.coerceIn(0, if (limiterAvailable) capMb else 0) - state.loudnessReliefMb.coerceIn(0, 1000))
            .coerceAtLeast(0)

    private fun Float.finite(fallback: Float) = if (isFinite()) this else fallback
}
