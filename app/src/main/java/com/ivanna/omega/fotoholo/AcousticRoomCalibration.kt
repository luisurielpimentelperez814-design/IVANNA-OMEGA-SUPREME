package com.ivanna.omega.fotoholo

import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.ln
import kotlin.math.sqrt

/**
 * Room acoustics derived from AR point-cloud bounds. These are estimates, never measured RT60/RIR.
 * Surface absorption is a selectable prior; it is not inferred from camera pixels.
 */
data class AcousticRoomCalibration(
    val widthM: Double,
    val heightM: Double,
    val depthM: Double,
    val volumeM3: Double,
    val surfaceAreaM2: Double,
    val absorptionCoefficient: Double,
    val equivalentAbsorptionM2: Double,
    val sabineRt60Seconds: Double,
    val eyringRt60Seconds: Double,
    val schroederHz: Double,
    val aspectRatio: Double,
    val spatialWidth: Float,
    val spatialAngleDeg: Float,
    val itdMicroseconds: Double,
    val materialPreset: String,
    val evidenceKind: String = "AR_POINT_CLOUD_BOUNDS_ESTIMATE",
    val measuredRirAvailable: Boolean = false
) {
    fun toJson(): org.json.JSONObject = org.json.JSONObject()
        .put("schemaVersion", 1)
        .put("evidenceKind", evidenceKind)
        .put("measuredRirAvailable", measuredRirAvailable)
        .put("dimensionsMeters", org.json.JSONObject()
            .put("width", widthM).put("height", heightM).put("depth", depthM))
        .put("volumeM3", volumeM3)
        .put("surfaceAreaM2", surfaceAreaM2)
        .put("materialPreset", materialPreset)
        .put("assumedMeanAbsorptionCoefficient", absorptionCoefficient)
        .put("equivalentAbsorptionM2", equivalentAbsorptionM2)
        .put("sabineRt60EstimateSeconds", sabineRt60Seconds)
        .put("eyringRt60EstimateSeconds", eyringRt60Seconds)
        .put("schroederFrequencyEstimateHz", schroederHz)
        .put("spatialWidthCommand", spatialWidth)
        .put("spatialAngleDegCommand", spatialAngleDeg)
        .put("interauralTimeDifferenceEstimateMicroseconds", itdMicroseconds)
        .put("limitations", org.json.JSONArray()
            .put("Dimensions are axis-aligned AR point-cloud bounds, not a closed room mesh.")
            .put("Absorption coefficient is a user-selected prior, not a measured material property.")
            .put("Sabine/Eyring RT60 and Schroeder frequency are estimates, not acoustic measurements.")
            .put("No microphone loopback, logarithmic sweep, measured RIR, or hardware-clock calibration was performed."))
}

object AcousticRoomCalibrationModel {
    data class MaterialPreset(val id: String, val label: String, val meanAbsorption: Double)

    val materials = listOf(
        MaterialPreset("hard", "Superficies duras", 0.08),
        MaterialPreset("mixed", "Mixto", 0.22),
        MaterialPreset("treated", "Tratamiento acústico", 0.48),
        MaterialPreset("absorptive", "Muy absorbente", 0.68)
    )

    fun estimate(
        widthM: Double,
        heightM: Double,
        depthM: Double,
        material: MaterialPreset = materials[1],
        azimuthDeg: Double = 0.0
    ): AcousticRoomCalibration? {
        val dimensions = listOf(widthM, heightM, depthM)
        if (dimensions.any { !it.isFinite() || it !in 0.5..30.0 }) return null
        if (!material.meanAbsorption.isFinite() || material.meanAbsorption !in 0.01..0.95) return null
        if (!azimuthDeg.isFinite()) return null

        val volume = widthM * heightM * depthM
        val area = 2.0 * (widthM * heightM + widthM * depthM + heightM * depthM)
        if (!volume.isFinite() || volume <= 0.0 || !area.isFinite() || area <= 0.0) return null
        val alpha = material.meanAbsorption
        val equivalentAbsorption = area * alpha
        val sabine = (0.161 * volume / equivalentAbsorption).coerceIn(0.05, 20.0)
        val eyringDenominator = -area * ln(1.0 - alpha)
        val eyring = if (eyringDenominator > 0.0) (0.161 * volume / eyringDenominator).coerceIn(0.05, 20.0) else sabine
        val schroeder = 2000.0 * sqrt(eyring / volume)
        val ratio = maxOf(widthM, depthM) / minOf(widthM, depthM)
        val widthCommand = ratio.coerceIn(0.75, 1.5).toFloat()
        val angle = ((azimuthDeg + 180.0) % 360.0 + 360.0) % 360.0 - 180.0
        // Woodworth spherical-head approximation, bounded to a physically plausible maximum.
        val theta = abs(angle) * PI / 180.0
        val itdUs = (0.0875 / 343.0 * (theta + kotlin.math.sin(theta)) * 1_000_000.0).coerceIn(0.0, 700.0)
        return AcousticRoomCalibration(
            widthM, heightM, depthM, volume, area, alpha, equivalentAbsorption,
            sabine, eyring, schroeder, ratio, widthCommand, angle.toFloat(), itdUs, material.label
        )
    }
}
