package com.ivanna.omega.audio.objects

/**
 * Representa un objeto de audio inmersivo 3D con coordenadas cartesianas,
 * ganancia, prioridad y metadatos dinámicos.
 */
data class AudioObject(
    val id: Int,
    var positionX: Float = 0.0f,  // Azimuth (-1.0 a 1.0)
    var positionY: Float = 0.0f,  // Elevation (-1.0 a 1.0)
    var positionZ: Float = 1.0f,  // Distance / Depth (0.1 a 10.0)
    var gain: Float = 1.0f,
    var priority: Int = 1,
    var objectWidth: Float = 0.0f,
    var objectHeight: Float = 0.0f,
    val metadata: MutableMap<String, Any> = mutableMapOf()
) {
    fun updatePosition(x: Float, y: Float, z: Float) {
        positionX = x.coerceIn(-10.0f, 10.0f)
        positionY = y.coerceIn(-10.0f, 10.0f)
        positionZ = z.coerceIn(0.01f, 20.0f)
    }

    fun toAzimuthElevation(): Pair<Float, Float> {
        val azimuth = Math.toDegrees(Math.atan2(positionX.toDouble(), positionZ.toDouble())).toFloat()
        val elevation = Math.toDegrees(Math.atan2(positionY.toDouble(), Math.hypot(positionX.toDouble(), positionZ.toDouble()))).toFloat()
        return Pair(azimuth, elevation)
    }
}
