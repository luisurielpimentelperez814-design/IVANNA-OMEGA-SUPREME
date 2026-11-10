package com.ivanna.omega.ui

/**
 * HeadMeasureMath — matemática PURA de la medición de cabeza (v2.5.389).
 *
 * Cómo se mide el radio de cabeza (el dato que alimenta la Ec. 2 del campo Ψ
 * y el render ORBE): foto FRONTAL con una tarjeta de crédito/débito estándar
 * (ISO/IEC 7810: 85.60 × 53.98 mm) pegada verticalmente en la frente/sien, a
 * la MISMA distancia que la cabeza. La tarjeta es la escala: como la foto es
 * una proyección, el cociente de anchos en píxeles es el cociente real.
 *
 *   anchoCabezaCm = 8.56 cm × (anchoCabezaPx / anchoTarjetaPx)
 *   radioM = anchoCabezaCm / 200   (ancho = diámetro; radio = diámetro / 2)
 *
 * El ancho (de oreja a oreja) es la medida correcta para el radio del modelo
 * esférico: el ILD depende de la distancia entre oídos, no de la
 * circunferencia (la cabeza es un elipsoide: más larga de frente a atrás).
 * La circunferencia con cinta sobreestima el radio. Rango válido de ancho:
 * 10–25 cm (radio 5–15 cm); fuera de ahí la medición es inválida (foto mal
 * tomada o marcadores mal puestos), nunca se inventa un valor.
 */
object HeadMeasureMath {

    /** Ancho de una tarjeta de crédito/débito estándar (cm). */
    const val CARD_WIDTH_CM = 8.56f

    /** Radio KEMAR (m): el default cuando no hay medición. */
    const val KEMAR_RADIUS_M = 0.0875f

    const val MIN_RADIUS_M = 0.05f
    const val MAX_RADIUS_M = 0.15f

    /** Ancho de cabeza (cm) desde los anchos en píxeles. null si inválido. */
    fun headWidthCmFromSpans(headSpanPx: Float, cardSpanPx: Float, cardWidthCm: Float = CARD_WIDTH_CM): Float? {
        if (!headSpanPx.isFinite() || !cardSpanPx.isFinite() || !cardWidthCm.isFinite()) return null
        if (cardSpanPx <= 1f || headSpanPx <= 1f || cardWidthCm <= 0f) return null
        val w = cardWidthCm * headSpanPx / cardSpanPx
        return if (w.isFinite() && w in 10f..25f) w else null
    }

    /** Radio (m) desde el ancho de cabeza (cm). Clamp [0.05, 0.15]. */
    fun headRadiusMFromWidthCm(widthCm: Float): Float {
        if (!widthCm.isFinite()) return KEMAR_RADIUS_M
        return (widthCm / 200f).coerceIn(MIN_RADIUS_M, MAX_RADIUS_M)
    }

    /** Radio (m) desde los spans en píxeles. null si la medición es inválida. */
    fun headRadiusMFromSpans(headSpanPx: Float, cardSpanPx: Float): Float? {
        val w = headWidthCmFromSpans(headSpanPx, cardSpanPx) ?: return null
        return headRadiusMFromWidthCm(w)
    }
}
