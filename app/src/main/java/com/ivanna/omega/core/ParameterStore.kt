package com.ivanna.omega.core

import android.content.Context
import android.content.SharedPreferences

/**
 * ParameterStore — Persistencia de parámetros DSP en SharedPreferences.
 *
 * FIX v3.0: se amplía de 3 a los ~24 parámetros que expone
 * IvannaControlPanel v3.0 (OPE, Compresor, NHO/Espacial, Evolutivo, NPE,
 * Motor Binaural), para que MainActivity pueda restaurar el estado completo
 * de la sesión anterior al arrancar (antes sólo Exciter/EQ/Width/AntiDolby
 * sobrevivían a un reinicio; el resto volvía siempre a sus defaults de UI).
 */
class ParameterStore(context: Context) {
    private val prefs: SharedPreferences = context.getSharedPreferences(
        PREFS_NAME, Context.MODE_PRIVATE
    )

    companion object {
        private const val PREFS_NAME = "ivanna_omega_params"

        // DSP Core (ya existentes)
        private const val KEY_EXCITER = "exciter"
        private const val KEY_EQ_GAIN = "eq_gain"
        private const val KEY_WIDTH = "width"
        private const val KEY_ANTI_DOLBY = "anti_dolby"
        private const val KEY_PRESET = "current_preset"

        // Motor OPE / modo global
        private const val KEY_OMEGA_MODE = "omega_mode"
        private const val KEY_AUTO_MODE = "auto_mode"

        // Compresor
        private const val KEY_COMP_THRESHOLD = "comp_threshold"
        private const val KEY_COMP_RATIO = "comp_ratio"

        // NHO / Espacial (PDEngine)
        private const val KEY_NHO_HARMONIC = "nho_harmonic"
        private const val KEY_SPATIAL_ANGLE = "spatial_angle"
        private const val KEY_SPATIAL_WIDTH = "spatial_width"

        // Kernel evolutivo
        private const val KEY_EVO_ENABLED = "evo_enabled"

        // Motor NPE (neuromórfico)
        private const val KEY_NPE_BYPASS = "npe_bypass"
        private const val KEY_NPE_HARMONIC = "npe_harmonic"
        private const val KEY_NPE_LATERAL_INHIB = "npe_lateral_inhib"
        private const val KEY_NPE_OHC_COMPRESSION = "npe_ohc_compression"
        private const val KEY_NPE_MASTER_GAIN = "npe_master_gain"
        private const val KEY_NPE_AGC_TARGET = "npe_agc_target"
        private const val KEY_NPE_AGC_RATE = "npe_agc_rate"
        private const val KEY_NPE_HRTF = "npe_hrtf"
        private const val KEY_NPE_COCHLEAR = "npe_cochlear"
        private const val KEY_NPE_ADAPT = "npe_adapt"
        private const val KEY_NPE_MANIFOLD = "npe_manifold"

        // Motor Binaural (32 objetos)
        private const val KEY_SPATIAL_ENABLED = "spatial_enabled"
        private const val KEY_SPATIAL_INIT_PENDING = "spatial_init_pending"
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

    fun getOmegaMode(): Int = prefs.getInt(KEY_OMEGA_MODE, 0)
    fun setOmegaMode(value: Int) = prefs.edit().putInt(KEY_OMEGA_MODE, value).apply()

    fun isAutoModeEnabled(): Boolean = prefs.getBoolean(KEY_AUTO_MODE, false)
    fun setAutoModeEnabled(enabled: Boolean) = prefs.edit().putBoolean(KEY_AUTO_MODE, enabled).apply()

    // TUNED v3.1: ratio de compresión por defecto casi no comprimía (0.16:1
    // es prácticamente transparente). Se sube a un ratio suave pero real.
    fun getCompThreshold(): Float = prefs.getFloat(KEY_COMP_THRESHOLD, 0.5f)
    fun getCompRatio(): Float = prefs.getFloat(KEY_COMP_RATIO, 0.35f)
    fun setCompParams(threshold: Float, ratio: Float) = prefs.edit()
        .putFloat(KEY_COMP_THRESHOLD, threshold)
        .putFloat(KEY_COMP_RATIO, ratio)
        .apply()

    fun getNhoHarmonic(): Float = prefs.getFloat(KEY_NHO_HARMONIC, 0.0f)
    fun setNhoHarmonic(value: Float) = prefs.edit().putFloat(KEY_NHO_HARMONIC, value).apply()

    fun getSpatialAngle(): Float = prefs.getFloat(KEY_SPATIAL_ANGLE, 0.5f)
    fun setSpatialAngle(value: Float) = prefs.edit().putFloat(KEY_SPATIAL_ANGLE, value).apply()

    fun getSpatialWidth(): Float = prefs.getFloat(KEY_SPATIAL_WIDTH, 0.5f)
    fun setSpatialWidth(value: Float) = prefs.edit().putFloat(KEY_SPATIAL_WIDTH, value).apply()

    fun isEvoEnabled(): Boolean = prefs.getBoolean(KEY_EVO_ENABLED, true)
    fun setEvoEnabled(enabled: Boolean) = prefs.edit().putBoolean(KEY_EVO_ENABLED, enabled).apply()

    fun isNpeBypass(): Boolean = prefs.getBoolean(KEY_NPE_BYPASS, false)
    fun setNpeBypass(value: Boolean) = prefs.edit().putBoolean(KEY_NPE_BYPASS, value).apply()

    // TUNED v3.1: defaults de fábrica del motor NPE subidos de un punto de
    // partida casi-plano (0.2/0.2/0.3) a valores que dan presencia armónica
    // y separación audibles desde el primer arranque, sin llegar a los
    // extremos que usan los perfiles "hard rock" (Budgie llega a 0.85/0.75).
    fun getNpeHarmonic(): Float = prefs.getFloat(KEY_NPE_HARMONIC, 0.35f)
    fun getNpeLateralInhib(): Float = prefs.getFloat(KEY_NPE_LATERAL_INHIB, 0.35f)
    fun getNpeOhcCompression(): Float = prefs.getFloat(KEY_NPE_OHC_COMPRESSION, 0.4f)
    fun getNpeMasterGain(): Float = prefs.getFloat(KEY_NPE_MASTER_GAIN, 0.0f)
    fun setNpeNeuroParams(harmonic: Float, lateralInhib: Float, ohc: Float, masterGain: Float) =
        prefs.edit()
            .putFloat(KEY_NPE_HARMONIC, harmonic)
            .putFloat(KEY_NPE_LATERAL_INHIB, lateralInhib)
            .putFloat(KEY_NPE_OHC_COMPRESSION, ohc)
            .putFloat(KEY_NPE_MASTER_GAIN, masterGain)
            .apply()

    fun getNpeAgcTarget(): Float = prefs.getFloat(KEY_NPE_AGC_TARGET, -18.0f)
    fun getNpeAgcRate(): Float = prefs.getFloat(KEY_NPE_AGC_RATE, 0.3f)
    fun setNpeAgc(target: Float, rate: Float) = prefs.edit()
        .putFloat(KEY_NPE_AGC_TARGET, target)
        .putFloat(KEY_NPE_AGC_RATE, rate)
        .apply()

    fun getNpeHrtf(): Boolean = prefs.getBoolean(KEY_NPE_HRTF, true)
    fun getNpeCochlear(): Boolean = prefs.getBoolean(KEY_NPE_COCHLEAR, true)
    fun getNpeAdapt(): Boolean = prefs.getBoolean(KEY_NPE_ADAPT, true)
    fun setNpeFlags(hrtf: Boolean, cochlear: Boolean, adapt: Boolean) = prefs.edit()
        .putBoolean(KEY_NPE_HRTF, hrtf)
        .putBoolean(KEY_NPE_COCHLEAR, cochlear)
        .putBoolean(KEY_NPE_ADAPT, adapt)
        .apply()

    fun isNpeManifoldEnabled(): Boolean = prefs.getBoolean(KEY_NPE_MANIFOLD, false)
    fun setNpeManifoldEnabled(enabled: Boolean) = prefs.edit().putBoolean(KEY_NPE_MANIFOLD, enabled).apply()

    fun isSpatialEnabled(): Boolean = prefs.getBoolean(KEY_SPATIAL_ENABLED, false)
    fun setSpatialEnabled(enabled: Boolean) = prefs.edit().putBoolean(KEY_SPATIAL_ENABLED, enabled).apply()

    fun isSpatialInitPending(): Boolean = prefs.getBoolean(KEY_SPATIAL_INIT_PENDING, false)
    fun setSpatialInitPending(pending: Boolean) = prefs.edit().putBoolean(KEY_SPATIAL_INIT_PENDING, pending).apply()

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
