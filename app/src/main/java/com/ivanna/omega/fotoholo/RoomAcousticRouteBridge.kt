package com.ivanna.omega.fotoholo

import android.content.Context
import android.util.Log
import com.ivanna.omega.audio.OutputRoute
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.core.NativeBridge
import org.json.JSONObject
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min

/**
 * Applies the last user-locked AR room prior to the currently active output route.
 *
 * AR geometry/material values are priors, not measured room response. This bridge
 * only sends bounded commands to the existing native DSP controls; it does not
 * claim that an IPC/native command changed audible output unless read-back agrees.
 */
object RoomAcousticRouteBridge {
    private const val TAG = "RoomAcousticRouteBridge"
    private const val PREFS = "ivanna_rp88_acoustic_calibration"
    private const val KEY_JSON = "calibration_json"

    /**
     * Called after route DSP stages are selected and after a user explicitly locks
     * a room scan. The route factor preserves the conservative output policy:
     * Bluetooth and phone speaker do not inherit the full wired width.
     */
    fun applySaved(context: Context, route: OutputRoute): Boolean {
        if (route == OutputRoute.UNKNOWN) return false
        if (!IvannaNativeLib.isLoaded || !NativeBridge.isLoaded) {
            Log.i(TAG, "AR room prior retained; native bridge unavailable for route=$route")
            return false
        }

        val json = runCatching {
            context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
                .getString(KEY_JSON, null)?.let(::JSONObject)
        }.getOrNull() ?: return false

        // Only apply a calibration explicitly locked by the user; an incomplete
        // scan must never silently retune the global audio path.
        val prefs = context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        if (!prefs.getBoolean("lock_requested", false)) return false

        val width = json.optDouble("spatialWidthCommand", Double.NaN)
        val angleDeg = json.optDouble("spatialAngleDegCommand", Double.NaN)
        val absorption = json.optDouble("assumedMeanAbsorptionCoefficient", Double.NaN)
        if (!width.isFinite() || width !in 0.75..1.5 ||
            !angleDeg.isFinite() || angleDeg !in -90.0..90.0 ||
            !absorption.isFinite() || absorption !in 0.01..0.95) {
            Log.w(TAG, "Rejected invalid persisted room calibration for route=$route")
            return false
        }

        val routeWidthFactor = when (route) {
            OutputRoute.BLUETOOTH -> 0.88f
            OutputRoute.SPEAKER -> 0.85f
            OutputRoute.WIRED_AUX, OutputRoute.USB -> 1.0f
            OutputRoute.UNKNOWN -> return false
        }
        val routedWidth = (width.toFloat() * routeWidthFactor).coerceIn(0.75f, 1.5f)
        val angleRad = Math.toRadians(angleDeg).toFloat()

        return runCatching {
            IvannaNativeLib.nativeSetSpatialWidthDirect(routedWidth)
            IvannaNativeLib.nativeSetSpatialAngleRad(angleRad)
            NativeBridge.safeSetPhysicalSceneParams(
                occlusionFactor = 0.0f,
                wallAbsorption = absorption.toFloat()
            )

            val widthReadback = IvannaNativeLib.nativeGetSpatialWidthDirect()
            val angleReadback = IvannaNativeLib.nativeGetSpatialAngleRad()
            val sceneReadback = NativeBridge.safeGetPhysicalSceneSnapshot()
            val widthConfirmed = widthReadback.isFinite() && abs(widthReadback - routedWidth) <= 0.15f
            val angleConfirmed = angleReadback.isFinite() && abs(angleReadback - angleRad) <= 0.02f
            val absorptionConfirmed = sceneReadback != null && sceneReadback.size >= 2 &&
                sceneReadback[1].isFinite() && abs(sceneReadback[1] - absorption.toFloat()) <= 0.05f

            prefs.edit()
                .putString("last_applied_route", route.name)
                .putFloat("last_route_spatial_width_command", routedWidth)
                .putBoolean("last_route_width_readback_confirmed", widthConfirmed)
                .putBoolean("last_route_angle_readback_confirmed", angleConfirmed)
                .putBoolean("last_route_absorption_readback_confirmed", absorptionConfirmed)
                .putLong("last_route_apply_at_ms", System.currentTimeMillis())
                .apply()

            Log.i(TAG, "route=$route width=$routedWidth readbackWidth=$widthConfirmed " +
                "readbackAngle=$angleConfirmed readbackAbsorption=$absorptionConfirmed; " +
                "read-back confirms control values only, not measured acoustic response")
            widthConfirmed && angleConfirmed && absorptionConfirmed
        }.getOrElse {
            Log.w(TAG, "Could not apply saved room prior to route=$route", it)
            false
        }
    }
}
