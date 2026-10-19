package com.ivanna.omega.spatial

import kotlin.math.cos
import kotlin.math.sin
import kotlin.math.sqrt

/** Pure, bounded 3D pose transform shared by the runtime and unit tests. */
internal data class SpatialStemPose(
    val x: Float,
    val y: Float,
    val z: Float,
    val width: Float,
    val gain: Float = 1f
)

internal object SpatialScenePoseMath {
    val defaultStems = listOf(
        SpatialStemPose(0f, 0f, 1.2f, 0.15f, 0.95f),    // vocals
        SpatialStemPose(0f, 0.3f, -0.8f, 0.45f, 0.85f), // drums
        SpatialStemPose(0f, -0.8f, 0.2f, 0.25f, 1.0f),  // bass
        SpatialStemPose(0f, 0.1f, 0f, 0.85f, 0.75f)     // other
    )

    fun transform(
        base: SpatialStemPose,
        azimuthRad: Float,
        elevationRad: Float,
        distanceScale: Float
    ): SpatialStemPose? {
        if (!azimuthRad.isFinite() || !elevationRad.isFinite() || !distanceScale.isFinite()) return null
        if (azimuthRad !in -Math.PI.toFloat()..Math.PI.toFloat()) return null
        if (elevationRad !in -Math.PI.toFloat() / 4f..Math.PI.toFloat() / 4f) return null
        if (distanceScale !in 0.5f..2f) return null

        val yawCos = cos(azimuthRad)
        val yawSin = sin(azimuthRad)
        val elevCos = cos(elevationRad)
        val elevSin = sin(elevationRad)

        // Azimuth rotates around the vertical z-axis; elevation tilts around y.
        val yawX = base.x * yawCos - base.y * yawSin
        val yawY = base.x * yawSin + base.y * yawCos
        val yawZ = base.z
        return SpatialStemPose(
            x = ((yawX * elevCos + yawZ * elevSin) * distanceScale).coerceIn(-2f, 2f),
            y = (yawY * distanceScale).coerceIn(-2f, 2f),
            z = ((-yawX * elevSin + yawZ * elevCos) * distanceScale).coerceIn(-2f, 2f),
            width = base.width.coerceIn(0.02f, 1f),
            // Relative distance cue: a bounded inverse-square-root gain law.
            // It changes level without pretending the room has been measured.
            gain = (base.gain / sqrt(distanceScale)).coerceIn(0f, 1.35f)
        )
    }
}


/**
 * Index mapping for the native bridge's two buffer contracts:
 * upmixer output = frame-major, four interleaved stereo stems;
 * object renderer input = object-major, planar stereo.
 */
internal object SpatialStemBufferLayout {
    fun interleavedStemIndex(frame: Int, stem: Int, channel: Int): Int =
        frame * 8 + stem * 2 + channel

    fun planarObjectIndex(stem: Int, frame: Int, channel: Int, frameCount: Int): Int =
        stem * 2 * frameCount + channel * frameCount + frame
}
