package com.ivanna.omega.fotoholo

/**
 * Mapeo PURO del panel FotoHolo a parámetros que SÍ cambian el audio (quinto eje: impacto sonoro).
 *
 *  - Profundidad de escena (20..110) → distancia de reflexiones tempranas (0,5..2,0 m), que mueve
 *    el retardo y la ganancia de las 4 primeras reflexiones del motor espacial.
 *  - Elevación del sonido (±45°) → elevación del HRTF (pitch del seguidor de cabeza).
 *
 * Valores no finitos devuelven null: nunca se manda un número inventado al motor.
 */
object FotoHoloAudioImpact {
    const val DEPTH_MIN = 20f
    const val DEPTH_MAX = 110f
    const val DISTANCE_MIN_M = 0.5f
    const val DISTANCE_MAX_M = 2.0f
    const val ELEVATION_MAX_DEG = 45f

    fun distanceMetersFromDepth(depth: Float): Float? {
        if (!depth.isFinite()) return null
        val t = (depth.coerceIn(DEPTH_MIN, DEPTH_MAX) - DEPTH_MIN) / (DEPTH_MAX - DEPTH_MIN)
        return DISTANCE_MIN_M + t * (DISTANCE_MAX_M - DISTANCE_MIN_M)
    }

    fun elevationRadFromDeg(deg: Float): Float? {
        if (!deg.isFinite()) return null
        return Math.toRadians(deg.coerceIn(-ELEVATION_MAX_DEG, ELEVATION_MAX_DEG).toDouble()).toFloat()
    }
}
