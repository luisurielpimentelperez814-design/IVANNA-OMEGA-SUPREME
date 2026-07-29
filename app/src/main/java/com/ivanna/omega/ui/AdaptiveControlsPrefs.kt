package com.ivanna.omega.ui

import android.content.Context

data class AdaptiveControlsState(
    val antiDolbyEnabled: Boolean = false,
    val selectedPreset: String = "Warm",
    val autoMode: Boolean = false,
    val omegaMode: Int = 0,
    val nhoHarmonic: Float = 0.0f,
    val spatialAngle: Float = 0.5f,
    val spatialWidth: Float = 0.5f,
    val hrtfWet: Float = 0f,
    val reflectionGain0: Float = 0f,
    val reflectionGain1: Float = 0f,
    val reflectionGain2: Float = 0f,
    val reflectionGain3: Float = 0f,
    val evoEnabled: Boolean = true,
    val npeBypass: Boolean = false,
    val npeHarmonic: Float = 0.2f,
    val npeLateralInhib: Float = 0f,
    val npeOhcCompression: Float = 0f,
    val npeMasterGain: Float = 0.0f,
    val npeAgcTarget: Float = -18.0f,
    val npeAgcRate: Float = 0.3f,
    val npeHrtf: Boolean = true,
    val npeCochlear: Boolean = true,
    val npeAdapt: Boolean = true,
    val npeManifold: Boolean = false,
    val spatialEnabled: Boolean = false,
    val phaseOracleIntensity: Float = 0f
)

object AdaptiveControlsPrefs {
    private const val PREFS_NAME = "ivanna_adaptive_controls"

    fun load(context: Context): AdaptiveControlsState {
        return try {
            val p = context.applicationContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
            val d = AdaptiveControlsState()
            AdaptiveControlsState(
                antiDolbyEnabled     = p.getBoolean("antiDolbyEnabled", d.antiDolbyEnabled),
                selectedPreset       = p.getString("selectedPreset", d.selectedPreset) ?: d.selectedPreset,
                autoMode             = p.getBoolean("autoMode", d.autoMode),
                omegaMode            = p.getInt("omegaMode", d.omegaMode),
                nhoHarmonic          = p.getFloat("nhoHarmonic", d.nhoHarmonic),
                spatialAngle         = p.getFloat("spatialAngle", d.spatialAngle),
                spatialWidth         = p.getFloat("spatialWidth", d.spatialWidth),
                hrtfWet              = p.getFloat("hrtfWet", d.hrtfWet),
                reflectionGain0      = p.getFloat("reflectionGain0", d.reflectionGain0),
                reflectionGain1      = p.getFloat("reflectionGain1", d.reflectionGain1),
                reflectionGain2      = p.getFloat("reflectionGain2", d.reflectionGain2),
                reflectionGain3      = p.getFloat("reflectionGain3", d.reflectionGain3),
                evoEnabled           = p.getBoolean("evoEnabled", d.evoEnabled),
                npeBypass            = p.getBoolean("npeBypass", d.npeBypass),
                npeHarmonic          = p.getFloat("npeHarmonic", d.npeHarmonic),
                npeLateralInhib      = p.getFloat("npeLateralInhib", d.npeLateralInhib),
                npeOhcCompression    = p.getFloat("npeOhcCompression", d.npeOhcCompression),
                npeMasterGain        = p.getFloat("npeMasterGain", d.npeMasterGain),
                npeAgcTarget         = p.getFloat("npeAgcTarget", d.npeAgcTarget),
                npeAgcRate           = p.getFloat("npeAgcRate", d.npeAgcRate),
                npeHrtf              = p.getBoolean("npeHrtf", d.npeHrtf),
                npeCochlear          = p.getBoolean("npeCochlear", d.npeCochlear),
                npeAdapt             = p.getBoolean("npeAdapt", d.npeAdapt),
                npeManifold          = p.getBoolean("npeManifold", d.npeManifold),
                spatialEnabled       = p.getBoolean("spatialEnabled", d.spatialEnabled),
                phaseOracleIntensity = p.getFloat("phaseOracleIntensity", d.phaseOracleIntensity)
            )
        } catch (e: Exception) {
            AdaptiveControlsState()
        }
    }

    fun save(context: Context, s: AdaptiveControlsState) {
        try {
            val p = context.applicationContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
            p.edit()
                .putBoolean("antiDolbyEnabled", s.antiDolbyEnabled)
                .putString("selectedPreset", s.selectedPreset)
                .putBoolean("autoMode", s.autoMode)
                .putInt("omegaMode", s.omegaMode)
                .putFloat("nhoHarmonic", s.nhoHarmonic)
                .putFloat("spatialAngle", s.spatialAngle)
                .putFloat("spatialWidth", s.spatialWidth)
                .putFloat("hrtfWet", s.hrtfWet)
                .putFloat("reflectionGain0", s.reflectionGain0)
                .putFloat("reflectionGain1", s.reflectionGain1)
                .putFloat("reflectionGain2", s.reflectionGain2)
                .putFloat("reflectionGain3", s.reflectionGain3)
                .putBoolean("evoEnabled", s.evoEnabled)
                .putBoolean("npeBypass", s.npeBypass)
                .putFloat("npeHarmonic", s.npeHarmonic)
                .putFloat("npeLateralInhib", s.npeLateralInhib)
                .putFloat("npeOhcCompression", s.npeOhcCompression)
                .putFloat("npeMasterGain", s.npeMasterGain)
                .putFloat("npeAgcTarget", s.npeAgcTarget)
                .putFloat("npeAgcRate", s.npeAgcRate)
                .putBoolean("npeHrtf", s.npeHrtf)
                .putBoolean("npeCochlear", s.npeCochlear)
                .putBoolean("npeAdapt", s.npeAdapt)
                .putBoolean("npeManifold", s.npeManifold)
                .putBoolean("spatialEnabled", s.spatialEnabled)
                .putFloat("phaseOracleIntensity", s.phaseOracleIntensity)
                .apply()
        } catch (e: Exception) {
            // no romper la UI
        }
    }
}
