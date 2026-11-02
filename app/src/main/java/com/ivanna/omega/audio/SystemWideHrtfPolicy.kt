package com.ivanna.omega.audio

import android.content.Context
import com.ivanna.omega.ui.SpatialAudioPrefs

/**
 * Single route-policy gate for HRTF writes into the SYSTEM_WIDE AudioFlinger effect.
 * UI panels may keep a saved "requested" switch enabled, but this gate prevents
 * headphone HRIR processing from being re-enabled on built-in speakers or an
 * explicitly declared external line-out.
 */
object SystemWideHrtfPolicy {
    fun allow(context: Context, requested: Boolean, route: OutputRoute = AudioRouteManager.detectOutputRoute()): Boolean {
        val declaredLineOut = runCatching {
            SpatialAudioPrefs.load(context.applicationContext).externalStereoLineOut
        }.getOrDefault(false)
        return ExternalStereoPolicy.allowHeadphoneHrtf(route, declaredLineOut, requested)
    }
}
