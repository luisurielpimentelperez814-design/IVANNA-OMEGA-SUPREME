package com.ivanna.omega.perceptual

/**
 * Versioned, dependency-free contract for a perceptual scene.
 *
 * This model deliberately separates requested settings from engine-confirmed state.
 * It is a validation boundary, not evidence that a DSP route is already wired to it.
 */
enum class PerceptualAxis {
    AZIMUTH,
    ELEVATION,
    DISTANCE,
    MOVEMENT,
    HEAD_ROTATION,
    HRTF_PERSONALIZATION,
    ROOM_GEOMETRY,
    REVERBERANT_FIELD,
    SOURCE_SEPARATION,
    AV_HAPTIC_SYNC
}

enum class AxisState { REQUESTED, APPLIED, DEGRADED, UNAVAILABLE, ERROR }
enum class OutputRoute { HEADPHONES, AUX, USB_DAC, SPEAKERS, UNKNOWN }
enum class CalibrationKind { UNCALIBRATED, PRESET, MEASURED }
enum class HrtfProvenanceKind { MEASURED, INTERPOLATED, SYNTHETIC, UNKNOWN }

data class Vec3Meters(val x: Float, val y: Float, val z: Float)

data class PerceptualSourcePose(
    val sourceId: String,
    val positionMeters: Vec3Meters,
    val velocityMetersPerSecond: Vec3Meters? = null
)

data class RoomGeometry(
    val widthMeters: Float,
    val depthMeters: Float,
    val heightMeters: Float,
    val listenerPositionMeters: Vec3Meters,
    val measuredAtEpochMs: Long? = null
)

data class HrtfProvenance(
    val datasetId: String,
    val subjectId: String? = null,
    val sampleRateHz: Int,
    val kind: HrtfProvenanceKind,
    val measuredAtEpochMs: Long? = null
)

data class SceneCalibration(
    val kind: CalibrationKind = CalibrationKind.UNCALIBRATED,
    val calibrationId: String? = null,
    val measuredAtEpochMs: Long? = null,
    val confidence: Float? = null
)

data class AxisStatus(
    val axis: PerceptualAxis,
    val state: AxisState,
    val unit: String,
    val requestedValue: Float? = null,
    val effectiveValue: Float? = null,
    /** True only after read-back/acknowledgement from the actual processing engine. */
    val engineConfirmed: Boolean = false,
    val reason: String? = null,
    val updatedAtEpochMs: Long? = null
)

data class PerceptualSceneSnapshot(
    val schemaVersion: Int = CURRENT_SCHEMA_VERSION,
    val sceneId: String,
    val capturedAtEpochMs: Long,
    val outputRoute: OutputRoute,
    val sources: List<PerceptualSourcePose>,
    val axes: List<AxisStatus>,
    val room: RoomGeometry? = null,
    val hrtf: HrtfProvenance? = null,
    val calibration: SceneCalibration = SceneCalibration()
) {
    /**
     * Returns every contract violation. Empty means structurally valid only;
     * it does not mean the scene was applied or acoustically verified.
     */
    fun validate(): List<String> = buildList {
        if (schemaVersion != CURRENT_SCHEMA_VERSION) add("unsupported schemaVersion=$schemaVersion")
        if (sceneId.isBlank()) add("sceneId is blank")
        if (capturedAtEpochMs <= 0L) add("capturedAtEpochMs must be positive")
        if (outputRoute == OutputRoute.UNKNOWN) add("outputRoute is unknown")

        val sourceIds = sources.map { it.sourceId }
        if (sourceIds.any { it.isBlank() }) add("sourceId is blank")
        if (sourceIds.size != sourceIds.toSet().size) add("sourceId values must be unique")
        sources.forEachIndexed { index, source ->
            if (!source.positionMeters.isFinite()) add("sources[$index].positionMeters is not finite")
            source.velocityMetersPerSecond?.let {
                if (!it.isFinite()) add("sources[$index].velocityMetersPerSecond is not finite")
            }
        }

        val axisCounts = axes.groupingBy { it.axis }.eachCount()
        PerceptualAxis.entries.forEach { axis ->
            if (axisCounts[axis] != 1) add("axis $axis must appear exactly once")
        }
        axes.forEachIndexed { index, status ->
            if (status.unit.isBlank()) add("axes[$index].unit is blank")
            if (status.requestedValue != null && !status.requestedValue.isFinite()) {
                add("axes[$index].requestedValue is not finite")
            }
            if (status.effectiveValue != null && !status.effectiveValue.isFinite()) {
                add("axes[$index].effectiveValue is not finite")
            }
            if (status.state == AxisState.APPLIED && !status.engineConfirmed) {
                add("axes[$index] is APPLIED without engine confirmation")
            }
            if (status.state in setOf(AxisState.DEGRADED, AxisState.UNAVAILABLE, AxisState.ERROR) &&
                status.reason.isNullOrBlank()
            ) add("axes[$index] needs a reason for state ${status.state}")
            if (status.state == AxisState.APPLIED && (status.updatedAtEpochMs ?: 0L) <= 0L) {
                add("axes[$index] is APPLIED without a timestamp")
            }
        }

        room?.let {
            if (!it.widthMeters.isFinite() || it.widthMeters <= 0f) add("room.widthMeters must be finite and positive")
            if (!it.depthMeters.isFinite() || it.depthMeters <= 0f) add("room.depthMeters must be finite and positive")
            if (!it.heightMeters.isFinite() || it.heightMeters <= 0f) add("room.heightMeters must be finite and positive")
            if (!it.listenerPositionMeters.isFinite()) add("room.listenerPositionMeters is not finite")
            if (calibration.kind == CalibrationKind.MEASURED && (it.measuredAtEpochMs ?: 0L) <= 0L) {
                add("measured room geometry needs measuredAtEpochMs")
            }
        }

        calibration.confidence?.let {
            if (!it.isFinite() || it !in 0f..1f) add("calibration.confidence must be in [0,1]")
        }
        if (calibration.kind == CalibrationKind.MEASURED) {
            if (calibration.calibrationId.isNullOrBlank()) add("measured calibration needs calibrationId")
            if ((calibration.measuredAtEpochMs ?: 0L) <= 0L) add("measured calibration needs measuredAtEpochMs")
        }

        hrtf?.let {
            if (it.datasetId.isBlank()) add("hrtf.datasetId is blank")
            if (it.sampleRateHz <= 0) add("hrtf.sampleRateHz must be positive")
            if (it.kind == HrtfProvenanceKind.MEASURED && (it.measuredAtEpochMs ?: 0L) <= 0L) {
                add("measured HRTF needs measuredAtEpochMs")
            }
        }
        val hrtfAxis = axes.singleOrNull { it.axis == PerceptualAxis.HRTF_PERSONALIZATION }
        if (hrtfAxis?.state == AxisState.APPLIED && (hrtf == null || hrtf.datasetId.isBlank() || hrtf.kind == HrtfProvenanceKind.UNKNOWN)) {
            add("HRTF axis cannot be APPLIED without identified dataset provenance")
        }
        val roomAxis = axes.singleOrNull { it.axis == PerceptualAxis.ROOM_GEOMETRY }
        if (roomAxis?.state == AxisState.APPLIED && (room == null || calibration.kind != CalibrationKind.MEASURED)) {
            add("room geometry axis cannot be APPLIED without measured room calibration")
        }
    }

    companion object {
        const val CURRENT_SCHEMA_VERSION: Int = 1
    }
}

private fun Vec3Meters.isFinite(): Boolean = x.isFinite() && y.isFinite() && z.isFinite()
