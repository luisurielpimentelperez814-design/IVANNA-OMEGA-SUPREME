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
    }

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
    fun getOmegaMode(): Int = prefs.getInt(KEY_OMEGA_MODE, 0).coerceIn(0, 2)
    fun setOmegaMode(mode: Int) = prefs.edit().putInt(KEY_OMEGA_MODE, mode.coerceIn(0, 2)).apply()

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
