package com.ivanna.omega.safmus

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.hardware.camera2.CameraCaptureSession
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CaptureRequest
import android.os.Handler
import android.util.Log
import android.util.Range

/**
 * Session-owner adapter for an already-open Camera2 session.
 * It never opens the camera, asks for permission, or bypasses the owning Activity.
 * The caller supplies its live builder/session and retains capture-result callbacks.
 */
object SafmusCameraAdapter {
    private const val TAG = "SAFMUS-CameraAdapter"

    data class ApplyResult(
        val applied: Boolean,
        val requestedFps: Range<Int>?,
        val previousFps: Range<Int>?,
        val reason: String
    )

    fun applyFrameRateBudget(
        context: Context,
        session: CameraCaptureSession,
        builder: CaptureRequest.Builder,
        characteristics: CameraCharacteristics,
        budget: SafmusHardwareCoordinator.Budget,
        telemetryTimestampMs: Long,
        callback: CameraCaptureSession.CaptureCallback,
        handler: Handler
    ): ApplyResult {
        val permission = context.checkSelfPermission(Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED
        val ranges = characteristics.get(CameraCharacteristics.CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES)
            ?.toList().orEmpty()
        val previous = runCatching { builder.get(CaptureRequest.CONTROL_AE_TARGET_FPS_RANGE) }.getOrNull()
        val snapshotFresh = System.currentTimeMillis() - telemetryTimestampMs in 0L..15_000L
        val candidate = when (budget) {
            SafmusHardwareCoordinator.Budget.NORMAL -> previous
                ?: ranges.maxByOrNull { it.upper }
            SafmusHardwareCoordinator.Budget.CONSERVATIVE -> ranges
                .filter { it.lower >= 15 && it.upper <= 30 }
                .minByOrNull { it.upper }
            SafmusHardwareCoordinator.Budget.SAFE -> ranges
                .filter { it.lower >= 10 && it.upper <= 24 }
                .minByOrNull { it.upper }
        }
        val gate = SafmusUniversalKernel.gateHardwareOptimization(
            SafmusUniversalKernel.HardwareResource.CAMERA,
            SafmusUniversalKernel.HardwareOptimizationEvidence(
                capabilityDetected = ranges.isNotEmpty(),
                permissionGranted = permission,
                owningSessionConfirmed = session.device.id.isNotBlank(),
                telemetryFresh = snapshotFresh,
                configurationSupported = candidate != null && candidate in ranges,
                resourceBudgetValid = budget != SafmusHardwareCoordinator.Budget.SAFE ||
                    ranges.any { it.lower >= 10 && it.upper <= 24 },
                // The owner can restore the prior explicit value or clear the key (null)
                // to return Android's camera HAL to its default policy.
                rollbackAvailable = true
            )
        )
        if (!gate.allowed) return ApplyResult(false, candidate, previous, gate.reason)
        return try {
            builder.set(CaptureRequest.CONTROL_AE_TARGET_FPS_RANGE, candidate)
            session.setRepeatingRequest(builder.build(), callback, handler)
            Log.i(TAG, "SAFMUS budget=$budget requestedFps=$candidate; awaiting capture-result readback")
            ApplyResult(true, candidate, previous, "Solicitud enviada; aún requiere read-back del CaptureResult")
        } catch (t: Throwable) {
            runCatching {
                builder.set(CaptureRequest.CONTROL_AE_TARGET_FPS_RANGE, previous)
                session.setRepeatingRequest(builder.build(), callback, handler)
            }
            ApplyResult(false, candidate, previous, "Falló la solicitud; reversión intentada: ${t.message}")
        }
    }

    /** Call from the owning session's onCaptureCompleted callback. */
    fun confirmReadback(
        requested: Range<Int>?,
        result: android.hardware.camera2.CaptureResult
    ): SafmusUniversalKernel.Gate {
        val effective = result.get(CaptureRequest.CONTROL_AE_TARGET_FPS_RANGE)
        val gate = SafmusUniversalKernel.confirmHardwareOptimization(
            SafmusUniversalKernel.Gate(requested != null, "No hay solicitud de FPS válida"),
            effective == requested
        )
        if (!gate.allowed) Log.w(TAG, "SAFMUS camera read-back rejected: effective=$effective requested=$requested")
        return gate
    }
}
