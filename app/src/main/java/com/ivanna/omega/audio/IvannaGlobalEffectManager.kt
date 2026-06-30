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

import android.media.audiofx.BassBoost
import android.media.audiofx.DynamicsProcessing
import android.media.audiofx.Equalizer
import android.media.audiofx.LoudnessEnhancer
import android.media.audiofx.Virtualizer
import android.os.Build
import android.util.Log
import java.util.concurrent.ConcurrentHashMap

data class IvannaEffectProfile(
    // EQ: 10 bandas, valores en milliBels (-1500 a +1500 mB)
    val eqBands: IntArray = intArrayOf(150, 100, 50, 0, -50, 0, 100, 200, 250, 300),
    // BassBoost: 0–1000
    val bassStrength: Short = 500,
    // Virtualizer: 0–1000
    val virtualizerStrength: Short = 400,
    // LoudnessEnhancer: ganancia en mB (0–1000)
    val loudnessGainMb: Int = 0,
    // Compresor (DynamicsProcessing): threshold dBFS, ratio
    val compThresholdDb: Float = -18f,
    val compRatio: Float = 3.5f
) {
    companion object {
        // Perfiles inspirados en los OmegaParameters existentes
        val FLAT = IvannaEffectProfile(
            eqBands = intArrayOf(0,0,0,0,0,0,0,0,0,0),
            bassStrength = 0, virtualizerStrength = 0, loudnessGainMb = 0
        )
        val WARM = IvannaEffectProfile(
            eqBands = intArrayOf(200,150,100,50,0,-50,0,50,100,150),
            bassStrength = 450, virtualizerStrength = 300, loudnessGainMb = 150
        )
        val ROCK_70S = IvannaEffectProfile(
            // Curva clásica para rock de los 70s: sub-bass controlado,
            // mids presentes, presencia 3-5kHz, brillo suave en treble.
            // Ideal para Zeppelin, Pink Floyd, Sabbath, Eagles.
            eqBands = intArrayOf(180, 220, 150, 80, 0, 120, 280, 300, 250, 180),
            bassStrength = 600,
            virtualizerStrength = 500,
            loudnessGainMb = 200,
            compThresholdDb = -14f,
            compRatio = 3f
        )
        val SPATIAL = IvannaEffectProfile(
            eqBands = intArrayOf(100,80,60,0,-80,0,80,160,200,240),
            bassStrength = 300, virtualizerStrength = 800, loudnessGainMb = 0
        )
        val PUNCH = IvannaEffectProfile(
            eqBands = intArrayOf(300,250,200,100,0,0,100,200,300,350),
            bassStrength = 700, virtualizerStrength = 200, loudnessGainMb = 300,
            compThresholdDb = -12f, compRatio = 5f
        )

        // FIX (cableado UI): mapa nombre→perfil para exponer los 5 presets
        // reales en la UI (antes solo se usaban internamente para Anti-Dolby).
        val byName: LinkedHashMap<String, IvannaEffectProfile> = linkedMapOf(
            "Flat" to FLAT,
            "Warm" to WARM,
            "Rock 70s" to ROCK_70S,
            "Spatial" to SPATIAL,
            "Punch" to PUNCH
        )

        /** Nombre del primer preset cuyo perfil coincide por referencia con [profile]. */
        fun nameOf(profile: IvannaEffectProfile): String =
            byName.entries.firstOrNull { it.value === profile }?.key ?: "Warm"
    }
}

class IvannaGlobalEffectManager {

    private val tag = "IvannaNPE.GlobalFX"

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
        val dynamics:          DynamicsProcessing?
    ) {
        fun releaseAll() {
            runCatching { equalizer?.release() }
            runCatching { bassBoost?.release() }
            runCatching { virtualizer?.release() }
            runCatching { loudness?.release() }
            runCatching { dynamics?.release() }
        }
    }

    // ── Abre efectos para una nueva sesión de audio ───────────────────────────
    fun openSession(audioSession: Int, sourcePackage: String?) {
        if (audioSession <= 0) return
        if (activeSessions.containsKey(audioSession)) return

        Log.i(tag, "Abriendo sesión $audioSession (${sourcePackage ?: "desconocido"})")

        val eq   = createEqualizer(audioSession)
        val bb   = createBassBoost(audioSession)
        val virt = createVirtualizer(audioSession)
        val loud = createLoudness(audioSession)
        val dyn  = createDynamics(audioSession)

        activeSessions[audioSession] = SessionEffects(eq, bb, virt, loud, dyn)
        applyProfileToSession(audioSession, activeProfile)

        Log.i(tag, "Sesión $audioSession activa: EQ=${eq != null} BB=${bb != null} " +
                   "Virt=${virt != null} Loud=${loud != null} Dyn=${dyn != null}")
    }

    // ── Cierra y libera efectos de una sesión ─────────────────────────────────
    fun closeSession(audioSession: Int) {
        activeSessions.remove(audioSession)?.releaseAll()
        Log.i(tag, "Sesión $audioSession cerrada")
    }

    // ── Aplica un perfil a todas las sesiones activas ─────────────────────────
    fun applyProfile(profile: IvannaEffectProfile) {
        activeProfile = profile
        activeSessions.keys.forEach { applyProfileToSession(it, profile) }
    }

    // ── Cierra todas las sesiones ─────────────────────────────────────────────
    fun releaseAll() {
        activeSessions.values.forEach { it.releaseAll() }
        activeSessions.clear()
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
        }.onFailure { Log.w(tag, "Error aplicando perfil a sesión $sessionId", it) }
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
        }.onFailure { Log.w(tag, "Error aplicando Dynamics a la sesión", it) }
    }

    // ─── Creadores con manejo de error (muchos dispositivos no soportan todos) ─
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
