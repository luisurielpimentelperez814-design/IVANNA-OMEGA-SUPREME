package com.ivanna.omega.ui

import android.content.Context

/** Estado persistente del panel Spatial Audio (HRTF + RIR + SAF). */
data class SpatialAudioState(
    val hrtfEnabled: Boolean = true,
    val hrtfSubject: String = "kemar_subject_165",
    val rirEnabled: Boolean = false,
    val rirRt60: Float = 0.5f,
    val rirWet: Float = 0.35f,
    val safEnabled: Boolean = false,
    val safIntensity: Float = 0.5f,
    val safAutoMode: Boolean = true
)

object SpatialAudioPrefs {
    private const val PREFS = "ivanna_spatial_audio"

    fun load(context: Context): SpatialAudioState {
        val p = context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        val d = SpatialAudioState()
        return SpatialAudioState(
            hrtfEnabled  = p.getBoolean("hrtfEnabled", d.hrtfEnabled),
            hrtfSubject  = p.getString("hrtfSubject", d.hrtfSubject) ?: d.hrtfSubject,
            rirEnabled   = p.getBoolean("rirEnabled", d.rirEnabled),
            rirRt60      = p.getFloat("rirRt60", d.rirRt60),
            rirWet       = p.getFloat("rirWet", d.rirWet),
            safEnabled   = p.getBoolean("safEnabled", d.safEnabled),
            safIntensity = p.getFloat("safIntensity", d.safIntensity),
            safAutoMode  = p.getBoolean("safAutoMode", d.safAutoMode)
        )
    }

    fun save(context: Context, s: SpatialAudioState) {
        context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
            .putBoolean("hrtfEnabled", s.hrtfEnabled)
            .putString("hrtfSubject", s.hrtfSubject)
            .putBoolean("rirEnabled", s.rirEnabled)
            .putFloat("rirRt60", s.rirRt60)
            .putFloat("rirWet", s.rirWet)
            .putBoolean("safEnabled", s.safEnabled)
            .putFloat("safIntensity", s.safIntensity)
            .putBoolean("safAutoMode", s.safAutoMode)
            .apply()
    }
}
