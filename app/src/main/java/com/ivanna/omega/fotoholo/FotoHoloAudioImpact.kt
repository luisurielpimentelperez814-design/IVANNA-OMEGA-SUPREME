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

    /** Sala del catálogo RIR tal como la describe el motor nativo. Unidades: s, m³, m. */
    data class CatalogRoom(val idx: Int, val rt60S: Float, val volumeM3: Float, val widthM: Float, val heightM: Float, val depthM: Float)

    /**
     * Interpreta "nombre|rt60|volumen|distancia|WxHxD" (formato de `rirRoomInfo`). Devuelve null si algo no es
     * finito o positivo: una sala dudosa no entra al catálogo (sin valores por defecto inventados).
     */
    fun parseRoomInfo(idx: Int, line: String?): CatalogRoom? {
        if (line == null) return null
        val parts = line.split('|')
        if (parts.size < 5) return null
        val rt60 = parts[1].toFloatOrNull() ?: return null
        val vol = parts[2].toFloatOrNull() ?: return null
        val dims = parts[4].split('x').map { it.toFloatOrNull() ?: return null }
        if (dims.size != 3) return null
        val all = listOf(rt60, vol, dims[0], dims[1], dims[2])
        if (all.any { !it.isFinite() || it <= 0f }) return null
        return CatalogRoom(idx, rt60, vol, dims[0], dims[1], dims[2])
    }

    /** JSON compacto [[idx,rt60,vol,w,h,d],...] para el visor. Puro. */
    fun catalogJson(rooms: List<CatalogRoom>): String = rooms.joinToString(prefix = "[", postfix = "]", separator = ",") {
        java.lang.String.format(java.util.Locale.US, "[%d,%.3f,%.1f,%.2f,%.2f,%.2f]", it.idx, it.rt60S, it.volumeM3, it.widthM, it.heightM, it.depthM)
    }
}
