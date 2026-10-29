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
 *   Virtualizer estéreo, LoudnessEnhancer, DynamicsProcessing (compresor) y
 *   EnvironmentalReverb (sala/RIR aproximada — no convolución con IR medida,
 *   pero mismos parámetros conceptuales: nivel de sala, decay RT60-equivalente,
 *   primeras reflexiones). Para IvannaBridgePlayer (reproductor propio), el
 *   pipeline IVANNA completo sigue activo en toda su profundidad.
 */
package com.ivanna.omega.audio

import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

import android.media.audiofx.BassBoost
import android.media.audiofx.DynamicsProcessing
import android.media.audiofx.Equalizer
import android.media.audiofx.EnvironmentalReverb
import android.media.audiofx.LoudnessEnhancer
import android.media.audiofx.Virtualizer
import android.os.Build
import android.util.Log
import java.util.UUID
import com.ivanna.omega.safmus.SafmusUniversalKernel
import java.util.concurrent.ConcurrentHashMap

@kotlinx.serialization.Serializable
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
    val compRatio: Float = 3.0f,
    // FIX (RIR sin root): EnvironmentalReverb — sala/reverb sin necesidad de
    // root, análogo a los presets de sala del daemon (SET_ROOM_RT60) pero
    // con el efecto estándar de Android en vez de convolución RIR medida.
    // roomLevel: mB, nivel general de la sala (-9000 silencio..0 máximo)
    // decayTimeMs: ms, tiempo de decaimiento RT60-equivalente (100..20000)
    // reflectionsLevel: mB, nivel de primeras reflexiones (-9000..1000)
    val reverbRoomLevelMb: Int = -9000,      // por defecto: sin reverb (seco)
    val reverbDecayTimeMs: Int = 1000,
    val reverbReflectionsLevelMb: Int = -9000
) {
    companion object {
        // ── FLAT — referencia limpia ────────────────────────────────────────────
        val FLAT = IvannaEffectProfile(
            eqBands = intArrayOf(0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
            bassStrength = 0, virtualizerStrength = 0, loudnessGainMb = 0,
            compThresholdDb = -24f, compRatio = 1.5f,
            reverbRoomLevelMb = -9000, reverbDecayTimeMs = 500, reverbReflectionsLevelMb = -9000
        )

        // ── WARM — calidez analógica, vocales en primer plano ──────────────────
        // TUNED v3.3: eliminado el dip en 1kHz que huecaba las vocales.
        // Curva Baxandall cálida con presence sutil y air controlado.
        val WARM = IvannaEffectProfile(
            eqBands = intArrayOf(180, 160, 120, 80, 40, 0, 0, 30, 70, 90),
            bassStrength = 420, virtualizerStrength = 280, loudnessGainMb = 160,
            compThresholdDb = -16f, compRatio = 2.8f,
            reverbRoomLevelMb = -2600, reverbDecayTimeMs = 900, reverbReflectionsLevelMb = -3200
        )

        // ── ROCK 70s — cuerpo, punch, presencia de guitarra ───────────────────
        // TUNED v3.3: mids más presentes, 4kHz destacado (ataque de guitarra),
        // treble controlado (no sibilante). Zeppelin, Floyd, Sabbath.
        val ROCK_70S = IvannaEffectProfile(
            eqBands = intArrayOf(160, 200, 160, 100, 40, 60, 180, 240, 170, 100),
            bassStrength = 560, virtualizerStrength = 460, loudnessGainMb = 200,
            compThresholdDb = -14f, compRatio = 3.2f,
            reverbRoomLevelMb = -2000, reverbDecayTimeMs = 1300, reverbReflectionsLevelMb = -2600
        )

        // ── SPATIAL — escenario headphone, imagen stereo magistral ─────────────
        // TUNED v3.3: presencia 2-4kHz boosteada (posicionamiento 3D),
        // sub controlado, virtualizer al máximo musical sin fatiga.
        val SPATIAL = IvannaEffectProfile(
            eqBands = intArrayOf(80, 60, 40, 0, -40, 20, 120, 200, 230, 190),
            bassStrength = 280, virtualizerStrength = 720, loudnessGainMb = 0,
            compThresholdDb = -16f, compRatio = 2.5f,
            reverbRoomLevelMb = -1600, reverbDecayTimeMs = 1800, reverbReflectionsLevelMb = -2000
        )

        // ── PUNCH — EDM / Hip-hop / Trap / Reggaetón ──────────────────────────
        // TUNED v3.3: sub-bass autoridad, mids limpios, presencia controlada.
        // Compresión más agresiva para ese golpe de bajo de EDM.
        val PUNCH = IvannaEffectProfile(
            eqBands = intArrayOf(280, 240, 160, 60, 0, 0, 60, 120, 150, 110),
            bassStrength = 680, virtualizerStrength = 200, loudnessGainMb = 300,
            compThresholdDb = -12f, compRatio = 4.0f,
            reverbRoomLevelMb = -9000, reverbDecayTimeMs = 400, reverbReflectionsLevelMb = -9000
        )

        // ── IVANNA OMEGA — preset firma prodigio magistral ─────────────────────
        // El sonido definitivo de IVANNA: autoridad de bajo, mids cristalinos,
        // presencia que corta, aire que respira. Pop/R&B/Soul/Electrónica/Modern.
        val IVANNA_OMEGA = IvannaEffectProfile(
            eqBands = intArrayOf(200, 160, 100, 40, 0, 0, 80, 160, 200, 160),
            bassStrength = 540, virtualizerStrength = 460, loudnessGainMb = 120,
            compThresholdDb = -14f, compRatio = 3.2f,
            reverbRoomLevelMb = -2200, reverbDecayTimeMs = 1200, reverbReflectionsLevelMb = -2800
        )

        // ── Presets musicales avanzados (IvannaMusicalIntentEngine) ───────────

        /** Épico: grandes dinámicas, graves con autoridad, imagen dramática. */
        val EPIC = IvannaEffectProfile(
            eqBands = intArrayOf(260, 220, 140, 60, -20, 20, 100, 200, 240, 200),
            bassStrength = 620, virtualizerStrength = 680, loudnessGainMb = 180,
            compThresholdDb = -13f, compRatio = 3.6f,
            reverbRoomLevelMb = -1400, reverbDecayTimeMs = 2200, reverbReflectionsLevelMb = -1800
        )

        /** Abbey Road: calidez analógica de estudio vintage, años 60. */
        val ABBEY_ROAD = IvannaEffectProfile(
            eqBands = intArrayOf(200, 240, 180, 120, 60, 20, 40, 80, 60, 20),
            bassStrength = 360, virtualizerStrength = 220, loudnessGainMb = 140,
            compThresholdDb = -18f, compRatio = 2.4f,
            reverbRoomLevelMb = -2400, reverbDecayTimeMs = 1600, reverbReflectionsLevelMb = -2200
        )

        /** Vinilo Premium: roll-off natural en altas, calidez armónica. */
        val VINYL_PREMIUM = IvannaEffectProfile(
            eqBands = intArrayOf(240, 200, 160, 120, 80, 40, 0, -40, -100, -180),
            bassStrength = 400, virtualizerStrength = 180, loudnessGainMb = 100,
            compThresholdDb = -20f, compRatio = 2.0f,
            reverbRoomLevelMb = -9000, reverbDecayTimeMs = 700, reverbReflectionsLevelMb = -9000
        )

        /** Concierto Masivo: escena de estadio, impacto físico. */
        val CONCERT_MASSIVE = IvannaEffectProfile(
            eqBands = intArrayOf(300, 260, 180, 80, -20, 0, 60, 140, 180, 160),
            bassStrength = 700, virtualizerStrength = 900, loudnessGainMb = 240,
            compThresholdDb = -10f, compRatio = 4.5f,
            reverbRoomLevelMb = -1000, reverbDecayTimeMs = 2800, reverbReflectionsLevelMb = -1400
        )

        /** Cinematográfico: voces potentes, graves para impacto de sala. */
        val CINEMATIC = IvannaEffectProfile(
            eqBands = intArrayOf(280, 220, 140, 40, 0, 40, 100, 160, 180, 140),
            bassStrength = 500, virtualizerStrength = 760, loudnessGainMb = 200,
            compThresholdDb = -14f, compRatio = 3.8f,
            reverbRoomLevelMb = -1200, reverbDecayTimeMs = 2400, reverbReflectionsLevelMb = -1600
        )

        /** Analógico: calor de válvulas, armónicos pares, sin frialdad digital. */
        val ANALOG_WARM = IvannaEffectProfile(
            eqBands = intArrayOf(180, 200, 160, 100, 60, 20, 0, -20, -60, -120),
            bassStrength = 380, virtualizerStrength = 160, loudnessGainMb = 120,
            compThresholdDb = -22f, compRatio = 1.8f,
            reverbRoomLevelMb = -9000, reverbDecayTimeMs = 600, reverbReflectionsLevelMb = -9000
        )

        /** Estudio Profesional: referencia casi plana para ingenieros. */
        val STUDIO_PRO = IvannaEffectProfile(
            eqBands = intArrayOf(20, 10, 0, 0, 0, 0, 20, 30, 20, 10),
            bassStrength = 60, virtualizerStrength = 80, loudnessGainMb = 0,
            compThresholdDb = -24f, compRatio = 1.4f,
            reverbRoomLevelMb = -9000, reverbDecayTimeMs = 300, reverbReflectionsLevelMb = -9000
        )

        /** Microdetalle: transitorios preservados, separación máxima. */
        val MICRO_DETAIL = IvannaEffectProfile(
            eqBands = intArrayOf(0, 0, 20, 40, 20, 40, 100, 180, 220, 200),
            bassStrength = 100, virtualizerStrength = 400, loudnessGainMb = 60,
            compThresholdDb = -28f, compRatio = 1.2f,
            reverbRoomLevelMb = -4200, reverbDecayTimeMs = 500, reverbReflectionsLevelMb = -4800
        )

        /**
         * IVANNA X SURROUND — realce de la mezcla estéreo ya renderizada.
         *
         * Pensado para DAC USB-C → AUX → procesador externo con Surround activado.
         * Aporta cuerpo, presencia y expansión estéreo moderada ANTES del Genezy;
         * la reverberación añadida se mantiene apagada para no emborronar transitorios
         * ni apilar una sala artificial encima del procesador externo. DynamicsProcessing
         * mantiene el limitador de seguridad cuando Android lo soporta.
         *
         * Es procesamiento de la señal estéreo disponible: no decodifica los objetos
         * ni recupera canales/metadatos Atmos que ya no estén presentes en esa señal.
         */
        val ATMOS_SURROUND_COMPANION = IvannaEffectProfile(
            eqBands = intArrayOf(120, 100, 60, 20, -10, 20, 80, 130, 110, 70),
            bassStrength = 460,
            virtualizerStrength = 420,
            loudnessGainMb = 40,
            compThresholdDb = -18f,
            compRatio = 2.2f,
            reverbRoomLevelMb = -9000,
            reverbDecayTimeMs = 600,
            reverbReflectionsLevelMb = -9000
        )

        // mapa nombre → perfil para la UI (LazyRow de FilterChip + comandos de voz)
        val byName: Map<String, IvannaEffectProfile> = linkedMapOf(
            "Flat"              to FLAT,
            "Warm"              to WARM,
            "Rock 70s"          to ROCK_70S,
            "Spatial"           to SPATIAL,
            "Punch"             to PUNCH,
            "IVANNA OMEGA"      to IVANNA_OMEGA,
            // Presets musicales avanzados
            "ÉPICO"             to EPIC,
            "ABBEY ROAD"        to ABBEY_ROAD,
            "VINILO PREMIUM"    to VINYL_PREMIUM,
            "CONCIERTO MASIVO"  to CONCERT_MASSIVE,
            "CINEMATOGRÁFICO"   to CINEMATIC,
            "ANALÓGICO"         to ANALOG_WARM,
            "ESTUDIO PRO"       to STUDIO_PRO,
            "MICRODETALLE"      to MICRO_DETAIL,
            "ATMOS / GENEZY"     to ATMOS_SURROUND_COMPANION
        )
    }
}

class IvannaGlobalEffectManager(
    private val context: android.content.Context
) {


    private val TAG = "IvannaNPE.GlobalFX"

    companion object {
        // Gain staging (ver EffectTuningPolicy): BassBoost stock empuja picos de bombo sobre
        // 0 dBFS; Virtualizer stock fuerte da voces huecas/robotizadas.
        private const val BASS_STRENGTH_CAP: Short = 300
        private const val VIRT_STRENGTH_CAP: Short = 220
        private const val LOUDNESS_CAP_WITH_LIMITER_MB = 150

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
    @Volatile var activeProfile: IvannaEffectProfile = validateProfileWithSafmus(
        EffectProfileStore.load(context) ?: IvannaEffectProfile.IVANNA_OMEGA)
        private set
    @Volatile private var tuning = EffectTuningPolicy.State()
        private set

    val isVideoStreamingActive: Boolean
        get() = activeSessions.values.any { it.isVideo }

    private data class SessionEffects(
        val equalizer:         Equalizer?,
        val bassBoost:         BassBoost?,
        val virtualizer:       Virtualizer?,
        val loudness:          LoudnessEnhancer?,
        val dynamics:          DynamicsProcessing?,
        // FIX (RIR sin root): EnvironmentalReverb por sesión — mismo patrón
        // que los demás efectos stock, sin root.
        val reverb:            EnvironmentalReverb? = null,
        // AUDIT FIX: instancia del efecto Omega custom (libomega_effect.so).
        // Solo no-null cuando el módulo Magisk está instalado y AudioFlinger
        // tiene la librería registrada en soundfx; en no-root el constructor
        // lanza y se queda en null (fallback silencioso a los efectos stock).
        // Se referencia con nombre completo porque com.ivanna.omega.audio.effects
        // .AudioEffect (interface propia) ensombrece a android.media.audiofx
        // .AudioEffect en el resolver de Kotlin -> error package-private.
        val omega:             android.media.audiofx.AudioEffect? = null,
        val isVideo:           Boolean = false
    )


    private fun releaseEffects(
        fx: SessionEffects
    ) {
        runCatching { fx.equalizer?.release() }
        runCatching { fx.bassBoost?.release() }
        runCatching { fx.virtualizer?.release() }
        runCatching { fx.loudness?.release() }
        runCatching { fx.dynamics?.release() }
        runCatching { fx.reverb?.release() }
        runCatching { fx.omega?.release() }
    }
    
    // ── ISO 226 Calibración ──────────────────────────────────────────────────

    /**
     * Base profile, excluding independently composed ISO and transient controls.
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
    @Synchronized
    fun applyIso226Compensation(gainsDb: FloatArray) {
        val next = EffectTuningPolicy.iso(gainsDb)
        if (next == tuning.isoMb) return
        tuning = tuning.copy(isoMb = next)
        reapplySessions(spatialOnly = true)
    }

    val activeSessionCount: Int get() = activeSessions.size
    /** Sesiones acopladas al efecto nativo omega: ahi los efectos stock van en bypass (ver openSession). */
    val omegaSessionCount: Int get() = activeSessions.values.count { it.omega != null }
    /** Sesiones donde un perfil (EQ/bajos/virtualizador/loudness/dinamica) SI actua via efectos stock. */
    val stockEffectSessionCount: Int
        get() = activeSessions.values.count {
            it.equalizer != null || it.bassBoost != null || it.virtualizer != null ||
                it.loudness != null || it.dynamics != null
        }

    fun isVideoStreamingPackage(pkg: String?): Boolean {
        if (pkg == null) {
            return CinematicEngineHost.activeModeOrdinal == 1 || activeProfile == IvannaEffectProfile.CINEMATIC
        }
        val p = pkg.lowercase(Locale.ROOT)
        return p.contains("amazon") || p.contains("prime") || p.contains("aiv") ||
               p.contains("avod") || p.contains("firebat") || p.contains("netflix") ||
               p.contains("disney") || p.contains("hbo") || p.contains("wbd") ||
               p.contains("max") || p.contains("youtube") || p.contains("twitch") ||
               p.contains("plex") || p.contains("vlc") || p.contains("videoplayer") ||
               p.contains("kodi") || p.contains("movie") || p.contains("video") ||
               p.contains("cinema") || p.contains("player") || p.contains("film")
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

        val isVideo = isVideoStreamingPackage(sourcePackage)
        ContentProfileEngine.noteSource(sourcePackage, isVideo)
        Log.i(TAG, "Abriendo sesión $audioSession (${sourcePackage ?: "desconocido"}, isVideo=$isVideo)")

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

        // FIX CINEMA / AMAZON PRIME VIDEO: Si el motor nativo omega_effect está activo,
        // él procesa todo en C++20 con latencia 0.00 ms. NO montar efectos stock encima
        // (evita doble compresión, desfase y eco). Si estamos en no-root y es una app de
        // video/cine, se desactivan el Virtualizer (elimina desfase de diálogo) y el
        // EnvironmentalReverb (elimina eco slapback de 900 ms).
        val eq: Equalizer?
        val bb: BassBoost?
        val virt: Virtualizer?
        val loud: LoudnessEnhancer?
        val dyn: DynamicsProcessing?
        val rev: EnvironmentalReverb?

        if (omega != null) {
            Log.i(TAG, "Sesión $audioSession acoplada a libomega_effect nativo (efectos stock en bypass)")
            eq = null; bb = null; virt = null; loud = null; dyn = null; rev = null
        } else {
            // FIX (distorsion al subir volumen): Android encadena los efectos de una
            // sesion en ORDEN DE CREACION. DynamicsProcessing se creaba primero, asi
            // que el compresor/limitador corria ANTES de EQ+BassBoost+Virtualizer+
            // Loudness y todo el realce posterior pegaba directo al mixer sin red.
            // Ahora se crea al final: es la ultima etapa y su limiter protege la salida.
            eq   = createEqualizer(audioSession)
            bb   = if (isVideo) null else createBassBoost(audioSession)
            virt = if (isVideo) null else createVirtualizer(audioSession)
            loud = createLoudness(audioSession)
            rev  = if (isVideo) null else createEnvironmentalReverb(audioSession)
            dyn  = createDynamics(audioSession, isVideo)
        }

        activeSessions[audioSession] = SessionEffects(eq, bb, virt, loud, dyn, rev, omega, isVideo)
        applyProfileToSession(audioSession, activeProfile)

        Log.i(TAG, "Sesión $audioSession activa: Omega=${omega != null} EQ=${eq != null} BB=${bb != null} " +
                   "Virt=${virt != null} Loud=${loud != null} Dyn=${dyn != null} Rev=${rev != null} isVideo=$isVideo")
    }

    // ── Cierra y libera efectos de una sesión ─────────────────────────────────
    fun closeSession(audioSession: Int) {
        activeSessions.remove(audioSession)?.let { releaseEffects(it) }
        // El origen (video/streaming) del perfil por contenido debe seguir a las
        // sesiones vivas: al cerrar la última de video ya no se debe seguir en MOVIE.
        if (!isVideoStreamingActive) ContentProfileEngine.noteSource(null, false)
        Log.i(TAG, "Sesión $audioSession cerrada")
    }

    // ── Aplica un perfil a todas las sesiones activas ─────────────────────────
    /**
     * SAFMUS valida los límites antes de enviar el perfil a los AudioEffects existentes.
     * No crea una ruta DSP nueva: normaliza la propuesta y conserva el dueño actual de la sesión.
     */
    private fun safmusBoundedInt(value: Int, min: Int, max: Int, fallback: Int = min): Int =
        SafmusUniversalKernel.sanitizeControlValue(value.toFloat(), min.toFloat(), max.toFloat())
            ?.toInt() ?: fallback

    private fun validateProfileWithSafmus(profile: IvannaEffectProfile): IvannaEffectProfile {
        val eq = IntArray(10) { i ->
            safmusBoundedInt(profile.eqBands.getOrElse(i) { 0 }, -1500, 1500, 0)
        }
        val threshold = SafmusUniversalKernel.sanitizeControlValue(
            profile.compThresholdDb, -60f, 0f
        ) ?: -18f
        val ratio = SafmusUniversalKernel.sanitizeControlValue(
            profile.compRatio, 1f, 20f
        ) ?: 2.2f
        return profile.copy(
            eqBands = eq,
            bassStrength = safmusBoundedInt(profile.bassStrength.toInt(), 0, 1000, 420).toShort(),
            virtualizerStrength = safmusBoundedInt(profile.virtualizerStrength.toInt(), 0, 1000, 380).toShort(),
            loudnessGainMb = safmusBoundedInt(profile.loudnessGainMb, 0, 1000, 80),
            compThresholdDb = threshold,
            compRatio = ratio,
            reverbRoomLevelMb = safmusBoundedInt(profile.reverbRoomLevelMb, -9000, 0, -9000),
            reverbDecayTimeMs = safmusBoundedInt(profile.reverbDecayTimeMs, 100, 20_000, 1000),
            reverbReflectionsLevelMb = safmusBoundedInt(profile.reverbReflectionsLevelMb, -9000, 1000, -9000)
        )
    }

    @Synchronized
    fun applyProfile(profile: IvannaEffectProfile) {
        activeProfile = validateProfileWithSafmus(profile)
        // Selecting a preset clears transient controls, preserving independent ISO compensation.
        tuning = tuning.copy(live = null, safSigma = null, trebleMb = 0, bassStrength = null, loudnessReliefMb = 0)
        runCatching { EffectProfileStore.save(context, activeProfile) }
            .onFailure { Log.w(TAG, "No se pudo guardar el perfil", it) }
        reapplySessions()
    }

    @Synchronized
    fun adjustLiveParams(eqGainDb: Float, stereoWidth: Float, compThresholdDb: Float, compRatio: Float) {
        val next = EffectTuningPolicy.live(eqGainDb, stereoWidth, compThresholdDb, compRatio)
        if (next == tuning.live) return
        tuning = tuning.copy(live = next)
        reapplySessions()
    }

    /** Called on output changes as well as parameter changes; stock virtualization obeys line-out. */
    @Synchronized
    fun refreshOutputRoute() = reapplySessions(spatialOnly = true)

    private fun reapplySessions(spatialOnly: Boolean = false) {
        activeSessions.keys.forEach { applyProfileToSession(it, activeProfile, spatialOnly) }
    }

    // ── Ajustes puntuales de voz sin cambiar de perfil completo ────────────────
    /**
     * FIX (bug real reportado por el flanco de integración cruzada, ver
     * AGENT_CLAIMS.md): "bass_boost"/"treble_reduce"/"auto_optimize" caían
     * al else de VoiceController ("comando desconocido") mientras
     * IvannaDSPOrchestrator.executeCommand() confirmaba applied=true
     * hardcodeado, sin leer ningún resultado real — IVANNA le decía al
     * usuario "graves potenciados" sin que el audio cambiara una sola vez.
     *
     * BassBoost.setStrength(): el efecto ya se creaba por sesión
     * (createBassBoost) pero su intensidad nunca se ajustaba desde ningún
     * punto del código — otro bug del mismo patrón, encontrado en esta
     * misma auditoría (fx.bassBoost confirmado sin ningún .setStrength()
     * real en todo el archivo antes de este fix).
     */
    @Synchronized
    fun boostBass(strength: Short = 700) {
        tuning = tuning.copy(bassStrength = strength.toInt().coerceIn(0, BASS_STRENGTH_CAP.toInt()))
        reapplySessions()
    }

    /**
     * Alivio real de clipping antes de reforzar graves: reduce la ganancia
     * del LoudnessEnhancer ya abierto en cada sesión (mismo mecanismo real
     * usado en el resto de esta clase — toca el AudioEffect en vivo, no
     * calcula nada nuevo). Se llama desde VoiceController "clip_relief",
     * usado por el flujo "bass_boost_safe" de IvannaCognitiveCore cuando
     * ya hay clipping detectado (ver AgentState.health.clipCountDelta).
     *
     * @param reductionMb cuánto bajar el targetGain del LoudnessEnhancer,
     *                     en mB (100 mB = 1 dB). 300 mB (~3 dB) es
     *                     suficiente margen para absorber el boost de
     *                     graves sin percibirse como un "bajón" de volumen.
     */
    @Synchronized
    fun relieveClipping(reductionMb: Int = 300) {
        tuning = tuning.copy(loudnessReliefMb =
            (tuning.loudnessReliefMb + reductionMb.coerceIn(0, 1000)).coerceAtMost(1000))
        reapplySessions()
    }

    /** Android no tiene un efecto "Treble" dedicado equivalente a BassBoost
     *  — se usa el Equalizer real ya abierto, con offset negativo en las
     *  últimas 2 bandas (agudas) del perfil activo, sin tocar el resto. */
    @Synchronized
    fun reduceTreble(offsetDb: Float = -6f) {
        val cut = if (offsetDb.isFinite()) (offsetDb.coerceIn(-18f, 0f) * 100f).toInt() else 0
        tuning = tuning.copy(trebleMb = cut)
        reapplySessions()
    }

    /** Reset transient adjustments without baking them into the base profile. */
    @Synchronized
    fun autoOptimize() {
        tuning = tuning.copy(live = null, safSigma = null, trebleMb = 0, bassStrength = null, loudnessReliefMb = 0)
        reapplySessions()
    }

    /** Absolute SAF overlay: repeated polling cannot accumulate gain or undo the user's controls. */
    @Synchronized
    fun applySafState(diagnostics: FloatArray) {
        val sigma = diagnostics.getOrNull(3)?.takeIf { it.isFinite() }?.coerceIn(0f, 1f) ?: return
        if (sigma == tuning.safSigma) return
        tuning = tuning.copy(safSigma = sigma)
        reapplySessions(spatialOnly = true)
    }

    // ── Cierra todas las sesiones ─────────────────────────────────────────────

    /** Libera todos los AudioEffect activos. Llamar en onTerminate(). */
    fun releaseAll() {
        activeSessions.values.forEach {
            releaseEffects(it)
        }

        activeSessions.clear()
        ContentProfileEngine.noteSource(null, false)

        Log.i(
            TAG,
            "Todas las sesiones liberadas"
        )
    }

    // ─────────────────────────────────────────────────────────────────────────
    @Synchronized
    private fun applyProfileToSession(sessionId: Int, profile: IvannaEffectProfile, spatialOnly: Boolean = false) {
        val fx = activeSessions[sessionId] ?: return
        val state = tuning
        val route = AudioRouteManager.detectOutputRoute()
        val lineOut = runCatching {
            ExternalStereoPolicy.usesExternalSpeakers(route,
                com.ivanna.omega.ui.SpatialAudioPrefs.load(context).externalStereoLineOut)
        }.getOrDefault(false)
        val virtualAllowed = !fx.isVideo && !lineOut && route != OutputRoute.SPEAKER && route != OutputRoute.UNKNOWN
        // Each effect is independent: an unsupported EQ band must not skip the final limiter.
        fun apply(name: String, action: () -> Unit) {
            runCatching(action).onFailure { Log.w(TAG, "$name en sesión $sessionId: ${it.message}") }
        }
        apply("EQ") {
            fx.equalizer?.let { eq ->
                if (!eq.enabled) return@let
                val centers = IntArray(eq.numberOfBands.toInt()) { eq.getCenterFreq(it.toShort()) }
                val range = eq.bandLevelRange
                val curve = EffectTuningPolicy.curve(profile.eqBands, state, fx.isVideo)
                val targets = EffectTuningPolicy.deviceBands(curve, centers,
                    range[0].toInt(), range[1].toInt())
                targets.forEachIndexed { i, gain -> eq.setBandLevel(i.toShort(), gain.toShort()) }
            }
        }
        apply("Virtualizer") {
            fx.virtualizer?.let {
                it.enabled = virtualAllowed
                if (it.strengthSupported) it.setStrength(EffectTuningPolicy.virtualizer(
                    profile.virtualizerStrength.toInt(), state, virtualAllowed, VIRT_STRENGTH_CAP.toInt()))
            }
        }
        if (!spatialOnly) {
            apply("BassBoost") {
                fx.bassBoost?.let { if (it.strengthSupported) it.setStrength(
                    EffectTuningPolicy.bass(profile.bassStrength.toInt(), state, BASS_STRENGTH_CAP.toInt())) }
            }
            apply("Reverb") {
                fx.reverb?.let {
                    // Existing stock reverb stays disabled; native/physical room stages own reverberation.
                    it.enabled = false
                    it.setRoomLevel(profile.reverbRoomLevelMb.toShort())
                    it.setDecayTime(profile.reverbDecayTimeMs)
                    it.setReflectionsLevel(profile.reverbReflectionsLevelMb.toShort())
                }
            }
            apply("Loudness") {
                fx.loudness?.setTargetGain(EffectTuningPolicy.loudness(profile.loudnessGainMb, state,
                    fx.dynamics != null, LOUDNESS_CAP_WITH_LIMITER_MB))
            }
            val live = state.live
            applyDynamicsProfile(fx.dynamics, if (live == null) profile else profile.copy(
                compThresholdDb = live.thresholdDb, compRatio = live.ratio))
        }
    }

    private fun applyDynamicsProfile(dyn: DynamicsProcessing?, profile: IvannaEffectProfile) {
        if (dyn == null || Build.VERSION.SDK_INT < Build.VERSION_CODES.P) return
        runCatching {
            // FIX (pumping + imagen estereo desbalanceada): antes solo se configuraba
            // el canal 0 (el derecho pasaba sin compresion), con rodilla dura 0 dB,
            // ataque 5 ms (modula los graves = bombeo) y sin limitador. Ahora:
            // ambos canales con la misma curva, rodilla suave, ataque/release lentos
            // y un limiter de seguridad a -1.5 dBFS como ultima etapa de la sesion.
            val band = DynamicsProcessing.MbcBand(
                true,                                         // enabled
                20000f,                                       // cutoff: banda unica full-range
                12f,                                          // attack ms
                160f,                                         // release ms
                profile.compRatio.coerceIn(1f, 6f),           // ratio
                profile.compThresholdDb.coerceIn(-40f, 0f),   // threshold dBFS
                8f,                                           // knee dB (suave)
                -90f,                                         // noise gate off
                1f,                                           // expander off
                0f,                                           // preGain
                0f                                            // postGain: sin make-up
            )
            dyn.setMbcBandAllChannelsTo(0, band)
            val limiter = DynamicsProcessing.Limiter(
                true, true, 0,
                1f,       // attack ms
                60f,      // release ms
                20f,      // ratio ~ brickwall
                -1.5f,    // threshold dBFS
                0f        // postGain
            )
            dyn.setLimiterAllChannelsTo(limiter)
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
        // FIX (build 2026-08-10): el constructor AudioEffect(UUID, UUID, Int, Int)
        // existe en el framework en runtime pero es package-private en los SDK
        // stubs de compileSdk 35 → "Cannot access '<init>': it is package-private".
        // Patrón estándar de la industria para efectos custom (ViPER4Android,
        // JamesDSP): invocarlo por reflection sobre la clase real del runtime.
        val ctor = android.media.audiofx.AudioEffect::class.java.getConstructor(
            java.util.UUID::class.java,
            java.util.UUID::class.java,
            Int::class.javaPrimitiveType!!,
            Int::class.javaPrimitiveType!!
        )
        ctor.newInstance(
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

    // FIX (RIR sin root): EnvironmentalReverb es el equivalente estándar de
    // Android a una respuesta de sala — sin convolución con IR medida real
    // (eso vive solo en el motor nativo del daemon, root-only), pero con los
    // mismos parámetros conceptuales: nivel de sala, decaimiento RT60,
    // primeras reflexiones. Disponible sin root desde API 9.
    private fun createEnvironmentalReverb(session: Int): EnvironmentalReverb? = runCatching {
        // En reposo por defecto para evitar eco tipo canyon en películas / llamadas.
        EnvironmentalReverb(Int.MAX_VALUE, session).also { it.enabled = false }
    }.getOrNull()

    private fun createLoudness(session: Int): LoudnessEnhancer? = runCatching {
        LoudnessEnhancer(session).also { it.enabled = true }
    }.getOrNull()

    private fun createDynamics(session: Int, isVideo: Boolean = false): DynamicsProcessing? {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.P) return null
        return runCatching {
            val variant = if (isVideo) {
                DynamicsProcessing.VARIANT_FAVOR_TIME_RESOLUTION
            } else {
                DynamicsProcessing.VARIANT_FAVOR_FREQUENCY_RESOLUTION
            }
            val frameDuration = if (isVideo) 0.5f else 2.5f
            val config = DynamicsProcessing.Config.Builder(
                variant,
                2,    // canales
                false, 0,   // sin preEQ
                true,  1,   // MBC: 1 banda (compresor broadband)
                false, 0,   // sin postEQ
                true         // limiter de seguridad (ultima etapa de la sesion)
            ).setPreferredFrameDuration(frameDuration).build()
            DynamicsProcessing(Int.MAX_VALUE, session, config).also { it.enabled = true }
        }.getOrNull()
    }
}
