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

        // Sample rate nativo activo (48k/96k/192k/384k) — persiste tras
        // reinicio de app/servicio para no volver a 48k por defecto.
        private const val KEY_NATIVE_SR = "native_sample_rate_hz"

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
        // ISO 226 Calibración
        private const val KEY_ISO226_LISTEN_PHON  = "iso226_listen_phon"
        private const val KEY_ISO226_REF_PHON      = "iso226_ref_phon"
        private const val KEY_ISO226_CALIBRATED    = "iso226_calibrated"
        private const val KEY_ISO226_GAINS         = "iso226_gains"
        private const val KEY_SPATIAL_INIT_PENDING = "spatial_init_pending"

        // Adaptive Control Center
        private const val KEY_ADAPTIVE_MODE = "adaptive_mode"
        private const val KEY_ADAPTIVE_INTENSITY = "adaptive_intensity"
        private const val KEY_VOICE_PROTECTION = "voice_protection"
        private const val KEY_AUDIO_PROFILE_ID = "audio_profile_id"
        private const val KEY_ADAPTIVE_MANUAL_MODE = "adaptive_manual_mode"

        // Preset de primer arranque (IVANNA_OMEGA_SIGNATURE)
        private const val KEY_SIGNATURE_APPLIED = "signature_preset_applied"
        private const val KEY_ANTI_DOLBY_INTENSITY = "anti_dolby_intensity"
    }

    fun getExciter(): Float = prefs.getFloat(KEY_EXCITER, 0.50f) // RESOLUCIÓN v3.4: 0.35→0.50 — LPF ahora en 14.5kHz permite más drive sin aliasing
    fun setExciter(value: Float) = prefs.edit().putFloat(KEY_EXCITER, value).apply()

    fun getEqGain(): Float = prefs.getFloat(KEY_EQ_GAIN, 1.5f) // TUNED v3.1: 0.0→1.5 dB (treble/presence boost)
    fun setEqGain(value: Float) = prefs.edit().putFloat(KEY_EQ_GAIN, value).apply()

    /**
     * IVANNA_OMEGA_SIGNATURE — preset premium de primer arranque.
     *
     * Se aplica UNA sola vez (flag KEY_SIGNATURE_APPLIED): jamás sobrescribe
     * la configuración de un usuario que ya ajustó algo. Solo escribe claves
     * cuya escala está verificada en este archivo/AdaptiveControlsState:
     *   - width 1.30        → escenario más amplio sin exageración
     *   - eq_gain 1.8 dB    → detalle/presencia sin fatiga (LPF 14.5k protege)
     *   - exciter 0.55      → armónicos 2ª/3ª controlados
     *   - anti_dolby 0.85   → neutraliza compresión OEM agresiva
     *   - npe_hrtf true     → voces naturales al frente
     *   - spatial_enabled true + spatial_width 0.65 → separación instrumental
     * Los graves quedan gobernados por los RouteProfile de AudioRouteManager
     * (bassBoostDb por ruta) — escala distinta, no se toca aquí.
     * El restoreToNative() del arranque empuja estos valores al DSP nativo.
     *
     * @return true si se aplicó (primer inicio), false si ya existía.
     */
    fun applySignaturePresetIfFirstRun(): Boolean {
        if (prefs.getBoolean(KEY_SIGNATURE_APPLIED, false)) return false
        prefs.edit()
            .putFloat(KEY_WIDTH, 1.30f)
            .putFloat(KEY_EQ_GAIN, 1.8f)
            .putFloat(KEY_EXCITER, 0.55f)
            // FIX (ClassCastException): la versión anterior escribía
            // anti_dolby como Float (0.85f) pero isAntiDolbyEnabled() la
            // lee con getBoolean() → crash en el primer arranque tras
            // aplicar el signature. anti_dolby es un toggle booleano en
            // toda la app; la intensidad 0.85 va en su propia clave float.
            .putBoolean(KEY_ANTI_DOLBY, true)
            .putFloat(KEY_ANTI_DOLBY_INTENSITY, 0.85f)
            .putBoolean(KEY_NPE_HRTF, true)
            .putBoolean(KEY_SPATIAL_ENABLED, true)
            .putFloat(KEY_SPATIAL_WIDTH, 0.65f)
            .putString(KEY_PRESET, "IVANNA_OMEGA_SIGNATURE")
            .putBoolean(KEY_SIGNATURE_APPLIED, true)
            .apply()
        return true
    }

    fun getWidth(): Float = prefs.getFloat(KEY_WIDTH, 0.75f) // TUNED v3.1: 0.5→0.75 (stereo más evidente en headphones)
    fun setWidth(value: Float) = prefs.edit().putFloat(KEY_WIDTH, value).apply()

    fun getMasterVolume(): Float = prefs.getFloat("master_volume", 0.8f)
    fun setMasterVolume(value: Float) = prefs.edit().putFloat("master_volume", value.coerceIn(0f, 1f)).apply()

    fun isAntiDolbyEnabled(): Boolean = prefs.getBoolean(KEY_ANTI_DOLBY, false)
    fun setAntiDolbyEnabled(enabled: Boolean) = prefs.edit().putBoolean(KEY_ANTI_DOLBY, enabled).apply()

    fun getCurrentPreset(): String = prefs.getString(KEY_PRESET, "Warm") ?: "Warm"
    fun setCurrentPreset(name: String) = prefs.edit().putString(KEY_PRESET, name).apply()

    fun getOmegaMode(): Int = prefs.getInt(KEY_OMEGA_MODE, 0)
    fun setOmegaMode(value: Int) = prefs.edit().putInt(KEY_OMEGA_MODE, value).apply()

    fun isAutoModeEnabled(): Boolean = prefs.getBoolean(KEY_AUTO_MODE, false)
    fun setAutoModeEnabled(enabled: Boolean) = prefs.edit().putBoolean(KEY_AUTO_MODE, enabled).apply()

    fun getCompThreshold(): Float = prefs.getFloat(KEY_COMP_THRESHOLD, 0.32f) // RESOLUCIÓN v3.4: 0.375→0.32 (-12dB, comprime menos material, más dinámicas)
    fun getCompRatio(): Float = prefs.getFloat(KEY_COMP_RATIO, 0.042f) // RESOLUCIÓN v3.4: 0.105→0.042 | 3.0:1→1.8:1 (micro-transientes libres)
    fun setCompParams(threshold: Float, ratio: Float) = prefs.edit()
        .putFloat(KEY_COMP_THRESHOLD, threshold)
        .putFloat(KEY_COMP_RATIO, ratio)
        .apply()

    fun getNhoHarmonic(): Float = prefs.getFloat(KEY_NHO_HARMONIC, 0.18f) // TUNED v3.3: 0.0→0.18 (NHO activo — 2ª/3ª armónica, calidez analógica)
    fun setNhoHarmonic(value: Float) = prefs.edit().putFloat(KEY_NHO_HARMONIC, value).apply()

    fun getSpatialAngle(): Float = prefs.getFloat(KEY_SPATIAL_ANGLE, 0.5f)
    fun setSpatialAngle(value: Float) = prefs.edit().putFloat(KEY_SPATIAL_ANGLE, value).apply()

    fun getSpatialWidth(): Float = prefs.getFloat(KEY_SPATIAL_WIDTH, 0.5f)
    fun setSpatialWidth(value: Float) = prefs.edit().putFloat(KEY_SPATIAL_WIDTH, value).apply()

    fun isEvoEnabled(): Boolean = prefs.getBoolean(KEY_EVO_ENABLED, true)
    fun setEvoEnabled(enabled: Boolean) = prefs.edit().putBoolean(KEY_EVO_ENABLED, enabled).apply()

    fun isNpeBypass(): Boolean = prefs.getBoolean(KEY_NPE_BYPASS, false)
    fun setNpeBypass(value: Boolean) = prefs.edit().putBoolean(KEY_NPE_BYPASS, value).apply()

    fun getNpeHarmonic(): Float = prefs.getFloat(KEY_NPE_HARMONIC, 0.60f) // RESOLUCIÓN v3.4: 0.58→0.60 (más color armónico para sabor analógico)
    fun getNpeLateralInhib(): Float = prefs.getFloat(KEY_NPE_LATERAL_INHIB, 0.50f) // RESOLUCIÓN v3.4: 0.56→0.50 (menos inhibición = menos artificioso, más natural)
    fun getNpeOhcCompression(): Float = prefs.getFloat(KEY_NPE_OHC_COMPRESSION, 0.40f) // RESOLUCIÓN v3.4: 0.60→0.40 — CRÍTICO: OHC alta aplana micro-dinámicas que dan textura
    fun getNpeMasterGain(): Float = prefs.getFloat(KEY_NPE_MASTER_GAIN, 2.5f) // TUNED v3.3: 2.0→2.5 dB (volumen percibido NPE óptimo)
    fun setNpeNeuroParams(harmonic: Float, lateralInhib: Float, ohc: Float, masterGain: Float) =
        prefs.edit()
            .putFloat(KEY_NPE_HARMONIC, harmonic)
            .putFloat(KEY_NPE_LATERAL_INHIB, lateralInhib)
            .putFloat(KEY_NPE_OHC_COMPRESSION, ohc)
            .putFloat(KEY_NPE_MASTER_GAIN, masterGain)
            .apply()

    fun getNpeAgcTarget(): Float = prefs.getFloat(KEY_NPE_AGC_TARGET, -14.0f) // TUNED v3.3: -15→-14 dB (AGC menos restrictivo, dinámica real conservada)
    fun getNpeAgcRate(): Float = prefs.getFloat(KEY_NPE_AGC_RATE, 0.62f) // TUNED v3.3: 0.55→0.62 (respuesta AGC más ágil, sigue transientes musicales)
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

    fun getAdaptiveModeOrdinal(): Int = prefs.getInt(KEY_ADAPTIVE_MODE, 1) // NATURAL por defecto
    fun setAdaptiveModeOrdinal(value: Int) = prefs.edit().putInt(KEY_ADAPTIVE_MODE, value.coerceIn(0, 3)).apply()

    fun getAdaptiveIntensity(): Float = prefs.getFloat(KEY_ADAPTIVE_INTENSITY, 50f)
    fun setAdaptiveIntensity(value: Float) = prefs.edit().putFloat(KEY_ADAPTIVE_INTENSITY, value.coerceIn(0f, 100f)).apply()

    fun isVoiceProtectionEnabled(): Boolean = prefs.getBoolean(KEY_VOICE_PROTECTION, true)
    fun setVoiceProtectionEnabled(enabled: Boolean) = prefs.edit().putBoolean(KEY_VOICE_PROTECTION, enabled).apply()

    fun getCurrentAudioProfileId(): String? = prefs.getString(KEY_AUDIO_PROFILE_ID, null)
    fun setCurrentAudioProfileId(id: String?) = prefs.edit().putString(KEY_AUDIO_PROFILE_ID, id).apply()

    fun isAdaptiveManualModeEnabled(): Boolean = prefs.getBoolean(KEY_ADAPTIVE_MANUAL_MODE, false)
    fun setAdaptiveManualModeEnabled(enabled: Boolean) = prefs.edit().putBoolean(KEY_ADAPTIVE_MANUAL_MODE, enabled).apply()

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

    // ── ISO 226 Calibración ──────────────────────────────────────────────────

    fun saveIso226(listenPhon: Float, refPhon: Float, gains: FloatArray) {
        prefs.edit()
            .putFloat(KEY_ISO226_LISTEN_PHON, listenPhon)
            .putFloat(KEY_ISO226_REF_PHON,    refPhon)
            .putBoolean(KEY_ISO226_CALIBRATED, true)
            .putString(KEY_ISO226_GAINS, gains.joinToString(","))
            .apply()
    }

    fun loadIso226ListenPhon(): Float  = prefs.getFloat(KEY_ISO226_LISTEN_PHON,  60f)
    fun loadIso226RefPhon(): Float     = prefs.getFloat(KEY_ISO226_REF_PHON,     80f)
    fun loadIso226Calibrated(): Boolean = prefs.getBoolean(KEY_ISO226_CALIBRATED, false)
    fun loadIso226Gains(): FloatArray {
        val s = prefs.getString(KEY_ISO226_GAINS, "") ?: return FloatArray(10)
        return if (s.isBlank()) FloatArray(10)
        else s.split(",").mapNotNull { it.trim().toFloatOrNull() }.toFloatArray()
    }

    fun clearIso226() {
        prefs.edit()
            .remove(KEY_ISO226_LISTEN_PHON)
            .remove(KEY_ISO226_REF_PHON)
            .remove(KEY_ISO226_CALIBRATED)
            .remove(KEY_ISO226_GAINS)
            .apply()
    }

    // ── SSOT (Single Source Of Truth) ────────────────────────────────────────
    // core.ParameterStore es la fuente única de verdad del estado adaptativo.
    // audio.ParameterStore (blob JSON de AudioState) espeja estos 4 campos y
    // necesita saber si el usuario YA tocó estos controles para no sobreescribir
    // su configuración con defaults durante la reconciliación v1→v2.
    fun hasAdaptiveMode(): Boolean = prefs.contains(KEY_ADAPTIVE_MODE)
    fun hasAdaptiveIntensity(): Boolean = prefs.contains(KEY_ADAPTIVE_INTENSITY)
    fun hasVoiceProtection(): Boolean = prefs.contains(KEY_VOICE_PROTECTION)
    fun hasAdaptiveManualMode(): Boolean = prefs.contains(KEY_ADAPTIVE_MANUAL_MODE)

    // Presencia de las claves que el preset IVANNA_OMEGA_SIGNATURE siembra —
    // reconcileWithCore() en audio/ParameterStore las usa para llevar el
    // signature al AudioState (y de ahí, vía restoreToNative(), al DSP nativo).
    fun hasSignatureSeed(): Boolean = prefs.contains(KEY_WIDTH) &&
            prefs.contains(KEY_EQ_GAIN) && prefs.contains(KEY_EXCITER)
    fun getAntiDolbyIntensity(): Float = prefs.getFloat(KEY_ANTI_DOLBY_INTENSITY, 0.85f)

    // ── Primer lanzamiento ────────────────────────────────────────────────────
    // Controla si el preset magistral de entrada ya fue enviado al daemon.
    // Se marca una sola vez; las preferencias del usuario prevalecen después.
    fun hasAppliedFirstLaunchPreset(): Boolean =
        prefs.getBoolean("first_launch_preset_applied", false)

    fun markFirstLaunchPresetApplied() {
        prefs.edit().putBoolean("first_launch_preset_applied", true).apply()
    }

    // ── Sample rate nativo directo (48k/96k/192k/384k) ──────────────────
    // Persiste la SR activa para que el motor arranque en la misma SR tras
    // reinicio de app/servicio — el usuario no vuelve a 48k por defecto.
    fun saveNativeSampleRate(sr: Int) {
        prefs.edit().putInt(KEY_NATIVE_SR, sr).apply()
    }
    fun loadNativeSampleRate(): Int = prefs.getInt(KEY_NATIVE_SR, 48000)
}
