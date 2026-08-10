/*
 * © 2026 Luis Uriel Pimentel Pérez — IVANNA N-P-E
 * IvannaGlobalEffectManager.kt
 *
 * Sistema de intercepción de audio global (sin root).
 * Mecanismo idéntico al que usa Wavelet EQ y Poweramp Equalizer:
 *
 *   1. Android emite el broadcast OPEN_AUDIO_EFFECT_CONTROL_SESSION
 *      cada vez que CUALQUIER app (Spotify, YouTube, Apple Music, etc.)
 *      abre una sesión de audio.
 *   2. Nuestro AudioSessionReceiver captura el sessionId.
 *   3. IvannaGlobalEffectManager crea instancias de AudioEffect nativas
 *      (Equalizer, BassBoost, Virtualizer, LoudnessEnhancer, DynamicsProcessing)
 *      en esa sesión con prioridad máxima (Int.MAX_VALUE), descartando cualquier
 *      otro efecto del sistema.
 *   4. Los parámetros los expone el IVANNA engine (alpha/beta/neuro params)
 *      mapeados a las bandas del Equalizer y los controles de efecto.
 *   5. Cuando la sesión se cierra (CLOSE_AUDIO_EFFECT_CONTROL_SESSION),
 *      los efectos se liberan sin memory leak.
 *
 * LIMITACIÓN TÉCNICA HONESTA:
 *   El DSP de convolución profunda (PI-LSTM + Cochlear Manifold) NO puede
 *   inyectarse en el proceso de audio de otra app sin privilegios de sistema.
 *   Lo que sí se aplica globalmente son: EQ paramétrico 10 bandas, BassBoost,
 *   Virtualizer estéreo, LoudnessEnhancer y DynamicsProcessing (compresor).
 *   Para IvannaBridgePlayer (reproductor propio), el pipeline IVANNA completo
 *   sigue activo en toda su profundidad.
 */
package com.ivanna.omega.audio

import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

import android.media.audiofx.BassBoost
import android.media.audiofx.DynamicsProcessing
import android.media.audiofx.Equalizer
import android.media.audiofx.LoudnessEnhancer
import android.media.audiofx.Virtualizer
import android.os.Build
import android.util.Log
import java.util.UUID
import java.util.concurrent.ConcurrentHashMap

data class IvannaEffectProfile(
    // EQ: 10 bandas @ 31/63/125/250/500/1k/2k/4k/8k/16kHz, milliBels (-1500..+1500)
    // TUNED v3.3: default es la curva "IVANNA signature" — enhancement balanceado
    val eqBands: IntArray = intArrayOf(80, 60, 30, 0, -20, 0, 60, 100, 120, 100),
    // BassBoost: 0–1000
    val bassStrength: Short = 420,
    // Virtualizer: 0–1000
    val virtualizerStrength: Short = 380,
    // LoudnessEnhancer: ganancia en mB (0–1000)
    val loudnessGainMb: Int = 80,
    // Compresor (DynamicsProcessing): threshold dBFS, ratio
    val compThresholdDb: Float = -15f,
    val compRatio: Float = 3.0f
) {
    companion object {
        // ── FLAT — referencia limpia ────────────────────────────────────────────
        val FLAT = IvannaEffectProfile(
            eqBands = intArrayOf(0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
            bassStrength = 0, virtualizerStrength = 0, loudnessGainMb = 0,
            compThresholdDb = -24f, compRatio = 1.5f
        )

        // ── WARM — calidez analógica, vocales en primer plano ──────────────────
        // TUNED v3.3: eliminado el dip en 1kHz que huecaba las vocales.
        // Curva Baxandall cálida con presence sutil y air controlado.
        val WARM = IvannaEffectProfile(
            eqBands = intArrayOf(180, 160, 120, 80, 40, 0, 0, 30, 70, 90),
            bassStrength = 420, virtualizerStrength = 280, loudnessGainMb = 160,
            compThresholdDb = -16f, compRatio = 2.8f
        )

        // ── ROCK 70s — cuerpo, punch, presencia de guitarra ───────────────────
        // TUNED v3.3: mids más presentes, 4kHz destacado (ataque de guitarra),
        // treble controlado (no sibilante). Zeppelin, Floyd, Sabbath.
        val ROCK_70S = IvannaEffectProfile(
            eqBands = intArrayOf(160, 200, 160, 100, 40, 60, 180, 240, 170, 100),
            bassStrength = 560, virtualizerStrength = 460, loudnessGainMb = 200,
            compThresholdDb = -14f, compRatio = 3.2f
        )

        // ── SPATIAL — escenario headphone, imagen stereo magistral ─────────────
        // TUNED v3.3: presencia 2-4kHz boosteada (posicionamiento 3D),
        // sub controlado, virtualizer al máximo musical sin fatiga.
        val SPATIAL = IvannaEffectProfile(
            eqBands = intArrayOf(80, 60, 40, 0, -40, 20, 120, 200, 230, 190),
            bassStrength = 280, virtualizerStrength = 720, loudnessGainMb = 0,
            compThresholdDb = -16f, compRatio = 2.5f
        )

        // ── PUNCH — EDM / Hip-hop / Trap / Reggaetón ──────────────────────────
        // TUNED v3.3: sub-bass autoridad, mids limpios, presencia controlada.
        // Compresión más agresiva para ese golpe de bajo de EDM.
        val PUNCH = IvannaEffectProfile(
            eqBands = intArrayOf(280, 240, 160, 60, 0, 0, 60, 120, 150, 110),
            bassStrength = 680, virtualizerStrength = 200, loudnessGainMb = 300,
            compThresholdDb = -12f, compRatio = 4.0f
        )

        // ── IVANNA OMEGA — preset firma prodigio magistral ─────────────────────
        // El sonido definitivo de IVANNA: autoridad de bajo, mids cristalinos,
        // presencia que corta, aire que respira. Pop/R&B/Soul/Electrónica/Modern.
        val IVANNA_OMEGA = IvannaEffectProfile(
            eqBands = intArrayOf(200, 160, 100, 40, 0, 0, 80, 160, 200, 160),
            bassStrength = 540, virtualizerStrength = 460, loudnessGainMb = 120,
            compThresholdDb = -14f, compRatio = 3.2f
        )

        // mapa nombre → perfil para la UI (LazyRow de FilterChip)
        val byName: Map<String, IvannaEffectProfile> = linkedMapOf(
            "Flat"         to FLAT,
            "Warm"         to WARM,
            "Rock 70s"     to ROCK_70S,
            "Spatial"      to SPATIAL,
            "Punch"        to PUNCH,
            "IVANNA OMEGA" to IVANNA_OMEGA
        )
    }
}

class IvannaGlobalEffectManager(
    private val context: android.content.Context
) {


    private val TAG = "IvannaNPE.GlobalFX"

    companion object {
        // AUDIT FIX (no-root omite el motor Omega): UUID real del efecto
        // compilado en omega_effect.cpp (effect_uuid_t layout AOSP:
        // timeLow=0x4956414e "IVAN", timeMid=0x4e41 "NA",
        // timeHiAndVersion=0x4f4d "OM", clockSeq=0x4547 "EG",
        // node={41,53,55,50,52,45} "ASUPRE"). NO usar el UUID citado en
        // auditorías externas (8d7d5e0a-...) — no coincide con el binario.
        private val OMEGA_EFFECT_UUID: UUID =
            UUID.fromString("4956414e-4e41-4f4d-4547-415355505245")

        // EFFECT_TYPE_NULL no existe en la API pública del SDK (es constante
        // interna de AOSP); el patrón para efectos custom es pasar el UUID
        // de tipo nulo explícito como primer argumento del constructor.
        private val EFFECT_TYPE_NULL_UUID: UUID =
            UUID.fromString("00000000-0000-0000-0000-000000000000")
    }

    private fun writeToLogFile(message: String) {
        try {
            val logFile = File(context.filesDir, "ivanna_audio_debug.txt")
            val timestamp =
                SimpleDateFormat(
                    "yyyy-MM-dd HH:mm:ss.SSS",
                    Locale.US
                ).format(Date())

            logFile.appendText("[$timestamp] $message\n")

        } catch (_: Exception) {
        }
    }

    fun clearDebugLog() {
        try {
            val logFile = File(context.filesDir, "ivanna_audio_debug.txt")
            if (logFile.exists()) {
                logFile.delete()
            }
            writeToLogFile("=== IVANNA DEBUG START ===")

        } catch (_: Exception) {
        }
    }

    fun getDebugLogPath(): String {
        return File(
            context.filesDir,
            "ivanna_audio_debug.txt"
        ).absolutePath
    }


    // Mapa sessionId → lista de efectos activos en esa sesión
    private val activeSessions = ConcurrentHashMap<Int, SessionEffects>()

    // Perfil activo (se aplica a todas las sesiones nuevas y actualiza las existentes)
    @Volatile var activeProfile: IvannaEffectProfile = IvannaEffectProfile.WARM
        private set

    private data class SessionEffects(
        val equalizer:         Equalizer?,
        val bassBoost:         BassBoost?,
        val virtualizer:       Virtualizer?,
        val loudness:          LoudnessEnhancer?,
        val dynamics:          DynamicsProcessing?,
        // AUDIT FIX: instancia del efecto Omega custom (libomega_effect.so).
        // Solo no-null cuando el módulo Magisk está instalado y AudioFlinger
        // tiene la librería registrada en soundfx; en no-root el constructor
        // lanza y se queda en null (fallback silencioso a los efectos stock).
        // Se referencia con nombre completo porque com.ivanna.omega.audio.effects
        // .AudioEffect (interface propia) ensombrece a android.media.audiofx
        // .AudioEffect en el resolver de Kotlin -> error package-private.
        val omega:             android.media.audiofx.AudioEffect? = null
    )


    private fun releaseEffects(
        fx: SessionEffects
    ) {
        runCatching { fx.equalizer?.release() }
        runCatching { fx.bassBoost?.release() }
        runCatching { fx.virtualizer?.release() }
        runCatching { fx.loudness?.release() }
        runCatching { fx.dynamics?.release() }
        runCatching { fx.omega?.release() }
    }
    
    // ── ISO 226 Calibración ──────────────────────────────────────────────────

    /**
     * Devuelve el perfil activo actual (para que Iso226Calibrator pueda
     * hacer .copy(eqBands = ...) sin romper el preset activo).
     */
    fun currentProfile(): IvannaEffectProfile = activeProfile

    /**
     * Aplica la curva de compensación ISO 226 al Equalizer de todas las
     * sesiones abiertas, preservando los parámetros de BassBoost/Virtualizer
     * del preset activo.
     *
     * @param gainsDb FloatArray[10] — compensación en dB por banda EQ
     *                Bandas: 31/63/125/250/500/1k/2k/4k/8k/16k Hz
     */
    fun applyIso226Compensation(gainsDb: FloatArray) {
        val clamped = IntArray(10) { i ->
            (gainsDb.getOrElse(i) { 0f }.coerceIn(-12f, 12f) * 100f).toInt()
        }
        val baseEq   = activeProfile.eqBands
        val mergedEq = IntArray(10) { i ->
            (baseEq.getOrElse(i) { 0 } + clamped[i]).coerceIn(-1500, 1500)
        }
        val isoProfile = activeProfile.copy(eqBands = mergedEq)
        applyProfile(isoProfile)
        // FIX: persistir automáticamente para que la calibración sobreviva reinicios.
        // Iso226Calibrator.persist() guarda listenPhon+refPhon en SharedPreferences;
        // IVANNAApplication.restoreIfSaved() la recupera al arrancar.
        runCatching { com.ivanna.omega.audio.Iso226Calibrator.persist(context) }
        android.util.Log.i("IvannaGlobalFX", "ISO 226 EQ aplicado: ${clamped.map { "${it/100f}dB" }}")
    }

    // ── Abre efectos para una nueva sesión de audio ───────────────────────────
    fun openSession(
        audioSession: Int,
        sourcePackage: String?
    ) {

        writeToLogFile(
            "openSession $audioSession source=$sourcePackage"
        )
        if (audioSession <= 0) return
        if (activeSessions.containsKey(audioSession)) return

        Log.i(TAG, "Abriendo sesión $audioSession (${sourcePackage ?: "desconocido"})")

        // AUDIT FIX (Omega DSP nunca se adjuntaba por sesión): se intenta
        // primero el efecto custom por UUID. Si el módulo Magisk está
        // instalado, cada sesión queda enrutada por el IvannaFusionCore real
        // (DSP por instancia, aislado entre sesiones) y el control llega vía
        // OmegaControlBus/SHM — el efecto no implementa EFFECT_CMD_SET_PARAM
        // por diseño, el canal de parámetros es el bus, no setParameter().
        // En dispositivos sin el módulo el constructor lanza
        // (IllegalArgumentException/RuntimeException) y omega queda en null:
        // comportamiento idéntico al anterior (solo efectos stock).
        val omega = createOmegaEffect(audioSession)
        val eq   = createEqualizer(audioSession)
        val bb   = createBassBoost(audioSession)
        val virt = createVirtualizer(audioSession)
        val loud = createLoudness(audioSession)
        val dyn  = createDynamics(audioSession)

        activeSessions[audioSession] = SessionEffects(eq, bb, virt, loud, dyn, omega)
        applyProfileToSession(audioSession, activeProfile)

        Log.i(TAG, "Sesión $audioSession activa: Omega=${omega != null} EQ=${eq != null} BB=${bb != null} " +
                   "Virt=${virt != null} Loud=${loud != null} Dyn=${dyn != null}")
    }

    // ── Cierra y libera efectos de una sesión ─────────────────────────────────
    fun closeSession(audioSession: Int) {
        activeSessions.remove(audioSession)?.let { releaseEffects(it) }
        Log.i(TAG, "Sesión $audioSession cerrada")
    }

    // ── Aplica un perfil a todas las sesiones activas ─────────────────────────
    fun applyProfile(profile: IvannaEffectProfile) {
        activeProfile = profile
        activeSessions.keys.forEach { applyProfileToSession(it, profile) }
    }

    /**
     * Ajusta efectos en vivo desde los sliders de la UI SIN cambiar el preset.
     * Opera sobre el perfil activo: suma eqGainDb a las bandas, actualiza
     * virtualizer (width) y compresor (threshold/ratio) con los valores actuales.
     *
     * Llamado desde DSPState.pushToNative() — unico punto de entrada real de
     * cambios de parametro en toda la app. Con esto, mover cualquier slider
     * afecta en tiempo real a Spotify, YouTube y cualquier app con sesion abierta,
     * identico al mecanismo de Wavelet EQ y Poweramp Equalizer.
     */
    fun adjustLiveParams(
        eqGainDb: Float,        // -18..18 dB, offset sobre las bandas del perfil activo
        stereoWidth: Float,     // 0..1.5 → virtualizer strength 0..1000
        compThresholdDb: Float, // dBFS (usualmente -24..0)
        compRatio: Float        // 1.0..20.0
    ) {
        val prof = activeProfile
        val offsetMb = (eqGainDb * 100f).toInt()  // dB → milliBels
        activeSessions.forEach { (sessionId, fx) ->
            runCatching {
                // EQ: offset sobre las bandas del perfil activo
                fx.equalizer?.let { eq ->
                    if (!eq.enabled) return@let
                    val numBands = eq.numberOfBands.toInt()
                    for (band in 0 until numBands) {
                        val baseMb = if (band < prof.eqBands.size) prof.eqBands[band] else 0
                        eq.setBandLevel(
                            band.toShort(),
                            (baseMb + offsetMb).coerceIn(-600, 600).toShort()
                        )
                    }
                }
                // Virtualizer: width 0..1.5 → strength 0..1000
                fx.virtualizer?.let { v ->
                    if (v.strengthSupported)
                        v.setStrength(
                            (stereoWidth / 1.5f * 1000f).toInt().coerceIn(0, 1000).toShort()
                        )
                }
                // Compressor: reemplaza threshold/ratio del perfil activo
                applyDynamicsProfile(
                    fx.dynamics,
                    prof.copy(compThresholdDb = compThresholdDb, compRatio = compRatio)
                )
            }.onFailure { Log.w(TAG, "adjustLiveParams sesion $sessionId: ${it.message}") }
        }
    }

    // ── Cierra todas las sesiones ─────────────────────────────────────────────

    /** Libera todos los AudioEffect activos. Llamar en onTerminate(). */
    fun releaseAll() {
        activeSessions.values.forEach {
            releaseEffects(it)
        }

        activeSessions.clear()

        Log.i(
            TAG,
            "Todas las sesiones liberadas"
        )
    }

    // ─────────────────────────────────────────────────────────────────────────
    private fun applyProfileToSession(sessionId: Int, profile: IvannaEffectProfile) {
        val fx = activeSessions[sessionId] ?: return
        runCatching {
            fx.equalizer?.let { eq ->
                if (eq.enabled) {
                    val numBands = eq.numberOfBands.toInt()
                    for (band in 0 until minOf(numBands, profile.eqBands.size)) {
                        eq.setBandLevel(band.toShort(), profile.eqBands[band].toShort())
                    }
                }
            }
            fx.bassBoost?.let { bb ->
                if (bb.strengthSupported) bb.setStrength(profile.bassStrength)
            }
            fx.virtualizer?.let { v ->
                if (v.strengthSupported) v.setStrength(profile.virtualizerStrength)
            }
            fx.loudness?.setTargetGain(profile.loudnessGainMb)
            applyDynamicsProfile(fx.dynamics, profile)
        }.onFailure { Log.w(TAG, "Error aplicando perfil a sesión $sessionId", it) }
    }

    private fun applyDynamicsProfile(dyn: DynamicsProcessing?, profile: IvannaEffectProfile) {
        if (dyn == null || Build.VERSION.SDK_INT < Build.VERSION_CODES.P) return
        runCatching {
            val ch0 = dyn.getChannelByChannelIndex(0)
            
            // CORRECCIÓN: Navegación jerárquica correcta de la API (Channel -> Mbc -> Band)
            val mbcBand = ch0?.mbc?.getBand(0) ?: return@runCatching
            
            mbcBand.attackTime   = 5f
            mbcBand.releaseTime  = 100f
            mbcBand.ratio        = profile.compRatio
            mbcBand.threshold    = profile.compThresholdDb // CORRECCIÓN: 'threshold', no 'thresholdDb'
            mbcBand.isEnabled    = true
            
            dyn.setChannelTo(0, ch0)
            dyn.setEnabled(true)
        }.onFailure { Log.w(TAG, "Error aplicando Dynamics a la sesión", it) }
    }

    // ─── Creadores con manejo de error (muchos dispositivos no soportan todos) ─

    /**
     * AUDIT FIX: instancia el efecto IVANNA Omega por UUID sobre la sesión.
     * Tipo nulo (UUID 0) + uuid propio es el patrón estándar para efectos
     * custom registrados vía audio_effects.conf/xml (módulo Magisk soundfx).
     * Nombre de clase completo: la interface com.ivanna.omega.audio.effects
     * .AudioEffect ensombrece la clase del SDK en este paquete de imports.
     * Devuelve null —sin ruido— si la librería no está instalada (no-root).
     */
    private fun createOmegaEffect(session: Int): android.media.audiofx.AudioEffect? = runCatching {
        android.media.audiofx.AudioEffect(
            EFFECT_TYPE_NULL_UUID,
            OMEGA_EFFECT_UUID,
            0,          // prioridad: el control de parámetros va por el bus SHM
            session
        ).also { it.enabled = true }
    }.onFailure {
        Log.d(TAG, "Omega effect no disponible en sesión $session (módulo no instalado): ${it.message}")
    }.getOrNull()

    private fun createEqualizer(session: Int): Equalizer? = runCatching {
        Equalizer(Int.MAX_VALUE, session).also { it.enabled = true }
    }.getOrNull()

    private fun createBassBoost(session: Int): BassBoost? = runCatching {
        BassBoost(Int.MAX_VALUE, session).also { it.enabled = true }
    }.getOrNull()

    private fun createVirtualizer(session: Int): Virtualizer? = runCatching {
        Virtualizer(Int.MAX_VALUE, session).also { it.enabled = true }
    }.getOrNull()

    private fun createLoudness(session: Int): LoudnessEnhancer? = runCatching {
        LoudnessEnhancer(session).also { it.enabled = true }
    }.getOrNull()

    private fun createDynamics(session: Int): DynamicsProcessing? {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.P) return null
        return runCatching {
            val config = DynamicsProcessing.Config.Builder(
                DynamicsProcessing.VARIANT_FAVOR_FREQUENCY_RESOLUTION,
                2,    // canales
                false, 0,   // sin preEQ
                true,  1,   // MBC: 1 banda (compresor broadband)
                false, 0,   // sin postEQ
                false        // sin limiter
            ).build()
            DynamicsProcessing(Int.MAX_VALUE, session, config).also { it.enabled = true }
        }.getOrNull()
    }
}
