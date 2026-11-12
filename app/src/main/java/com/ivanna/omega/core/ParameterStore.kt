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
    private val appContext = context.applicationContext
    private val prefs: SharedPreferences = context.getSharedPreferences(
        PREFS_NAME, Context.MODE_PRIVATE
    )

    // ── Guarda de tipo per-clave ────────────────────────────────────────────
    // El crash anti_dolby Float-vs-Boolean ya ocurrio una vez (ver FIX en
    // applySignaturePresetIfFirstRun): una escritura con tipo equivocado, un
    // restore de backup de una version vieja o un blob de otra app con el
    // mismo nombre de fichero deja la clave con tipo distinto al que el getter
    // espera, y getFloat/getBoolean lanzan ClassCastException EN EL GETTER —
    // potencialmente en el hilo de arranque. Estas lecturas seguras aplican el
    // invariante: la clave tiene el tipo esperado o se usa el default, con
    // log. Mismo patron ya aplicado en DSPStatePrefs.
    private fun safeGetFloat(key: String, def: Float): Float = try {
        prefs.getFloat(key, def)
    } catch (e: ClassCastException) {
        android.util.Log.w("ParameterStore", "clave '$key' con tipo distinto a Float — usando default $def")
        def
    }

    private fun safeGetBoolean(key: String, def: Boolean): Boolean = try {
        prefs.getBoolean(key, def)
    } catch (e: ClassCastException) {
        android.util.Log.w("ParameterStore", "clave '$key' con tipo distinto a Boolean — usando default $def")
        def
    }

    private fun safeGetInt(key: String, def: Int): Int = try {
        prefs.getInt(key, def)
    } catch (e: ClassCastException) {
        android.util.Log.w("ParameterStore", "clave '$key' con tipo distinto a Int — usando default $def")
        def
    }

    private fun safeGetString(key: String, def: String?): String? = try {
        prefs.getString(key, def)
    } catch (e: ClassCastException) {
        android.util.Log.w("ParameterStore", "clave '$key' con tipo distinto a String — usando default $def")
        def
    }

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

        // Preset de arranque sincronizado con la versión actual (IVANNA_OMEGA_SIGNATURE v2.3.18)
        private const val KEY_SIGNATURE_APPLIED = "signature_preset_applied_v2318"
        private const val KEY_ANTI_DOLBY_INTENSITY = "anti_dolby_intensity"

        // Intelligent Upmixing (HOA → Binaural)
        private const val KEY_HOA_UPMIXING_ENABLED = "hoa_upmixing_enabled"
        private const val KEY_WFS_ENABLED = "wfs_enabled"
        private const val KEY_WFS_SPREAD  = "wfs_spread"
        private const val KEY_AUX_ENABLED = "aux_field_enabled"
        private const val KEY_AUX_XOVER   = "aux_field_xover_hz"
        private const val KEY_AUX_COH     = "aux_field_coherence"
        private const val KEY_PUNCH       = "punch_amount"
        private const val KEY_CROSSFEED   = "crossfeed_strength"
        // ORBE — Binaural Esférico de Campo Cercano (v2.5.363). amount 0 = apagado; por defecto ACTIVO 1.0 (directiva 2026-11-08).
        private const val KEY_ORBE_AMOUNT       = "orbe_amount"
        private const val KEY_ORBE_DISTANCE_M   = "orbe_distance_m"
        private const val KEY_ORBE_SPREAD       = "orbe_spread"
        private const val KEY_ORBE_PROXIMITY_DB = "orbe_proximity_db"
        private const val KEY_ORBE_DIFFUSE      = "orbe_diffuse"
        private const val KEY_ORBE_HEAD_TRACKING = "orbe_head_tracking"
        // Ψ (v2.5.388): radio de cabeza personalizado (m) medido en
        // HeadMeasureScreen (foto + tarjeta de referencia). KEMAR 0.0875
        // por defecto: sin medición, el sonido no cambia.
        private const val KEY_ORBE_HEAD_RADIUS_M = "orbe_head_radius_m"
        // HOLOS — Operador Único de Campo Acústico (v2.5.364). amount 0 = apagado; por defecto ACTIVO 0.5, harmonics 0 (retuneo medido).
        private const val KEY_HOLOS_AMOUNT       = "holos_amount"
        private const val KEY_HOLOS_DISTANCE_M   = "holos_distance_m"
        private const val KEY_HOLOS_SPREAD       = "holos_spread"
        private const val KEY_HOLOS_PROXIMITY_DB = "holos_proximity_db"
        private const val KEY_HOLOS_DIFFUSE      = "holos_diffuse"
        private const val KEY_HOLOS_HARMONICS    = "holos_harmonics"
        private const val KEY_HOLOS_WARMTH       = "holos_warmth"
        private const val KEY_HOLOS_DYNAMICS     = "holos_dynamics"
        private const val KEY_HOLOS_WIDTH        = "holos_width"
        private const val KEY_HOLOS_GAMMA        = "holos_gamma"
        private const val KEY_HOLOS_ROOM_RT60    = "holos_room_rt60"
        private const val KEY_HOLOS_HEAD_TRACKING = "holos_head_tracking"
        private const val KEY_HOLOS_BAND_COUNT   = "holos_band_count"
        private const val KEY_HOLOS_EQ_LOW       = "holos_eq_low"
        private const val KEY_HOLOS_EQ_MID       = "holos_eq_mid"
        private const val KEY_HOLOS_EQ_HIGH      = "holos_eq_high"
        private const val KEY_HOLOS_EQ_PRESENCE  = "holos_eq_presence"
        private const val KEY_HOLOS_EQ_TILT_DB   = "holos_eq_tilt_db"
        private const val KEY_HOLOS_EQ_FREQ      = "holos_eq_freq"
        // IVANNA (v2.5.375). amount 1.0 = ACTIVO por defecto (directiva:
        // implementado y activo totalmente). scenePull 0..1, distancia 0.5..2.0 m.
        private const val KEY_OMNI_AMOUNT        = "omni_amount"
        private const val KEY_OMNI_SCENE_PULL    = "omni_scene_pull"
        private const val KEY_OMNI_DISTANCE_M    = "omni_distance_m"
        private const val KEY_OMNI_HEAD_TRACKING = "omni_head_tracking"
        // Hiper-Operador de Estado Φ (v2.5.369). amount 0.5 = ACTIVO por
        // defecto (directiva: todo lo nuevo suena desde la primera canción).
        private const val KEY_HYPER_AMOUNT       = "hyper_amount"
        private const val KEY_HYPER_PRESET       = "hyper_preset"
        private const val KEY_HYPER_XTC          = "hyper_xtc"
        private const val KEY_HYPER_TARGET_KIND  = "hyper_target_kind"
        private const val KEY_HYPER_BASS_TRIM_DB = "hyper_bass_trim_db"
        // Volterra H2 Genezi (v2.5.369). ON por defecto: el kernel se carga
        // al arrancar y suena salvo que el usuario lo apague (persistido).
        private const val KEY_VOLTERRA_ENABLED   = "volterra_enabled"
        // Selector de kernel Volterra (v2.5.369): device id del kernel
        // ACTIVO ("MOTO_G85_PZ1D" default, "MOTO_G85_GENEZY" alternativo).
        // Se persiste solo en éxito de carga/reemplazo, así que el
        // arranque siguiente carga el mismo kernel que sonaba. Un valor
        // desconocido o vacío se trata como el default PZ1D.
        private const val KEY_VOLTERRA_KERNEL_ID = "volterra_kernel_id"
        private const val KEY_4D_ENABLED  = "fourd_enabled"
        private const val KEY_4D_HAPTICS  = "fourd_haptics"
        private const val KEY_4D_HSCALE   = "fourd_haptic_scale"
        private const val KEY_4D_XTC      = "fourd_crosstalk"
        private const val KEY_4D_LEAD     = "fourd_lead_ms"
        private const val KEY_4D_DEPTH    = "fourd_depth"
        private const val KEY_4D_ARC      = "fourd_arc"
        private const val KEY_4D_ARCDEPTH = "fourd_arc_depth"
        private const val KEY_4D_DOSMODE  = "fourd_dos_mode"
        private const val KEY_4D_DOSCAL   = "fourd_dos_cal"
        private const val KEY_4D_OUTLAT   = "fourd_output_latency_ms"
        private const val KEY_HAPTIC_ENABLED  = "bass_haptic_enabled"
        private const val KEY_HAPTIC_STRENGTH = "bass_haptic_strength"
        private const val KEY_HAPTIC_RUMBLE   = "bass_haptic_rumble"
        private const val KEY_HAPTIC_TRIM     = "bass_haptic_trim_ms"
        private const val KEY_TEMPORAL_ENABLED = "temporal_field_enabled"
        private const val KEY_TEMPORAL_AMOUNT  = "temporal_field_amount"
        private const val KEY_TEMPORAL_AUTO    = "temporal_field_auto"
        private const val KEY_ROOM_GAINS       = "room_corr_gains"
        private const val KEY_ROOM_ENABLED     = "room_corr_enabled"
        private const val KEY_HOA_IMMERSIVITY = "hoa_immersivity"

        // v2.5.386 — MIGRACIÓN DEL SOBRE 380 (una sola vez). La 384 subió
        // HOLOS a amount 0.75 / width 1.25 / diffuse 0.65 y ORBE spread a 1.2;
        // esos valores quedaron PERSISTIDOS y el revert de la 385 solo cambió
        // los defaults — PersistedStateRestorer seguía reenviando los valores
        // 384 al daemon en cada arranque. Esta migración los clampa al sobre
        // 380 (los valores que sonaban bien) y marca la migración como hecha.
        private const val KEY_MIGRATION_386_ENVELOPE_DONE = "migration_386_envelope_done"

        // ── Perfiles de fábrica (v2.5.411, tarea 2) — UNA sola fuente de verdad ──
        // Los defaults de fábrica de la firma (ORBE/HOLOS/OmniField/Φ/Volterra)
        // viven SOLO aquí, en 3 perfiles con nombre. Los getters de abajo usan
        // BOOT_PROFILE como default: un fresh install (sin prefs guardadas)
        // arranca en "Espacial". Las preferencias EXISTENTES no se migran ni
        // se borran: si el usuario ya tiene un valor guardado (p. ej.
        // holos_harmonics=0.298, holos_eq_low=+7.8), el getter devuelve el
        // guardado, no el del perfil. El perfil de arranque es UNA constante
        // (BOOT_PROFILE); se cambiará tras el ABX.
        //   "Limpio": solo HOA+HRTF (la base; todos los operadores añadidos a 0).
        //   "Espacial": Limpio + ORBE ~0.5 + HOLOS 0.5 con K=0 (sin Φ, sin
        //     OmniField, sin Volterra).
        //   "Completo": defaults actuales del README (ORBE 1.0, HOLOS 0.5 con
        //     K=0, OmniField 1.0, Φ 0.5, Volterra ON) como perfil SELECCIONABLE,
        //     NO como arranque. El bypass de OmniField de la 410 (0.0) queda
        //     como arranque (Espacial no lo enciende) y "Completo" lo mantiene
        //     disponible para comparar por ABX; pendiente de confirmación.
        // Valores de partida (tarea 2): EQ estático de HOLOS plano (0 dB en
        // las 6 bandas), distance 0.35 m, harmonics 0, hyper_bass_trim 0,
        // preset Harman 2018 (targetKind 0) SIN corrección MEDIDA hasta que
        // haya calibración (el sensor MEDIDO solo corre con permiso
        // RECORD_AUDIO; sin él, Φ corre en APROXIMADO fail-closed).
        // La base compartida es el sobre 380 (los valores que sonaban bien).
        data class FactoryProfile(
            val name: String,
            val orbeAmount: Float,
            val orbeDistanceM: Float,
            val orbeSpread: Float,
            val orbeProximityDb: Float,
            val orbeDiffuse: Float,
            val orbeHeadTracking: Boolean,
            val orbeHeadRadiusM: Float,
            val holosAmount: Float,
            val holosDistanceM: Float,
            val holosSpread: Float,
            val holosProximityDb: Float,
            val holosDiffuse: Float,
            val holosHarmonics: Float,
            val holosWarmth: Float,
            val holosDynamics: Float,
            val holosWidth: Float,
            val holosGamma: Float,
            val holosRoomRt60: Float,
            val holosHeadTracking: Boolean,
            val holosBandCount: Int,
            val holosEqLow: Float,
            val holosEqMid: Float,
            val holosEqHigh: Float,
            val holosEqPresence: Float,
            val holosEqTiltDb: Float,
            val holosEqFreq: Float,
            val omniAmount: Float,
            val omniScenePull: Float,
            val omniDistanceM: Float,
            val omniHeadTracking: Boolean,
            val hyperAmount: Float,
            val hyperPreset: Int,
            val hyperXtc: Boolean,
            val hyperTargetKind: Int,
            val hyperBassTrimDb: Float,
            val volterraEnabled: Boolean
        )

        private val PROFILE_BASE = FactoryProfile(
            name = "Base",
            orbeAmount = 1.0f, orbeDistanceM = 0.35f, orbeSpread = 1.0f,
            orbeProximityDb = 3.0f, orbeDiffuse = 0.5f, orbeHeadTracking = true,
            orbeHeadRadiusM = 0.0875f,
            holosAmount = 0.5f, holosDistanceM = 0.35f, holosSpread = 1.0f,
            holosProximityDb = 3.0f, holosDiffuse = 0.5f, holosHarmonics = 0.0f,
            holosWarmth = 0.5f, holosDynamics = 1.0f, holosWidth = 1.0f,
            holosGamma = 1.0f, holosRoomRt60 = 0.34f, holosHeadTracking = true,
            holosBandCount = 24,
            holosEqLow = 0f, holosEqMid = 0f, holosEqHigh = 0f,
            holosEqPresence = 0f, holosEqTiltDb = 0f, holosEqFreq = 1000f,
            omniAmount = 1.0f, omniScenePull = 0.3f, omniDistanceM = 1.2f,
            omniHeadTracking = true,
            hyperAmount = 0.5f, hyperPreset = 0, hyperXtc = true,
            hyperTargetKind = 0, hyperBassTrimDb = 0f,
            volterraEnabled = true
        )

        val PROFILE_LIMPIO: FactoryProfile = PROFILE_BASE.copy(
            name = "Limpio",
            orbeAmount = 0f, holosAmount = 0f, omniAmount = 0f,
            hyperAmount = 0f, volterraEnabled = false
        )

        val PROFILE_ESPACIAL: FactoryProfile = PROFILE_BASE.copy(
            name = "Espacial",
            orbeAmount = 0.5f, holosAmount = 0.5f, holosHarmonics = 0.0f,
            omniAmount = 0f, hyperAmount = 0f, volterraEnabled = false
        )

        val PROFILE_COMPLETO: FactoryProfile = PROFILE_BASE.copy(name = "Completo")

        // El perfil de arranque es UNA constante; se cambiará tras el ABX.
        val BOOT_PROFILE: FactoryProfile = PROFILE_ESPACIAL
    }

    fun getExciter(): Float = safeGetFloat(KEY_EXCITER, 0.55f) // CALIBRACIÓN MAGISTRAL: armónicos 2ª/3ª ricos y controlados (LPF 14.5kHz sin aliasing)
    fun setExciter(value: Float) = prefs.edit().putFloat(KEY_EXCITER, value).apply()

    fun getEqGain(): Float = safeGetFloat(KEY_EQ_GAIN, 1.8f) // CALIBRACIÓN MAGISTRAL: +1.8 dB detalle, cuerpo y presencia cinematográfica/musical
    fun setEqGain(value: Float) = prefs.edit().putFloat(KEY_EQ_GAIN, value).apply()

    /**
     * IVANNA_OMEGA_SIGNATURE — Calibración Magistral de primer arranque en todos los sub-entornos.
     *
     * Se aplica UNA sola vez (flag KEY_SIGNATURE_APPLIED): jamás sobrescribe
     * la configuración de un usuario que ya ajustó algo. Prepara los motores
     * y sub-entornos (DSP Core, Anti-Dolby, NPE Neuromórfico, Volterra H2 Manifold,
     * Binaural 32 objetos, HOA 4º orden Upmixing, WFS 7 canales, ISO 226, Protección
     * Vocal para cine/diálogo y Modo Auto IA) con una calibración magistral lista
     * para música y películas. Los valores de fábrica no son una medición acústica.
     *
     * @return true si se aplicó (primer inicio), false si ya existía.
     */
    fun applySignaturePresetIfFirstRun(): Boolean {
        if (safeGetBoolean(KEY_SIGNATURE_APPLIED, false)) return false
        // Upgrade/backup without the old first-run flag must retain the user's values.
        if (prefs.all.keys.any { it != KEY_SIGNATURE_APPLIED && it != KEY_NATIVE_SR }) {
            prefs.edit().putBoolean(KEY_SIGNATURE_APPLIED, true).apply()
            return false
        }
        prefs.edit()
            .putFloat(KEY_WIDTH, 1.32f)
            .putFloat(KEY_EQ_GAIN, 1.8f)
            .putFloat(KEY_EXCITER, 0.55f)
            .putBoolean(KEY_ANTI_DOLBY, true)
            .putFloat(KEY_ANTI_DOLBY_INTENSITY, 0.85f)
            .putInt(KEY_OMEGA_MODE, 2)
            .putBoolean(KEY_AUTO_MODE, true)
            .putBoolean(KEY_EVO_ENABLED, true)
            .putBoolean(KEY_NPE_BYPASS, false)
            .putFloat(KEY_NPE_HARMONIC, 0.60f)
            .putFloat(KEY_NPE_LATERAL_INHIB, 0.50f)
            .putFloat(KEY_NPE_OHC_COMPRESSION, 0.40f)
            .putFloat(KEY_NPE_MASTER_GAIN, 2.5f)
            .putFloat(KEY_NPE_AGC_TARGET, -14.0f)
            .putFloat(KEY_NPE_AGC_RATE, 0.62f)
            .putBoolean(KEY_NPE_HRTF, true)
            .putBoolean(KEY_NPE_COCHLEAR, true)
            .putBoolean(KEY_NPE_ADAPT, true)
            .putBoolean(KEY_NPE_MANIFOLD, true)
            .putBoolean(KEY_SPATIAL_ENABLED, true)
            .putFloat(KEY_SPATIAL_WIDTH, 0.68f)
            .putFloat(KEY_NHO_HARMONIC, 0.28f)
            .putInt(KEY_ADAPTIVE_MODE, 2) // STUDIO / Magistral
            .putFloat(KEY_ADAPTIVE_INTENSITY, 82f)
            .putBoolean(KEY_VOICE_PROTECTION, true)
            .putBoolean(KEY_ADAPTIVE_MANUAL_MODE, false)
            .putBoolean(KEY_HOA_UPMIXING_ENABLED, true)
            .putFloat(KEY_HOA_IMMERSIVITY, 1.25f)
            .putBoolean(KEY_WFS_ENABLED, true)
            .putFloat(KEY_WFS_SPREAD, 1.18f)
            .putFloat(KEY_ISO226_LISTEN_PHON, 62f)
            .putFloat(KEY_ISO226_REF_PHON, 82f)
            .putBoolean(KEY_ISO226_CALIBRATED, false)
            .putString(KEY_PRESET, "IVANNA OMEGA")
            .putBoolean(KEY_SIGNATURE_APPLIED, true)
            .apply()
        return true
    }

    fun getWidth(): Float = safeGetFloat(KEY_WIDTH, 1.32f) // CALIBRACIÓN MAGISTRAL: escena amplia y tridimensional
    fun setWidth(value: Float) = prefs.edit().putFloat(KEY_WIDTH, value).apply()

    fun getMasterVolume(): Float = safeGetFloat("master_volume", 0.85f)
    fun setMasterVolume(value: Float) = prefs.edit().putFloat("master_volume", value.coerceIn(0f, 1f)).apply()

    fun isAntiDolbyEnabled(): Boolean = safeGetBoolean(KEY_ANTI_DOLBY, true)
    fun setAntiDolbyEnabled(enabled: Boolean) = prefs.edit().putBoolean(KEY_ANTI_DOLBY, enabled).apply()

    fun getCurrentPreset(): String = safeGetString(KEY_PRESET, "IVANNA OMEGA") ?: "IVANNA OMEGA"
    fun setCurrentPreset(name: String) = prefs.edit().putString(KEY_PRESET, name).apply()

    fun getOmegaMode(): Int = safeGetInt(KEY_OMEGA_MODE, 2) // 2 = OMEGA SUPREME (DSP + NHO + Spatial)
    fun setOmegaMode(value: Int) = prefs.edit().putInt(KEY_OMEGA_MODE, value).apply()

    fun isAutoModeEnabled(): Boolean = safeGetBoolean(KEY_AUTO_MODE, true)
    fun setAutoModeEnabled(enabled: Boolean) = prefs.edit().putBoolean(KEY_AUTO_MODE, enabled).apply()

    fun getCompThreshold(): Float = safeGetFloat(KEY_COMP_THRESHOLD, 0.32f) // RESOLUCIÓN v3.4: 0.375→0.32 (-12dB, comprime menos material, más dinámicas)
    fun getCompRatio(): Float = safeGetFloat(KEY_COMP_RATIO, 0.042f) // RESOLUCIÓN v3.4: 0.105→0.042 | 3.0:1→1.8:1 (micro-transientes libres)
    fun setCompParams(threshold: Float, ratio: Float) = prefs.edit()
        .putFloat(KEY_COMP_THRESHOLD, threshold)
        .putFloat(KEY_COMP_RATIO, ratio)
        .apply()

    fun getNhoHarmonic(): Float = safeGetFloat(KEY_NHO_HARMONIC, 0.28f) // CALIBRACIÓN MAGISTRAL: NHO activo — 2ª/3ª armónica, calidez analógica
    fun setNhoHarmonic(value: Float) = prefs.edit().putFloat(KEY_NHO_HARMONIC, value).apply()

    fun getSpatialAngle(): Float = safeGetFloat(KEY_SPATIAL_ANGLE, 0.5f)
    fun setSpatialAngle(value: Float) = prefs.edit().putFloat(KEY_SPATIAL_ANGLE, value).apply()

    fun getSpatialWidth(): Float = safeGetFloat(KEY_SPATIAL_WIDTH, 0.68f)
    fun setSpatialWidth(value: Float) = prefs.edit().putFloat(KEY_SPATIAL_WIDTH, value).apply()

    fun isEvoEnabled(): Boolean = safeGetBoolean(KEY_EVO_ENABLED, true)
    fun setEvoEnabled(enabled: Boolean) = prefs.edit().putBoolean(KEY_EVO_ENABLED, enabled).apply()

    fun isNpeBypass(): Boolean = safeGetBoolean(KEY_NPE_BYPASS, false)
    fun setNpeBypass(value: Boolean) = prefs.edit().putBoolean(KEY_NPE_BYPASS, value).apply()

    // ── NPE — PRECEDENCIA (v2.5.411, tarea 3b, auditoría) ──
    // Hay DOS almacenes NPE y sus valores DIFIEREN en el teléfono de Esse
    // (eso es el síntoma, no un bug de lectura): este (ivanna_omega_params)
    // y AdaptiveControlsPrefs (ivanna_adaptive_controls). Precedencia:
    //   * El motor NPE VIVO (PiLstmBridge / nativeSetNpeLocal) se alimenta
    //     de AdaptiveControlsPrefs: los sliders de la UI solo escriben ahí
    //     (LaunchedEffect en IvannaControlPanel) y PersistedStateRestorer
    //     restaura desde ahí. ESE es el autoritativo para lo que suena.
    //   * ESTOS getters solo alimentan el sync al cDSP (OemViewModel) y
    //     conservan los valores del preset de firma (applySignaturePreset-
    //     IfFirstRun: 0.60/0.50/0.40/2.5); la UI ya NO los escribe (el
    //     setter setNpeNeuroParams no tiene llamadores desde la UI).
    //   * Los FLAGS NPE (bypass/hrtf/cochlear/adapt/manifold) sí viven
    //     aquí y sí se leen en el arranque: no confundir con los valores.
    // NO se re-apunta el cDSP a AdaptiveControlsPrefs: cambiaría su sonido
    // sin evidencia ni ABX. La duplicidad queda documentada, no eliminada.
    fun getNpeHarmonic(): Float = safeGetFloat(KEY_NPE_HARMONIC, 0.60f) // RESOLUCIÓN v3.4: 0.58→0.60 (más color armónico para sabor analógico)
    fun getNpeLateralInhib(): Float = safeGetFloat(KEY_NPE_LATERAL_INHIB, 0.50f) // RESOLUCIÓN v3.4: 0.56→0.50 (menos inhibición = menos artificioso, más natural)
    fun getNpeOhcCompression(): Float = safeGetFloat(KEY_NPE_OHC_COMPRESSION, 0.40f) // RESOLUCIÓN v3.4: 0.60→0.40 — CRÍTICO: OHC alta aplana micro-dinámicas que dan textura
    fun getNpeMasterGain(): Float = safeGetFloat(KEY_NPE_MASTER_GAIN, 2.5f) // TUNED v3.3: 2.0→2.5 dB (volumen percibido NPE óptimo)
    fun setNpeNeuroParams(harmonic: Float, lateralInhib: Float, ohc: Float, masterGain: Float) =
        prefs.edit()
            .putFloat(KEY_NPE_HARMONIC, harmonic)
            .putFloat(KEY_NPE_LATERAL_INHIB, lateralInhib)
            .putFloat(KEY_NPE_OHC_COMPRESSION, ohc)
            .putFloat(KEY_NPE_MASTER_GAIN, masterGain)
            .apply()

    fun getNpeAgcTarget(): Float = safeGetFloat(KEY_NPE_AGC_TARGET, -14.0f) // TUNED v3.3: -15→-14 dB (AGC menos restrictivo, dinámica real conservada)
    fun getNpeAgcRate(): Float = safeGetFloat(KEY_NPE_AGC_RATE, 0.62f) // TUNED v3.3: 0.55→0.62 (respuesta AGC más ágil, sigue transientes musicales)
    fun setNpeAgc(target: Float, rate: Float) = prefs.edit()
        .putFloat(KEY_NPE_AGC_TARGET, target)
        .putFloat(KEY_NPE_AGC_RATE, rate)
        .apply()

    fun getNpeHrtf(): Boolean = safeGetBoolean(KEY_NPE_HRTF, true)
    fun getNpeCochlear(): Boolean = safeGetBoolean(KEY_NPE_COCHLEAR, true)
    fun getNpeAdapt(): Boolean = safeGetBoolean(KEY_NPE_ADAPT, true)
    fun setNpeFlags(hrtf: Boolean, cochlear: Boolean, adapt: Boolean) = prefs.edit()
        .putBoolean(KEY_NPE_HRTF, hrtf)
        .putBoolean(KEY_NPE_COCHLEAR, cochlear)
        .putBoolean(KEY_NPE_ADAPT, adapt)
        .apply()

    fun isNpeManifoldEnabled(): Boolean = safeGetBoolean(KEY_NPE_MANIFOLD, true)
    fun setNpeManifoldEnabled(enabled: Boolean) = prefs.edit().putBoolean(KEY_NPE_MANIFOLD, enabled).apply()

    fun isSpatialEnabled(): Boolean = safeGetBoolean(KEY_SPATIAL_ENABLED, true)
    fun setSpatialEnabled(enabled: Boolean) = prefs.edit().putBoolean(KEY_SPATIAL_ENABLED, enabled).apply()

    fun isSpatialInitPending(): Boolean = safeGetBoolean(KEY_SPATIAL_INIT_PENDING, false)
    fun setSpatialInitPending(pending: Boolean) = prefs.edit().putBoolean(KEY_SPATIAL_INIT_PENDING, pending).apply()

    fun getAdaptiveModeOrdinal(): Int = safeGetInt(KEY_ADAPTIVE_MODE, 2) // STUDIO / Magistral por defecto
    fun setAdaptiveModeOrdinal(value: Int) = prefs.edit().putInt(KEY_ADAPTIVE_MODE, value.coerceIn(0, 3)).apply()

    fun getAdaptiveIntensity(): Float = safeGetFloat(KEY_ADAPTIVE_INTENSITY, 82f)
    fun setAdaptiveIntensity(value: Float) = prefs.edit().putFloat(KEY_ADAPTIVE_INTENSITY, value.coerceIn(0f, 100f)).apply()

    fun isVoiceProtectionEnabled(): Boolean = safeGetBoolean(KEY_VOICE_PROTECTION, true)
    fun setVoiceProtectionEnabled(enabled: Boolean) = prefs.edit().putBoolean(KEY_VOICE_PROTECTION, enabled).apply()

    fun getCurrentAudioProfileId(): String? = safeGetString(KEY_AUDIO_PROFILE_ID, null)
    fun setCurrentAudioProfileId(id: String?) = prefs.edit().putString(KEY_AUDIO_PROFILE_ID, id).apply()

    fun isAdaptiveManualModeEnabled(): Boolean = safeGetBoolean(KEY_ADAPTIVE_MANUAL_MODE, false)
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
            safeGetFloat("${name}_exciter", 0.3f),
            safeGetFloat("${name}_eq", 0.0f),
            safeGetFloat("${name}_width", 0.5f)
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

    fun loadIso226ListenPhon(): Float  = safeGetFloat(KEY_ISO226_LISTEN_PHON,  62f)
    fun loadIso226RefPhon(): Float     = safeGetFloat(KEY_ISO226_REF_PHON,     82f)
    fun loadIso226Calibrated(): Boolean = safeGetBoolean(KEY_ISO226_CALIBRATED, false)
    fun loadIso226Gains(): FloatArray {
        val s = safeGetString(KEY_ISO226_GAINS, "") ?: return FloatArray(10)
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
    fun getAntiDolbyIntensity(): Float = safeGetFloat(KEY_ANTI_DOLBY_INTENSITY, 0.85f)

    // ── Primer lanzamiento ────────────────────────────────────────────────────
    // Controla si el preset magistral de entrada ya fue enviado al daemon.
    // Se marca una sola vez; las preferencias del usuario prevalecen después.
    fun hasAppliedFirstLaunchPreset(): Boolean =
        safeGetBoolean("first_launch_preset_applied", false)

    fun markFirstLaunchPresetApplied() {
        prefs.edit().putBoolean("first_launch_preset_applied", true).apply()
    }

    // ── Sample rate nativo directo (48k/96k/192k/384k) ──────────────────
    // Persiste la SR activa para que el motor arranque en la misma SR tras
    // reinicio de app/servicio — el usuario no vuelve a 48k por defecto.
    fun saveNativeSampleRate(sr: Int) {
        prefs.edit().putInt(KEY_NATIVE_SR, sr).apply()
    }
    fun loadNativeSampleRate(): Int = safeGetInt(KEY_NATIVE_SR, 48000)

    // ── Intelligent Upmixing (HOA → Binaural) ───────────────────────────────
    // Calibración Magistral: activo por defecto con inmersividad 1.25f para música y cine.
    fun isHoaUpmixingEnabled(): Boolean = safeGetBoolean(KEY_HOA_UPMIXING_ENABLED, true)
    fun setHoaUpmixingEnabled(enabled: Boolean) =
        prefs.edit().putBoolean(KEY_HOA_UPMIXING_ENABLED, enabled).apply()

    fun getHoaImmersivity(): Float = safeGetFloat(KEY_HOA_IMMERSIVITY, 1.25f)
    fun setHoaImmersivity(value: Float) =
        prefs.edit().putFloat(KEY_HOA_IMMERSIVITY, value.coerceIn(0f, 2f)).apply()

    // ── Wave Field Synthesis ── Calibración Magistral: activo por defecto con spread 1.18f
    fun isWfsEnabled(): Boolean = safeGetBoolean(KEY_WFS_ENABLED, true)
    fun setWfsEnabled(enabled: Boolean) =
        prefs.edit().putBoolean(KEY_WFS_ENABLED, enabled).apply()

    fun getWfsSpread(): Float = safeGetFloat(KEY_WFS_SPREAD, 1.18f)
    fun setWfsSpread(value: Float) =
        prefs.edit().putFloat(KEY_WFS_SPREAD, value.coerceIn(0f, 2f)).apply()

    // ── Modo AUX/altavoces (campo de presion coherente) ── apagado por defecto: no toca el audio hasta activarlo
    // ── Impacto predictivo (PunchEngine) ── 0 = apagado por defecto: bypass exacto, el audio no cambia hasta activarlo
    fun getPunchAmount(): Float = safeGetFloat(KEY_PUNCH, 0f)
    fun setPunchAmount(amount: Float) = prefs.edit().putFloat(KEY_PUNCH, amount.coerceIn(0f, 1f)).apply()
    // Crossfeed de graves con ITD (audifonos/DAC, Ruta A). 0 = apagado.
    fun getCrossfeedStrength(): Float = safeGetFloat(KEY_CROSSFEED, 0f)
    fun setCrossfeedStrength(s: Float) = prefs.edit().putFloat(KEY_CROSSFEED, if (s.isFinite()) s.coerceIn(0f, 1f) else 0f).apply()

    // ── ORBE — Binaural Esférico de Campo Cercano (v2.5.363) ── ACTIVO por defecto
    // (v2.5.369, directiva explícita: amount 1.0; THD+N −139.33 dB @1 kHz,
    // 23× RT, limpio sin retuneo). amount 0 = bypass bit-exacto.
    fun getOrbeAmount(): Float = safeGetFloat(KEY_ORBE_AMOUNT, BOOT_PROFILE.orbeAmount)
    fun getOrbeDistanceM(): Float = safeGetFloat(KEY_ORBE_DISTANCE_M, BOOT_PROFILE.orbeDistanceM)
    fun getOrbeSpread(): Float = safeGetFloat(KEY_ORBE_SPREAD, BOOT_PROFILE.orbeSpread)
    fun getOrbeProximityDb(): Float = safeGetFloat(KEY_ORBE_PROXIMITY_DB, BOOT_PROFILE.orbeProximityDb)
    fun getOrbeDiffuse(): Float = safeGetFloat(KEY_ORBE_DIFFUSE, BOOT_PROFILE.orbeDiffuse)
    fun isOrbeHeadTrackingEnabled(): Boolean = safeGetBoolean(KEY_ORBE_HEAD_TRACKING, BOOT_PROFILE.orbeHeadTracking)
    // Ψ (v2.5.388): radio de cabeza personalizado (m). KEMAR 0.0875 si
    // nunca se midió. Se clampa a [0.05, 0.15] m, igual que el nativo.
    fun getOrbeHeadRadiusM(): Float = safeGetFloat(KEY_ORBE_HEAD_RADIUS_M, BOOT_PROFILE.orbeHeadRadiusM)
    fun setOrbeHeadRadiusM(radiusM: Float) =
        prefs.edit().putFloat(KEY_ORBE_HEAD_RADIUS_M,
            if (radiusM.isFinite()) radiusM.coerceIn(0.05f, 0.15f) else 0.0875f).apply()
    fun setOrbeParams(
        amount: Float, distanceM: Float, spread: Float,
        proximityDb: Float, diffuse: Float, headTracking: Boolean
    ) = prefs.edit()
        .putFloat(KEY_ORBE_AMOUNT, if (amount.isFinite()) amount.coerceIn(0f, 1f) else 0f)
        .putFloat(KEY_ORBE_DISTANCE_M, if (distanceM.isFinite()) distanceM.coerceIn(0.15f, 2.0f) else 0.35f)
        .putFloat(KEY_ORBE_SPREAD, if (spread.isFinite()) spread.coerceIn(0f, 1.5f) else 1.0f)
        .putFloat(KEY_ORBE_PROXIMITY_DB, if (proximityDb.isFinite()) proximityDb.coerceIn(0f, 6f) else 3.0f)
        .putFloat(KEY_ORBE_DIFFUSE, if (diffuse.isFinite()) diffuse.coerceIn(0f, 1f) else 0.5f)
        .putBoolean(KEY_ORBE_HEAD_TRACKING, headTracking)
        .apply()

    // ── HOLOS — Operador Único de Campo Acústico (v2.5.364) ── ACTIVO por defecto
    // (v2.5.369, directiva explícita, CON RETUNEO: amount 0.5, harmonics 0.0.
    // Con defaults viejos (amount 1.0, harmonics 0.32) THD+N era −31.5 dB;
    // el retuneo medido en host da THD+N −94.0 dB @1 kHz / −79.6 dB @100 Hz,
    // IMD SMPTE −159.5 dB. El término K (harmonics) es el que distorsionaba;
    // con K=0 el render espacial G y el campo Γ siguen activos y limpios.)
    // Los 13 parámetros de FUSION.md §6; band_count 12 o 24 (24 = default).
    fun getHolosAmount(): Float = safeGetFloat(KEY_HOLOS_AMOUNT, BOOT_PROFILE.holosAmount)
    fun getHolosDistanceM(): Float = safeGetFloat(KEY_HOLOS_DISTANCE_M, BOOT_PROFILE.holosDistanceM)
    fun getHolosSpread(): Float = safeGetFloat(KEY_HOLOS_SPREAD, BOOT_PROFILE.holosSpread)
    fun getHolosProximityDb(): Float = safeGetFloat(KEY_HOLOS_PROXIMITY_DB, BOOT_PROFILE.holosProximityDb)
    fun getHolosDiffuse(): Float = safeGetFloat(KEY_HOLOS_DIFFUSE, BOOT_PROFILE.holosDiffuse)
    fun getHolosHarmonics(): Float = safeGetFloat(KEY_HOLOS_HARMONICS, BOOT_PROFILE.holosHarmonics)
    fun getHolosWarmth(): Float = safeGetFloat(KEY_HOLOS_WARMTH, BOOT_PROFILE.holosWarmth)
    fun getHolosDynamics(): Float = safeGetFloat(KEY_HOLOS_DYNAMICS, BOOT_PROFILE.holosDynamics)
    fun getHolosWidth(): Float = safeGetFloat(KEY_HOLOS_WIDTH, BOOT_PROFILE.holosWidth)
    fun getHolosGamma(): Float = safeGetFloat(KEY_HOLOS_GAMMA, BOOT_PROFILE.holosGamma)
    fun getHolosRoomRt60(): Float = safeGetFloat(KEY_HOLOS_ROOM_RT60, BOOT_PROFILE.holosRoomRt60)
    fun isHolosHeadTrackingEnabled(): Boolean = safeGetBoolean(KEY_HOLOS_HEAD_TRACKING, BOOT_PROFILE.holosHeadTracking)
    fun getHolosBandCount(): Int = safeGetInt(KEY_HOLOS_BAND_COUNT, BOOT_PROFILE.holosBandCount).let { if (it == 12) 12 else 24 }
    // EQ estático de Γ (dB -12..12; frecuencia del tilt 20..20000 Hz). Plano = no-op.
    fun getHolosEqLow(): Float = safeGetFloat(KEY_HOLOS_EQ_LOW, BOOT_PROFILE.holosEqLow)
    fun getHolosEqMid(): Float = safeGetFloat(KEY_HOLOS_EQ_MID, BOOT_PROFILE.holosEqMid)
    fun getHolosEqHigh(): Float = safeGetFloat(KEY_HOLOS_EQ_HIGH, BOOT_PROFILE.holosEqHigh)
    fun getHolosEqPresence(): Float = safeGetFloat(KEY_HOLOS_EQ_PRESENCE, BOOT_PROFILE.holosEqPresence)
    fun getHolosEqTiltDb(): Float = safeGetFloat(KEY_HOLOS_EQ_TILT_DB, BOOT_PROFILE.holosEqTiltDb)
    fun getHolosEqFreq(): Float = safeGetFloat(KEY_HOLOS_EQ_FREQ, BOOT_PROFILE.holosEqFreq)
    fun setHolosEq(
        low: Float, mid: Float, high: Float, presence: Float, tiltDb: Float, freq: Float
    ) = prefs.edit()
        .putFloat(KEY_HOLOS_EQ_LOW, if (low.isFinite()) low.coerceIn(-12f, 12f) else 0f)
        .putFloat(KEY_HOLOS_EQ_MID, if (mid.isFinite()) mid.coerceIn(-12f, 12f) else 0f)
        .putFloat(KEY_HOLOS_EQ_HIGH, if (high.isFinite()) high.coerceIn(-12f, 12f) else 0f)
        .putFloat(KEY_HOLOS_EQ_PRESENCE, if (presence.isFinite()) presence.coerceIn(-12f, 12f) else 0f)
        .putFloat(KEY_HOLOS_EQ_TILT_DB, if (tiltDb.isFinite()) tiltDb.coerceIn(-12f, 12f) else 0f)
        .putFloat(KEY_HOLOS_EQ_FREQ, if (freq.isFinite()) freq.coerceIn(20f, 20000f) else 1000f)
        .apply()
    fun setHolosParams(
        amount: Float, distanceM: Float, spread: Float,
        proximityDb: Float, diffuse: Float, harmonics: Float,
        warmth: Float, dynamics: Float, width: Float, gamma: Float,
        roomRt60: Float, headTracking: Boolean, bandCount: Int
    ) = prefs.edit()
        .putFloat(KEY_HOLOS_AMOUNT, if (amount.isFinite()) amount.coerceIn(0f, 1f) else 0f)
        .putFloat(KEY_HOLOS_DISTANCE_M, if (distanceM.isFinite()) distanceM.coerceIn(0.15f, 2.0f) else 0.35f)
        .putFloat(KEY_HOLOS_SPREAD, if (spread.isFinite()) spread.coerceIn(0f, 1.5f) else 1.0f)
        .putFloat(KEY_HOLOS_PROXIMITY_DB, if (proximityDb.isFinite()) proximityDb.coerceIn(0f, 6f) else 3.0f)
        .putFloat(KEY_HOLOS_DIFFUSE, if (diffuse.isFinite()) diffuse.coerceIn(0f, 1f) else 0.5f)
        .putFloat(KEY_HOLOS_HARMONICS, if (harmonics.isFinite()) harmonics.coerceIn(0f, 1f) else 0.32f)
        .putFloat(KEY_HOLOS_WARMTH, if (warmth.isFinite()) warmth.coerceIn(0f, 1f) else 0.5f)
        .putFloat(KEY_HOLOS_DYNAMICS, if (dynamics.isFinite()) dynamics.coerceIn(0f, 1f) else 1.0f)
        .putFloat(KEY_HOLOS_WIDTH, if (width.isFinite()) width.coerceIn(0f, 1.5f) else 1.0f)
        .putFloat(KEY_HOLOS_GAMMA, if (gamma.isFinite()) gamma.coerceIn(0.5f, 1f) else 1.0f)
        .putFloat(KEY_HOLOS_ROOM_RT60, if (roomRt60.isFinite()) roomRt60.coerceIn(0.18f, 1.8f) else 0.34f)
        .putBoolean(KEY_HOLOS_HEAD_TRACKING, headTracking)
        .putInt(KEY_HOLOS_BAND_COUNT, if (bandCount == 12) 12 else 24)
        .apply()

    /**
     * v2.5.386 — Migración única del sobre 380. Clampa los valores HOLOS/ORBE
     * persistidos por la 384 (amount 0.75 / width 1.25 / diffuse 0.65 /
     * orbeSpread 1.2) al sobre 380 y marca la migración como hecha. Idempotente:
     * si ya se hizo, no toca nada. Debe llamarse ANTES de que
     * PersistedStateRestorer reenvíe los valores al daemon.
     */
    fun migrateTo386Envelope() {
        if (safeGetBoolean(KEY_MIGRATION_386_ENVELOPE_DONE, false)) return
        val before = Envelope380(getHolosAmount(), getHolosWidth(), getHolosDiffuse(), getOrbeSpread())
        val after = clampTo380Envelope(before)
        prefs.edit()
            .putFloat(KEY_HOLOS_AMOUNT, after.holosAmount)
            .putFloat(KEY_HOLOS_WIDTH, after.holosWidth)
            .putFloat(KEY_HOLOS_DIFFUSE, after.holosDiffuse)
            .putFloat(KEY_ORBE_SPREAD, after.orbeSpread)
            .putBoolean(KEY_MIGRATION_386_ENVELOPE_DONE, true)
            .apply()
        if (before != after) {
            android.util.Log.i("ParameterStore",
                "Migración 386: sobre 380 aplicado (holos ${before.holosAmount}->${after.holosAmount}, " +
                "width ${before.holosWidth}->${after.holosWidth}, diffuse ${before.holosDiffuse}->${after.holosDiffuse}, " +
                "orbeSpread ${before.orbeSpread}->${after.orbeSpread})")
        }
    }

    // ── IVANNA — Operador Unificado de Campo Espacial (v2.5.375) ── BYPASS
    // por defecto desde v2.5.410 (amount 0.0). Antes ACTIVO 1.0 por
    // directiva explícita 2026-11-09; la medición host de la 410 lo
    // revoca: con tonos decorrelados y pseudo-música estéreo, OmniField
    // a 1.0 deja un residuo no-lineal añadido de −3.7/−6.1 dB (piso
    // −43.4 dB) y colapsa la fundamental (0.30 → 0.12); la cadena
    // completa termina con residuo L −2.08 / R +4.94 dB (más residuo que
    // señal en R). En bypass la cadena queda en el nivel post-HOLOS
    // (~−31.6/−34.4 dB) y se ahorran ~363 µs/bloque en el teléfono.
    // amount 0 = bypass bit-exacto (verificado en host); se reactiva en
    // un toque (setOmniParams / nativeSetOmniEnabled) si Esse lo pide
    // tras escuchar. scenePull 0..1, distancia 0.5..2.0 m.
    fun getOmniAmount(): Float = safeGetFloat(KEY_OMNI_AMOUNT, BOOT_PROFILE.omniAmount)
    fun getOmniScenePull(): Float = safeGetFloat(KEY_OMNI_SCENE_PULL, BOOT_PROFILE.omniScenePull)
    fun getOmniDistanceM(): Float = safeGetFloat(KEY_OMNI_DISTANCE_M, BOOT_PROFILE.omniDistanceM)
    fun isOmniHeadTrackingEnabled(): Boolean = safeGetBoolean(KEY_OMNI_HEAD_TRACKING, BOOT_PROFILE.omniHeadTracking)
    fun setOmniParams(
        amount: Float, scenePull: Float, distanceM: Float, headTracking: Boolean
    ) = prefs.edit()
        .putFloat(KEY_OMNI_AMOUNT, if (amount.isFinite()) amount.coerceIn(0f, 1f) else 0.0f)
        .putFloat(KEY_OMNI_SCENE_PULL, if (scenePull.isFinite()) scenePull.coerceIn(0f, 1f) else 0.3f)
        .putFloat(KEY_OMNI_DISTANCE_M, if (distanceM.isFinite()) distanceM.coerceIn(0.5f, 2.0f) else 1.2f)
        .putBoolean(KEY_OMNI_HEAD_TRACKING, headTracking)
        .apply()

    // ── Hiper-Operador de Estado Φ (v2.5.369) ── ACTIVO por defecto
    // (amount 0.5, corrección claramente audible pero suave; en APROXIMADO
    // solo corta, máx ~-1.5 dB a 0.5). preset 0 teléfono / 1 sala Genezi /
    // 2 auto. target kind 0 Harman 2018 (default) / 1 élite-lean /
    // 2 campo difuso H&M / 3 altavoces en sala. bass trim ±3 dB.
    fun getHyperAmount(): Float = safeGetFloat(KEY_HYPER_AMOUNT, BOOT_PROFILE.hyperAmount)
    fun getHyperPreset(): Int = safeGetInt(KEY_HYPER_PRESET, BOOT_PROFILE.hyperPreset).let { if (it in 0..2) it else 0 }
    fun isHyperXtcEnabled(): Boolean = safeGetBoolean(KEY_HYPER_XTC, BOOT_PROFILE.hyperXtc)
    fun getHyperTargetKind(): Int = safeGetInt(KEY_HYPER_TARGET_KIND, BOOT_PROFILE.hyperTargetKind).let { if (it in 0..3) it else 0 }
    fun getHyperBassTrimDb(): Float = safeGetFloat(KEY_HYPER_BASS_TRIM_DB, BOOT_PROFILE.hyperBassTrimDb)
    fun setHyperParams(
        amount: Float, preset: Int, xtcEnabled: Boolean,
        targetKind: Int, bassTrimDb: Float
    ) = prefs.edit()
        .putFloat(KEY_HYPER_AMOUNT, if (amount.isFinite()) amount.coerceIn(0f, 1f) else 0.5f)
        .putInt(KEY_HYPER_PRESET, if (preset in 0..2) preset else 0)
        .putBoolean(KEY_HYPER_XTC, xtcEnabled)
        .putInt(KEY_HYPER_TARGET_KIND, if (targetKind in 0..3) targetKind else 0)
        .putFloat(KEY_HYPER_BASS_TRIM_DB, if (bassTrimDb.isFinite()) bassTrimDb.coerceIn(-3f, 3f) else 0f)
        .apply()

    // ── Volterra H2 Genezi (v2.5.369) ── ON por defecto. Sin valor
    // persistido, el kernel se carga al arrancar y suena. Apagarlo en la
    // UI persiste y se respeta en el siguiente arranque.
    fun isVolterraEnabled(): Boolean = safeGetBoolean(KEY_VOLTERRA_ENABLED, BOOT_PROFILE.volterraEnabled)
    fun setVolterraEnabled(v: Boolean) = prefs.edit().putBoolean(KEY_VOLTERRA_ENABLED, v).apply()

    // ── Selector de kernel Volterra (v2.5.369) ── device id del kernel
    // activo. Default PZ1D (el refinado); el Genezi es el alternativo.
    // Solo IvannaNativeLib lo escribe, y solo en éxito de carga o
    // reemplazo, así que nunca apunta a un kernel que no sonó.
    fun getVolterraKernelId(): String {
        val id = try { prefs.getString(KEY_VOLTERRA_KERNEL_ID, null) } catch (_: Throwable) { null }
        return if (id == "MOTO_G85_GENEZY" || id == "MOTO_G85_PZ1D") id else "MOTO_G85_PZ1D"
    }
    fun setVolterraKernelId(id: String) {
        if (id != "MOTO_G85_GENEZY" && id != "MOTO_G85_PZ1D") return
        prefs.edit().putString(KEY_VOLTERRA_KERNEL_ID, id).apply()
    }

    // ── Cadena 4D (anticipación, arco, háptica, diafonía, dosímetro) ── todo apagado por defecto
    fun is4dEnabled(): Boolean = safeGetBoolean(KEY_4D_ENABLED, false)
    fun set4dEnabled(v: Boolean) = prefs.edit().putBoolean(KEY_4D_ENABLED, v).apply()
    fun is4dHapticsOn(): Boolean = safeGetBoolean(KEY_4D_HAPTICS, false)
    fun set4dHapticsOn(v: Boolean) = prefs.edit().putBoolean(KEY_4D_HAPTICS, v).apply()
    fun get4dHapticScale(): Float = safeGetFloat(KEY_4D_HSCALE, 0.6f)
    fun set4dHapticScale(v: Float) = prefs.edit().putFloat(KEY_4D_HSCALE, v.coerceIn(0f, 0.85f)).apply()
    // An explicit external-line-out declaration is the safe default opt-in for XTC.
    // A persisted false remains authoritative, so users can always disable it.
    fun is4dCrosstalkOn(): Boolean {
        val declaredLineOut = appContext.getSharedPreferences("ivanna_spatial_audio", Context.MODE_PRIVATE)
            .getBoolean("externalStereoLineOut", false)
        return safeGetBoolean(KEY_4D_XTC, declaredLineOut)
    }
    fun set4dCrosstalkOn(v: Boolean) = prefs.edit().putBoolean(KEY_4D_XTC, v).apply()
    fun get4dLeadMs(): Float = safeGetFloat(KEY_4D_LEAD, 7.5f)
    fun set4dLeadMs(v: Float) = prefs.edit().putFloat(KEY_4D_LEAD, v.coerceIn(5f, 10f)).apply()
    fun get4dDepth(): Float = safeGetFloat(KEY_4D_DEPTH, 0.7f)
    fun set4dDepth(v: Float) = prefs.edit().putFloat(KEY_4D_DEPTH, v.coerceIn(0f, 1f)).apply()
    fun is4dArcOn(): Boolean = safeGetBoolean(KEY_4D_ARC, false)
    fun set4dArcOn(v: Boolean) = prefs.edit().putBoolean(KEY_4D_ARC, v).apply()
    fun get4dArcDepth(): Float = safeGetFloat(KEY_4D_ARCDEPTH, 0.5f)
    fun set4dArcDepth(v: Float) = prefs.edit().putFloat(KEY_4D_ARCDEPTH, v.coerceIn(0f, 1f)).apply()
    fun get4dDosimeterMode(): Int = safeGetInt(KEY_4D_DOSMODE, 0).coerceIn(0, 2)
    fun set4dDosimeterMode(v: Int) = prefs.edit().putInt(KEY_4D_DOSMODE, v.coerceIn(0, 2)).apply()
    fun get4dDosimeterCal(): Float = safeGetFloat(KEY_4D_DOSCAL, 94f)
    fun set4dDosimeterCal(v: Float) = prefs.edit().putFloat(KEY_4D_DOSCAL, v.coerceIn(70f, 145f)).apply()

    // Latencia de salida real del dispositivo. Sin este dato el planificador 4D
    // no puede abrir el campo 5-10 ms ANTES del golpe: se quedaba con el valor
    // interno del motor y no habia forma de corregirlo desde la app.
    fun get4dOutputLatencyMs(): Float = safeGetFloat(KEY_4D_OUTLAT, 12f)
    fun set4dOutputLatencyMs(v: Float) = prefs.edit().putFloat(KEY_4D_OUTLAT, v.coerceIn(0f, 40f)).apply()

    fun isAuxFieldEnabled(): Boolean = safeGetBoolean(KEY_AUX_ENABLED, false)
    fun getAuxXoverHz(): Float = safeGetFloat(KEY_AUX_XOVER, 120f)
    fun getAuxCoherence(): Float = safeGetFloat(KEY_AUX_COH, 1f)
    fun setAuxField(enabled: Boolean, xoverHz: Float, coherence: Float) =
        prefs.edit().putBoolean(KEY_AUX_ENABLED, enabled)
            .putFloat(KEY_AUX_XOVER, xoverHz.coerceIn(60f, 200f))
            .putFloat(KEY_AUX_COH, coherence.coerceIn(0f, 1f)).apply()

    // ── Corrección acústica de sala (medida con el micrófono) ── sin medición no hay nada que aplicar
    /** Las 10 ganancias medidas (dB), o null si nunca se midió. */
    fun getRoomCorrectionGains(): FloatArray? {
        val raw = safeGetString(KEY_ROOM_GAINS, null) ?: return null
        val parts = raw.split(',')
        if (parts.size != 10) return null
        val g = FloatArray(10)
        for (i in 0 until 10) g[i] = parts[i].toFloatOrNull()?.coerceIn(-12f, 12f) ?: return null
        return g
    }
    fun isRoomCorrectionEnabled(): Boolean = safeGetBoolean(KEY_ROOM_ENABLED, false) && getRoomCorrectionGains() != null
    fun setRoomCorrection(gains: FloatArray?, enabled: Boolean) {
        val e = prefs.edit()
        if (gains != null && gains.size == 10) e.putString(KEY_ROOM_GAINS, gains.joinToString(",") { "%.2f".format(java.util.Locale.US, it) })
        else e.remove(KEY_ROOM_GAINS)
        e.putBoolean(KEY_ROOM_ENABLED, enabled && gains != null).apply()
    }

    // ── Dinámica espacial temporal (4D) ── apagada por defecto: no toca el audio hasta activarla
    fun isTemporalFieldEnabled(): Boolean = safeGetBoolean(KEY_TEMPORAL_ENABLED, false)
    fun getTemporalFieldAmount(): Float = safeGetFloat(KEY_TEMPORAL_AMOUNT, 0.6f)
    fun isTemporalFieldAdaptive(): Boolean = safeGetBoolean(KEY_TEMPORAL_AUTO, true)
    fun setTemporalField(enabled: Boolean, amount: Float, adaptive: Boolean = isTemporalFieldAdaptive()) =
        prefs.edit().putBoolean(KEY_TEMPORAL_ENABLED, enabled).putBoolean(KEY_TEMPORAL_AUTO, adaptive)
            .putFloat(KEY_TEMPORAL_AMOUNT, amount.coerceIn(0f, 1f)).apply()

    // ── Háptica de graves (4D) ── apagada por defecto
    fun isBassHapticEnabled(): Boolean = safeGetBoolean(KEY_HAPTIC_ENABLED, false)
    fun getBassHapticStrength(): Float = safeGetFloat(KEY_HAPTIC_STRENGTH, 0.7f)
    fun getBassHapticRumble(): Float = safeGetFloat(KEY_HAPTIC_RUMBLE, 0f)
    fun getBassHapticTrimMs(): Float = safeGetFloat(KEY_HAPTIC_TRIM, 0f)
    fun setBassHaptic(enabled: Boolean, strength: Float, rumble: Float = getBassHapticRumble(), trimMs: Float = getBassHapticTrimMs()) =
        prefs.edit().putBoolean(KEY_HAPTIC_ENABLED, enabled)
            .putFloat(KEY_HAPTIC_TRIM, trimMs.coerceIn(-100f, 200f))
            .putFloat(KEY_HAPTIC_STRENGTH, strength.coerceIn(0f, 1f))
            .putFloat(KEY_HAPTIC_RUMBLE, rumble.coerceIn(0f, 1f)).apply()
}

// ===========================================================================
// SOBRE 380 — v2.5.386 (top-level, NO en companion).
// Los cuatro valores persistidos que la migración 386 clampa al sobre 380
// (los que sonaban bien en la v2.5.380). Techos: NO subir.
// NOTA 2026-11-10: estaban dentro del companion object y
// compileReleaseUnitTestKotlin no los resolvía desde el test
// ("Unresolved reference 'Envelope380'") ni como `internal` ni como públicos.
// Movidos a top-level: el test los usa sin calificador (mismo paquete) y
// migrateTo386Envelope() los sigue resolviendo (mismo archivo/paquete).
// ===========================================================================

/** Techos del sobre 380 — NO subir. */
const val ENVELOPE_380_HOLOS_AMOUNT = 0.5f
const val ENVELOPE_380_HOLOS_WIDTH = 1.0f
const val ENVELOPE_380_HOLOS_DIFFUSE = 0.5f
const val ENVELOPE_380_ORBE_SPREAD = 1.0f

/** Los cuatro valores persistidos que la migración 386 clampa. */
data class Envelope380(
    val holosAmount: Float,
    val holosWidth: Float,
    val holosDiffuse: Float,
    val orbeSpread: Float
)

/** Pura y testeable: clampa valores persistidos al sobre 380. */
fun clampTo380Envelope(e: Envelope380): Envelope380 {
    fun clamp(v: Float, max: Float, def: Float): Float =
        if (v.isFinite()) v.coerceIn(0f, max) else def
    return Envelope380(
        holosAmount = clamp(e.holosAmount, ENVELOPE_380_HOLOS_AMOUNT, ENVELOPE_380_HOLOS_AMOUNT),
        holosWidth = clamp(e.holosWidth, ENVELOPE_380_HOLOS_WIDTH, ENVELOPE_380_HOLOS_WIDTH),
        holosDiffuse = clamp(e.holosDiffuse, ENVELOPE_380_HOLOS_DIFFUSE, ENVELOPE_380_HOLOS_DIFFUSE),
        orbeSpread = clamp(e.orbeSpread, ENVELOPE_380_ORBE_SPREAD, ENVELOPE_380_ORBE_SPREAD)
    )
}
