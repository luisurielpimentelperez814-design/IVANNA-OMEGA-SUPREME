package com.ivanna.omega.core

import android.content.Context
import android.content.SharedPreferences

/**
 * ParameterStore — Persistencia de parámetros DSP en SharedPreferences.
 * FIX v1.5: añade getters/setters para los 3 controles UI.
 */
class ParameterStore(context: Context) {
    private val prefs: SharedPreferences = context.getSharedPreferences(
        PREFS_NAME, Context.MODE_PRIVATE
    )

    companion object {
        private const val PREFS_NAME = "ivanna_omega_params"
        private const val KEY_EXCITER = "exciter"
        private const val KEY_EQ_GAIN = "eq_gain"
        private const val KEY_WIDTH = "width"
        private const val KEY_ANTI_DOLBY = "anti_dolby"
        private const val KEY_PRESET = "current_preset"
        private const val KEY_AUTO_MODE = "auto_classifier_mode"
        private const val KEY_OMEGA_MODE = "omega_pd_mode"
        private const val KEY_COMP_THRESH = "comp_threshold"
        private const val KEY_COMP_RATIO = "comp_ratio"
        private const val KEY_NHO_HARMONIC = "nho_harmonic"
        private const val KEY_SPATIAL_ANGLE = "spatial_angle"
        private const val KEY_SPATIAL_WIDTH = "spatial_width"
        private const val KEY_EVO_ENABLED = "evo_enabled"
        private const val KEY_NPE_BYPASS = "npe_bypass"
        private const val KEY_NPE_HARMONIC = "npe_harmonic_gain"
        private const val KEY_NPE_LATERAL_INHIB = "npe_lateral_inhib"
        private const val KEY_NPE_OHC_COMP = "npe_ohc_compression"
        private const val KEY_NPE_MASTER_GAIN = "npe_master_gain_db"
        private const val KEY_NPE_AGC_TARGET = "npe_agc_target_db"
        private const val KEY_NPE_AGC_RATE = "npe_agc_rate"
        private const val KEY_NPE_HRTF = "npe_hrtf_enabled"
        private const val KEY_NPE_COCHLEAR = "npe_cochlear_enabled"
        private const val KEY_NPE_ADAPT = "npe_adapt_enabled"
    }

    fun isNpeBypass(): Boolean = prefs.getBoolean(KEY_NPE_BYPASS, false)
    fun setNpeBypass(enabled: Boolean) = prefs.edit().putBoolean(KEY_NPE_BYPASS, enabled).apply()

    fun getNpeHarmonicGain(): Float = prefs.getFloat(KEY_NPE_HARMONIC, 0.2f)
    fun setNpeHarmonicGain(value: Float) = prefs.edit().putFloat(KEY_NPE_HARMONIC, value).apply()

    fun getNpeLateralInhib(): Float = prefs.getFloat(KEY_NPE_LATERAL_INHIB, 0.2f)
    fun setNpeLateralInhib(value: Float) = prefs.edit().putFloat(KEY_NPE_LATERAL_INHIB, value).apply()

    fun getNpeOhcCompression(): Float = prefs.getFloat(KEY_NPE_OHC_COMP, 0.3f)
    fun setNpeOhcCompression(value: Float) = prefs.edit().putFloat(KEY_NPE_OHC_COMP, value).apply()

    fun getNpeMasterGainDb(): Float = prefs.getFloat(KEY_NPE_MASTER_GAIN, 0.0f)
    fun setNpeMasterGainDb(value: Float) = prefs.edit().putFloat(KEY_NPE_MASTER_GAIN, value).apply()

    fun getNpeAgcTargetDb(): Float = prefs.getFloat(KEY_NPE_AGC_TARGET, -18.0f)
    fun setNpeAgcTargetDb(value: Float) = prefs.edit().putFloat(KEY_NPE_AGC_TARGET, value).apply()

    fun getNpeAgcRate(): Float = prefs.getFloat(KEY_NPE_AGC_RATE, 0.3f)
    fun setNpeAgcRate(value: Float) = prefs.edit().putFloat(KEY_NPE_AGC_RATE, value).apply()

    fun isNpeHrtfEnabled(): Boolean = prefs.getBoolean(KEY_NPE_HRTF, true)
    fun setNpeHrtfEnabled(enabled: Boolean) = prefs.edit().putBoolean(KEY_NPE_HRTF, enabled).apply()

    fun isNpeCochlearEnabled(): Boolean = prefs.getBoolean(KEY_NPE_COCHLEAR, true)
    fun setNpeCochlearEnabled(enabled: Boolean) = prefs.edit().putBoolean(KEY_NPE_COCHLEAR, enabled).apply()

    fun isNpeAdaptEnabled(): Boolean = prefs.getBoolean(KEY_NPE_ADAPT, true)
    fun setNpeAdaptEnabled(enabled: Boolean) = prefs.edit().putBoolean(KEY_NPE_ADAPT, enabled).apply()

    fun getCompThreshold(): Float = prefs.getFloat(KEY_COMP_THRESH, 0.5f)
    fun setCompThreshold(value: Float) = prefs.edit().putFloat(KEY_COMP_THRESH, value).apply()

    fun getCompRatio(): Float = prefs.getFloat(KEY_COMP_RATIO, 0.16f)
    fun setCompRatio(value: Float) = prefs.edit().putFloat(KEY_COMP_RATIO, value).apply()

    fun getNhoHarmonic(): Float = prefs.getFloat(KEY_NHO_HARMONIC, 0.0f)
    fun setNhoHarmonic(value: Float) = prefs.edit().putFloat(KEY_NHO_HARMONIC, value).apply()

    fun getSpatialAngle(): Float = prefs.getFloat(KEY_SPATIAL_ANGLE, 0.5f)
    fun setSpatialAngle(value: Float) = prefs.edit().putFloat(KEY_SPATIAL_ANGLE, value).apply()

    fun getSpatialWidth(): Float = prefs.getFloat(KEY_SPATIAL_WIDTH, 0.5f)
    fun setSpatialWidth(value: Float) = prefs.edit().putFloat(KEY_SPATIAL_WIDTH, value).apply()

    fun isEvoEnabled(): Boolean = prefs.getBoolean(KEY_EVO_ENABLED, true)
    fun setEvoEnabled(enabled: Boolean) = prefs.edit().putBoolean(KEY_EVO_ENABLED, enabled).apply()

    fun getExciter(): Float = prefs.getFloat(KEY_EXCITER, 0.3f)
    fun setExciter(value: Float) = prefs.edit().putFloat(KEY_EXCITER, value).apply()

    fun getEqGain(): Float = prefs.getFloat(KEY_EQ_GAIN, 0.0f)
    fun setEqGain(value: Float) = prefs.edit().putFloat(KEY_EQ_GAIN, value).apply()

    fun getWidth(): Float = prefs.getFloat(KEY_WIDTH, 0.5f)
    fun setWidth(value: Float) = prefs.edit().putFloat(KEY_WIDTH, value).apply()

    fun isAntiDolbyEnabled(): Boolean = prefs.getBoolean(KEY_ANTI_DOLBY, false)
    fun setAntiDolbyEnabled(enabled: Boolean) = prefs.edit().putBoolean(KEY_ANTI_DOLBY, enabled).apply()

    fun getCurrentPreset(): String = prefs.getString(KEY_PRESET, "Warm") ?: "Warm"
    fun setCurrentPreset(name: String) = prefs.edit().putString(KEY_PRESET, name).apply()

    fun isAutoModeEnabled(): Boolean = prefs.getBoolean(KEY_AUTO_MODE, false)
    fun setAutoModeEnabled(enabled: Boolean) = prefs.edit().putBoolean(KEY_AUTO_MODE, enabled).apply()

    /** 0 = DSP only, 1 = DSP+NHO, 2 = DSP+NHO+Spatial (PDEngine / OmegaEngine.setMode). */
    fun getOmegaMode(): Int = prefs.getInt(KEY_OMEGA_MODE, 0).coerceIn(0, 3)
    fun setOmegaMode(mode: Int) = prefs.edit().putInt(KEY_OMEGA_MODE, mode.coerceIn(0, 3)).apply()

    fun savePreset(name: String, exciter: Float, eq: Float, width: Float) {
        prefs.edit()
            .putFloat("${name}_exciter", exciter)
            .putFloat("${name}_eq", eq)
            .putFloat("${name}_width", width)
            .apply()
    }

    fun loadPreset(name: String): Triple<Float, Float, Float> {
        return Triple(
            prefs.getFloat("${name}_exciter", 0.3f),
            prefs.getFloat("${name}_eq", 0.0f),
            prefs.getFloat("${name}_width", 0.5f)
        )
    }
}
