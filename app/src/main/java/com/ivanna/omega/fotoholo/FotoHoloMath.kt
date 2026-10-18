package com.ivanna.omega.fotoholo

import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.max
import kotlin.math.sin

/**
 * Rasgos extraídos de las 64 bandas Bark REALES (las que alimenta la captura de audio).
 * [valid] = false cuando no hay señal utilizable (silencio, bandas no finitas, sin captura).
 */
data class FotoHoloFeatures(
    val energy: Float,      // 0..1 media de las bandas
    val lowShare: Float,    // 0..1 fracción de energía en graves (bandas 0..15, ≲ 630 Hz)
    val highShare: Float,   // 0..1 fracción de energía en agudos (bandas 40..63, ≳ 2.9 kHz)
    val centroid: Float,    // 0..1 centroide espectral sobre el eje Bark
    val flux: Float,        // 0..1 aumento de energía respecto al cuadro anterior (transitorios)
    val valid: Boolean,
) {
    companion object { val NONE = FotoHoloFeatures(0f, 0f, 0f, 0f, 0f, false) }
}

/**
 * Lógica pura (sin Android) de la capa Fotoholografía·Nanotomix.
 *
 * Qué hace de verdad: convierte el espectro Bark medido en (1) una modulación ACOTADA del ancho
 * estéreo —el único parámetro de audio que gobierna— y (2) la geometría de la escena paramétrica.
 * No es holografía óptica: es una visualización 3D proyectada y un control adaptativo de ancho.
 */
object FotoHoloMath {
    const val BANDS = 64
    private const val LOW_END = 16          // bandas [0, 16)
    private const val HIGH_START = 40       // bandas [40, 64)

    /** Desviación máxima respecto al ancho que fijó el usuario (a intensidad 1). */
    const val MAX_OFFSET = 0.25f
    /** Techo del ancho aplicado, salvo que el usuario ya haya fijado uno mayor. */
    const val WIDTH_CEIL = 1.2f
    /** Variación máxima del ancho por cuadro de control (20 Hz → 0.2 por segundo): sin saltos audibles. */
    const val MAX_STEP = 0.01f

    private const val SILENCE_SUM = 1e-3f

    fun features(bands: FloatArray, prev: FloatArray?): FotoHoloFeatures {
        val n = minOf(bands.size, BANDS)
        if (n < BANDS) return FotoHoloFeatures.NONE
        var sum = 0f; var low = 0f; var high = 0f; var weighted = 0f; var rise = 0f
        for (i in 0 until BANDS) {
            val raw = bands[i]
            val v = if (raw.isFinite()) raw.coerceIn(0f, 1f) else 0f
            sum += v
            if (i < LOW_END) low += v
            if (i >= HIGH_START) high += v
            weighted += v * i
            val p = prev?.getOrNull(i)?.takeIf { it.isFinite() }?.coerceIn(0f, 1f)
            if (p != null && v > p) rise += v - p
        }
        if (sum < SILENCE_SUM) return FotoHoloFeatures.NONE
        return FotoHoloFeatures(
            energy = (sum / BANDS).coerceIn(0f, 1f),
            lowShare = (low / sum).coerceIn(0f, 1f),
            highShare = (high / sum).coerceIn(0f, 1f),
            centroid = (weighted / (sum * (BANDS - 1))).coerceIn(0f, 1f),
            flux = (rise / sum).coerceIn(0f, 1f),
            valid = true,
        )
    }

    /**
     * Ancho objetivo: parte del ancho del usuario ([base]) y lo desplaza hasta ±[MAX_OFFSET]·[intensity].
     * Agudos dominantes abren la imagen; graves dominantes la cierran (los graves anchos suman fase y
     * desperdician excursión). Sin señal válida → devuelve [base] sin tocar nada.
     */
    fun targetWidth(base: Float, intensity: Float, f: FotoHoloFeatures): Float {
        if (!base.isFinite()) return 1.0f
        val b = base.coerceIn(0f, 1.5f)
        if (!f.valid) return b
        val k = if (intensity.isFinite()) intensity.coerceIn(0f, 1f) else 0f
        val tilt = (f.highShare - f.lowShare).coerceIn(-1f, 1f)
        val ceil = max(WIDTH_CEIL, b)
        return (b + MAX_OFFSET * k * tilt).coerceIn(0f, ceil)
    }

    /** Acerca [current] a [target] sin superar [maxStep]; entradas no finitas no mueven nada. */
    fun slew(current: Float, target: Float, maxStep: Float = MAX_STEP): Float {
        if (!current.isFinite()) return if (target.isFinite()) target else 1.0f
        if (!target.isFinite()) return current
        val step = if (maxStep.isFinite()) maxStep.coerceAtLeast(0f) else 0f
        return current + (target - current).coerceIn(-step, step)
    }

    /** Diferencia absoluta usada para decidir si vale la pena escribir al motor. */
    fun changed(a: Float, b: Float, eps: Float = 0.002f) = !a.isFinite() || !b.isFinite() || abs(a - b) >= eps

    /**
     * Punto 3D proyectado de la banda [index] con valor [value]: anillo de 64 nodos, radio según energía,
     * ondulación en profundidad y perspectiva. Devuelve [x, y, escalaDeProfundidad] con x,y en ≈[-1, 1].
     */
    fun holoPoint(index: Int, value: Float, rotation: Float): FloatArray {
        val v = if (value.isFinite()) value.coerceIn(0f, 1f) else 0f
        val rot = if (rotation.isFinite()) rotation else 0f
        val a = (2.0 * PI * index / BANDS).toFloat() + rot
        val r = 0.35f + 0.55f * v
        val z = sin(3f * a + rot) * 0.3f
        val scale = 1f / (1.6f - z * 0.5f)
        return floatArrayOf(r * cos(a) * scale, r * sin(a) * 0.55f * scale, scale)
    }
}
