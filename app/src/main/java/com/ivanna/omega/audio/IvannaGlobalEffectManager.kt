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
