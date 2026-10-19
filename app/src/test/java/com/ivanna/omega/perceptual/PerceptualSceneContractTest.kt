package com.ivanna.omega.perceptual

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class PerceptualSceneContractTest {
    private fun axisStatuses(overrides: Map<PerceptualAxis, AxisStatus> = emptyMap()): List<AxisStatus> =
        PerceptualAxis.entries.map { axis ->
            overrides[axis] ?: AxisStatus(
                axis = axis,
                state = AxisState.UNAVAILABLE,
                unit = "n/a",
                reason = "No verified provider configured"
            )
        }

    private fun validSnapshot(
        axes: List<AxisStatus> = axisStatuses(),
        room: RoomGeometry? = null,
        hrtf: HrtfProvenance? = null,
        calibration: SceneCalibration = SceneCalibration()
    ) = PerceptualSceneSnapshot(
        sceneId = "test-scene",
        capturedAtEpochMs = 1_800_000_000_000L,
        outputRoute = OutputRoute.HEADPHONES,
        sources = listOf(PerceptualSourcePose("music", Vec3Meters(0f, 0f, 1f))),
        axes = axes,
        room = room,
        hrtf = hrtf,
        calibration = calibration
    )

    @Test fun allTenAxesCanBeUnavailableHonestly() {
        val snapshot = validSnapshot()
        assertTrue(snapshot.validate().toString(), snapshot.validate().isEmpty())
        assertTrue(snapshot.axes.map { it.axis }.toSet().containsAll(PerceptualAxis.entries))
    }

    @Test fun missingAndDuplicateAxesFailValidation() {
        val axes = axisStatuses().dropLast(1) + axisStatuses().first()
        val errors = validSnapshot(axes = axes).validate()
        assertTrue(errors.any { it.contains("must appear exactly once") })
    }

    @Test fun appliedAxisRequiresEngineConfirmationAndTimestamp() {
        val statuses = axisStatuses(mapOf(
            PerceptualAxis.AZIMUTH to AxisStatus(
                axis = PerceptualAxis.AZIMUTH,
                state = AxisState.APPLIED,
                unit = "deg",
                requestedValue = 30f,
                effectiveValue = 30f,
                engineConfirmed = false
            )
        ))
        val errors = validSnapshot(axes = statuses).validate()
        assertTrue(errors.any { it.contains("without engine confirmation") })
        assertTrue(errors.any { it.contains("without a timestamp") })
    }

    @Test fun nanAndInfiniteCoordinatesAreRejected() {
        val snapshot = validSnapshot().copy(
            sources = listOf(
                PerceptualSourcePose("bad", Vec3Meters(Float.NaN, 0f, Float.POSITIVE_INFINITY))
            )
        )
        assertTrue(snapshot.validate().any { it.contains("positionMeters is not finite") })
    }

    @Test fun hrtfCannotBeAppliedWithoutDatasetProvenance() {
        val axes = axisStatuses(mapOf(
            PerceptualAxis.HRTF_PERSONALIZATION to AxisStatus(
                axis = PerceptualAxis.HRTF_PERSONALIZATION,
                state = AxisState.APPLIED,
                unit = "dataset",
                engineConfirmed = true,
                updatedAtEpochMs = 1_800_000_000_000L
            )
        ))
        assertTrue(validSnapshot(axes = axes).validate().any {
            it.contains("without identified dataset and subject provenance")
        })
    }

    @Test fun appliedHrtfPersonalizationRequiresSubjectId() {
        val axes = axisStatuses(mapOf(
            PerceptualAxis.HRTF_PERSONALIZATION to AxisStatus(
                axis = PerceptualAxis.HRTF_PERSONALIZATION,
                state = AxisState.APPLIED,
                unit = "dataset",
                engineConfirmed = true,
                updatedAtEpochMs = 1_800_000_000_000L
            )
        ))
        val hrtf = HrtfProvenance(
            datasetId = "measured-dataset",
            subjectId = null,
            sampleRateHz = 48_000,
            kind = HrtfProvenanceKind.MEASURED,
            measuredAtEpochMs = 1_800_000_000_000L
        )
        assertTrue(validSnapshot(axes = axes, hrtf = hrtf).validate().any {
            it.contains("without identified dataset and subject provenance")
        })
    }

    @Test fun roomGeometryCannotBeAppliedFromPresetAlone() {
        val axes = axisStatuses(mapOf(
            PerceptualAxis.ROOM_GEOMETRY to AxisStatus(
                axis = PerceptualAxis.ROOM_GEOMETRY,
                state = AxisState.APPLIED,
                unit = "m",
                engineConfirmed = true,
                updatedAtEpochMs = 1_800_000_000_000L
            )
        ))
        val room = RoomGeometry(
            widthMeters = 4f,
            depthMeters = 5f,
            heightMeters = 2.5f,
            listenerPositionMeters = Vec3Meters(2f, 1f, 1.2f)
        )
        val errors = validSnapshot(
            axes = axes,
            room = room,
            calibration = SceneCalibration(
                kind = CalibrationKind.PRESET,
                calibrationId = "generic-room-preset"
            )
        ).validate()
        assertTrue(errors.any { it.contains("without measured room calibration") })
    }

    @Test fun measuredRoomAndIdentifiedHrtfPassStructuralValidation() {
        val timestamp = 1_800_000_000_000L
        val axes = axisStatuses(mapOf(
            PerceptualAxis.ROOM_GEOMETRY to AxisStatus(
                axis = PerceptualAxis.ROOM_GEOMETRY,
                state = AxisState.APPLIED,
                unit = "m",
                engineConfirmed = true,
                updatedAtEpochMs = timestamp
            ),
            PerceptualAxis.HRTF_PERSONALIZATION to AxisStatus(
                axis = PerceptualAxis.HRTF_PERSONALIZATION,
                state = AxisState.APPLIED,
                unit = "dataset",
                engineConfirmed = true,
                updatedAtEpochMs = timestamp
            )
        ))
        val snapshot = validSnapshot(
            axes = axes,
            room = RoomGeometry(
                widthMeters = 4f,
                depthMeters = 5f,
                heightMeters = 2.5f,
                listenerPositionMeters = Vec3Meters(2f, 1f, 1.2f),
                measuredAtEpochMs = timestamp
            ),
            hrtf = HrtfProvenance(
                datasetId = "dataset-verified-001",
                subjectId = "subject-001",
                sampleRateHz = 48_000,
                kind = HrtfProvenanceKind.MEASURED,
                measuredAtEpochMs = timestamp
            ),
            calibration = SceneCalibration(
                kind = CalibrationKind.MEASURED,
                calibrationId = "room-measurement-001",
                measuredAtEpochMs = timestamp,
                confidence = 0.95f
            )
        )
        assertTrue(snapshot.validate().toString(), snapshot.validate().isEmpty())
        assertFalse(snapshot.axes.any { it.state == AxisState.APPLIED && !it.engineConfirmed })
    }
}
