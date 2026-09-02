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
    val phaseOracleIntensity: Float = 0f,
    // Bug C — DynamicsTab AGC
    val agcTarget: Float = -18f,
    val agcRate: Float = 0.35f,
    // Bug D — BinauralTab
    val binauralAdaptEnabled: Boolean = true,
    val binauralAzimuth: Float = 0f,
    val binauralElevation: Float = 0f,
    // Bug E — NHOTab
    val nhoEta: Float = 0.5f,
    val nhoHarmonicGain: Float = 0.0f,
    val nhoLateralInhib: Float = 0.3f,
    val nhoOhcGain: Float = 0.5f,
    // Bug F — EvolutionTab
    val evoPopSize: Int = 50,
    val evoGenerations: Int = 100,
    val evoMutationRate: Float = 0.05f,
    // Supreme Audio Lab — TinyML kernel controls (persistencia real)
    val antiDolbyThreshold: Float = 0.85f,
    val spatialSuppression: Float = 0.72f,
    val spscRingFactor: Float = 0.95f,
    val tinymlInferenceGain: Float = 1.0f,
    // EQ + Compresor + Gain — AudioState es in-memory; sin esto se pierden al salir
    val eqBass: Float = 0f,
    val eqMid: Float = 0f,
    val eqTreble: Float = 0f,
    val eqPresence: Float = 0f,
    val masterGain: Float = 1.0f,
    val compressorThreshold: Float = -20f,
    val compressorRatio: Float = 2f,
    val compressorAttack: Float = 10f,
    val compressorRelease: Float = 100f
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
                phaseOracleIntensity = p.getFloat("phaseOracleIntensity", d.phaseOracleIntensity),
                agcTarget            = p.getFloat("agcTarget", d.agcTarget),
                agcRate              = p.getFloat("agcRate", d.agcRate),
                binauralAdaptEnabled = p.getBoolean("binauralAdaptEnabled", d.binauralAdaptEnabled),
                binauralAzimuth      = p.getFloat("binauralAzimuth", d.binauralAzimuth),
                binauralElevation    = p.getFloat("binauralElevation", d.binauralElevation),
                nhoEta               = p.getFloat("nhoEta", d.nhoEta),
                nhoHarmonicGain      = p.getFloat("nhoHarmonicGain", d.nhoHarmonicGain),
                nhoLateralInhib      = p.getFloat("nhoLateralInhib", d.nhoLateralInhib),
                nhoOhcGain           = p.getFloat("nhoOhcGain", d.nhoOhcGain),
                evoPopSize           = p.getInt("evoPopSize", d.evoPopSize),
                evoGenerations       = p.getInt("evoGenerations", d.evoGenerations),
                evoMutationRate      = p.getFloat("evoMutationRate", d.evoMutationRate),
                antiDolbyThreshold   = p.getFloat("antiDolbyThreshold", d.antiDolbyThreshold),
                spatialSuppression   = p.getFloat("spatialSuppression", d.spatialSuppression),
                spscRingFactor       = p.getFloat("spscRingFactor", d.spscRingFactor),
                tinymlInferenceGain  = p.getFloat("tinymlInferenceGain", d.tinymlInferenceGain),
                eqBass               = p.getFloat("eqBass", d.eqBass),
                eqMid                = p.getFloat("eqMid", d.eqMid),
                eqTreble             = p.getFloat("eqTreble", d.eqTreble),
                eqPresence           = p.getFloat("eqPresence", d.eqPresence),
                masterGain           = p.getFloat("masterGain", d.masterGain),
                compressorThreshold  = p.getFloat("compressorThreshold", d.compressorThreshold),
                compressorRatio      = p.getFloat("compressorRatio", d.compressorRatio),
                compressorAttack     = p.getFloat("compressorAttack", d.compressorAttack),
                compressorRelease    = p.getFloat("compressorRelease", d.compressorRelease)
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
                .putFloat("agcTarget", s.agcTarget)
                .putFloat("agcRate", s.agcRate)
                .putBoolean("binauralAdaptEnabled", s.binauralAdaptEnabled)
                .putFloat("binauralAzimuth", s.binauralAzimuth)
                .putFloat("binauralElevation", s.binauralElevation)
                .putFloat("nhoEta", s.nhoEta)
                .putFloat("nhoHarmonicGain", s.nhoHarmonicGain)
                .putFloat("nhoLateralInhib", s.nhoLateralInhib)
                .putFloat("nhoOhcGain", s.nhoOhcGain)
                .putInt("evoPopSize", s.evoPopSize)
                .putInt("evoGenerations", s.evoGenerations)
                .putFloat("evoMutationRate", s.evoMutationRate)
                .putFloat("antiDolbyThreshold", s.antiDolbyThreshold)
                .putFloat("spatialSuppression", s.spatialSuppression)
                .putFloat("spscRingFactor", s.spscRingFactor)
                .putFloat("tinymlInferenceGain", s.tinymlInferenceGain)
                .putFloat("eqBass", s.eqBass)
                .putFloat("eqMid", s.eqMid)
                .putFloat("eqTreble", s.eqTreble)
                .putFloat("eqPresence", s.eqPresence)
                .putFloat("masterGain", s.masterGain)
                .putFloat("compressorThreshold", s.compressorThreshold)
                .putFloat("compressorRatio", s.compressorRatio)
                .putFloat("compressorAttack", s.compressorAttack)
                .putFloat("compressorRelease", s.compressorRelease)
                .apply()
        } catch (e: Exception) {
            // no romper la UI
        }
    }
}
