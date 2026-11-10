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
import android.media.AudioManager
import android.media.AudioPlaybackConfiguration
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

/**
 * Backoff para reintentar la creación del efecto Omega en una sesión que
 * quedó sin él (Ruta B). Puro y determinista para poder probarlo en host.
 *
 * La creación del efecto falla cuando el módulo/HAL aún no está listo
 * (p. ej. sesión abierta antes de que el HAL lazy arrancara, o módulo
 * instalado después de abrir la app). Sin reintento, esa sesión quedaba en
 * efectos stock para siempre — el bug de la sesión 409 de YouTube Music
 * (DIAGNOSTICO_RUTA_B_2026-11-07). El backoff es exponencial con techo:
 * reintenta agresivo al principio (el HAL suele tardar segundos) y luego
 * cada 30 s de forma indefinida mientras la sesión siga viva, por si el
 * módulo/HAL se vuelve disponible más tarde. Nunca se rinde.
 */
internal object OmegaAttachBackoff {
    /** Delays en ms por número de intento fallido consecutivo (índice = intento). */
    private val DELAYS_MS = longArrayOf(2_000L, 5_000L, 10_000L, 20_000L, 30_000L)
    fun delayMs(attempt: Int): Long =
        DELAYS_MS[attempt.coerceIn(0, DELAYS_MS.lastIndex)]
}

/**
 * Desenrolla la cadena de causas de una excepción hasta la causa raíz y
 * devuelve un mensaje legible. El constructor AudioEffect por reflection
 * envuelve el error real en InvocationTargetException con mensaje null, así
 * que loguear solo `it.message` ocultaba la causa (el diagnóstico del 07
 * solo veía "null"). Puro para poder probarlo en host.
 */
internal fun omegaRootCauseMessage(t: Throwable): String {
    var cur: Throwable? = t
    var depth = 0
    while (cur?.cause != null && cur.cause !== cur && depth < 10) {
        cur = cur.cause
        depth++
    }
    val root = cur ?: t
    val name = root.javaClass.simpleName
    val msg = root.message?.takeIf { it.isNotBlank() } ?: "(sin mensaje)"
    return if (root === t) "$name: $msg" else "${t.javaClass.simpleName} -> $name: $msg"
}

/**
 * Clasificación del fallo al crear el efecto Omega (Ruta B), a partir del
 * código de error que el constructor AudioEffect propaga en el mensaje
 * ("Cannot initialize effect engine for type: ... Error: -N").
 *
 * v2.5.374 — MANEJO DEL -38 (INVALID_OPERATION / ENOSYS): AudioFlinger
 * devuelve -38 cuando EffectChain::addEffect_ll rechaza la inserción
 * (getInsertIndex = -1, típicamente por un efecto INSERT_EXCLUSIVE ya en
 * la cadena, p. ej. Dolby DAP o music_helper). PERO el JNI de AudioEffect
 * no siempre propaga ese status: en el Moto G85 la app ve "Error: -3"
 * (código genérico de initCheck) mientras AudioFlinger registra el -38
 * real en logcat ("AudioFlinger could not create effect ... status: -38").
 * Por eso -3 se clasifica como GENERIC_INIT_3 con un aviso explícito de
 * que la causa real está en el logcat de AudioFlinger, y -38 se clasifica
 * aparte cuando el framework sí lo propaga. El reintento con backoff
 * (OmegaAttachBackoff + watchdog) ya existe; la clasificación solo hace
 * el fallo visible y accionable en el log en vez de un "-3" opaco.
 * Pura para poder probarla en host.
 */
enum class OmegaFailureKind {
    /** -38 INVALID_OPERATION/ENOSYS: inserción rechazada (conflicto exclusivo). */
    INVALID_OPERATION_38,
    /** -22 BAD_VALUE: duplicado o descriptor no encontrado por AudioFlinger. */
    BAD_VALUE_22,
    /** -2 ENOENT: la librería/descriptor no está registrado en el HAL. */
    NOT_FOUND_2,
    /** -3 genérico de initCheck: la causa real está en el logcat de AudioFlinger. */
    GENERIC_INIT_3,
    /** Sin código reconocible en el mensaje. */
    UNKNOWN
}

internal fun classifyOmegaFailure(t: Throwable): OmegaFailureKind {
    val msg = omegaRootCauseMessage(t)
    val code = Regex("Error:\\s*(-?\\d+)").find(msg)
        ?.groupValues?.getOrNull(1)?.toIntOrNull()
    return when (code) {
        -38 -> OmegaFailureKind.INVALID_OPERATION_38
        -22 -> OmegaFailureKind.BAD_VALUE_22
        -2 -> OmegaFailureKind.NOT_FOUND_2
        -3 -> OmegaFailureKind.GENERIC_INIT_3
        else -> OmegaFailureKind.UNKNOWN
    }
}

/** Mensaje accionable por clase de fallo, para el log de createOmegaEffect. */
internal fun omegaFailureHint(kind: OmegaFailureKind): String = when (kind) {
    OmegaFailureKind.INVALID_OPERATION_38 ->
        "INVALID_OPERATION (-38): AudioFlinger rechazó la inserción — hay un efecto INSERT_EXCLUSIVE en la cadena o el efecto ya existe. Verificar con dumpsys media.audio_flinger y logcat 'AudioFlinger could not create'."
    OmegaFailureKind.BAD_VALUE_22 ->
        "BAD_VALUE (-22): AudioFlinger no encontró el efecto o lo rechazó como duplicado — revisar que no haya <apply> global de omega en el XML y que el UUID esté registrado."
    OmegaFailureKind.NOT_FOUND_2 ->
        "NOT_FOUND (-2): el HAL no conoce el UUID — la librería no cargó (revisar vendor_file y que el módulo esté activo)."
    OmegaFailureKind.GENERIC_INIT_3 ->
        "Error -3 genérico de initCheck: la causa real está en el logcat de AudioFlinger ('AudioFlinger could not create effect ... status: -N'); en este dispositivo lo habitual es -38 (conflicto exclusivo)."
    OmegaFailureKind.UNKNOWN ->
        "Causa sin código reconocible; ver logcat de AudioFlinger."
}

// ── v2.5.375 — Descubrimiento de sesiones por reflexión (Ruta B) ─────────────
// CAUSA RAÍZ (verificada en fuente AOSP, no en teoría):
// - AudioPlaybackConfiguration.getSessionId() es @SystemApi/@hide. El método
//   NO se llama getAudioSessionId() (no existe: por eso el código de la 374
//   con cfg.audioSessionId no compilaba en el SDK público).
// - PlaybackActivityMonitor.getActivePlaybackConfigurations(isPrivileged)
//   devuelve las configs REALES solo si el llamante tiene
//   MODIFY_AUDIO_ROUTING; sin ese permiso aplica anonymizedCopy(), que pone
//   sessionId = AudioSystem.AUDIO_SESSION_ALLOCATE (-1), clientUid = -1 y
//   el player proxy en null. Una app normal NUNCA ve el sessionId real por
//   este camino: el escáner de la 374 solo podía ser diagnóstico.
// - El broadcast ACTION_OPEN_AUDIO_EFFECT_CONTROL_SESSION tampoco llega en
//   este ROM (receiver dinámico RECEIVER_EXPORTED + manifest exported=true
//   verificados en código; ni 'Nueva sesion' ni el log de registro aparecen
//   en logcat con TIDAL/YouTube Music). Por eso la Ruta B necesita una vía
//   que NO dependa del broadcast: el sondeo por reflexión (best-effort, con
//   el nombre correcto) + el descubrimiento por daemon root (sesión real).
//   v2.5.380: el attach global a la mezcla de salida (sesión 0) como
//   fallback se ELIMINÓ — nunca procesaba frames y su creación callaba a YTM.
// - La reflexión es runtime-only (sin APIs @hide en tiempo de compilación)
//   y fail-closed: si la hidden-API blacklist bloquea getSessionId(), se
//   registra y el sondeo no abre nada (la vía es el descubrimiento por
//   daemon). Si el ROM no anonimiza o la app llega a
//   ser privilegiada (MODIFY_AUDIO_ROUTING), el sondeo devuelve IDs reales.
/** Solo sessionId > 0 es utilizable: -1 es la copia anonimizada y 0 es AUDIO_SESSION_NONE. */
internal fun isUsableSessionId(sessionId: Int): Boolean = sessionId > 0

/**
 * Decisión pura: ¿hay que (re)acoplar omega a la sesión descubierta por el daemon root?
 *
 * v2.5.379 — Ruta B: la sesión 0 es un callejón sin salida (el efecto se crea
 * pero nunca recibe frames) y el sondeo por reflexión no ve sessionIds reales.
 * El daemon (root) descubre el sessionId real por dumpsys y lo publica en el
 * snapshot (ABI v11) + GET_STATUS. La app acopla omega a ESA sesión.
 *
 *   - discoveredId <= 0: no (sin descubrimiento; fail-closed, sigue sesión 0).
 *   - ya acoplado a discoveredId con omega vivo: no (nada que hacer).
 *   - en cualquier otro caso con discoveredId válido: sí (primera vez o la
 *     sesión descubierta cambió — los IDs son dinámicos: YTM era 297 y luego
 *     14177 — así que hay que re-acoplar al cambiar).
 */
internal fun shouldAttachDiscoveredSession(
    discoveredId: Int,
    attachedDiscoveredId: Int,
    omegaAttachedToDiscovered: Boolean
): Boolean {
    if (!isUsableSessionId(discoveredId)) return false
    if (attachedDiscoveredId == discoveredId && omegaAttachedToDiscovered) return false
    return true
}

/** Extrae los sessionIds utilizables de pares (sessionId, clientUid) reflejados. */
internal fun usableSessionIds(pairs: List<Pair<Int, Int>>): List<Int> =
    pairs.map { it.first }.filter { isUsableSessionId(it) }.distinct()

/**
 * Estado del bucle de retroalimentación por beacon (Ruta B, v2.5.386).
 * `lastFrames` = frames del beacon en el ciclo anterior (-1 = sin línea base
 * aún); `unchangedCycles` = ciclos consecutivos del watchdog (3 s c/u) con
 * el contador congelado mientras la sesión sigue sonando.
 */
internal data class OrphanBeaconState(val lastFrames: Long, val unchangedCycles: Int)

/** Resultado de la decisión de huérfano: recrear o no, y el estado siguiente. */
internal data class OrphanDecision(val recreate: Boolean, val nextState: OrphanBeaconState)

/**
 * Decisión pura: ¿el efecto acoplado a la sesión descubierta está HUÉRFANO?
 *
 * v2.5.386 — Ruta B: el efecto omega SÍ se crea y habilita, pero puede quedar
 * en "Orphan Effect Chains" (existe sin conectarse a ningún hilo de salida):
 * el objeto existe (`omegaAlive` = true) pero procesa 0 frames. El watchdog
 * anterior NUNCA consultaba el beacon, así que el huérfano quedaba para
 * siempre y Ruta B no sonaba. Esta función decide, solo con el beacon, si hay
 * que destruir el efecto y recrearlo en la misma sesión real.
 *
 *   - sin sesión acoplada, o la descubierta ya no es la acoplada: no; resetea.
 *   - la sesión NO está sonando (pausa): no; los frames congelados son lo
 *     esperado. Resetea (la pausa no es orfandad).
 *   - beacon no disponible (frames < 0): no; conserva el estado (fail-closed:
 *     sin lectura no se destruye nada).
 *   - sin línea base aún: no; la toma.
 *   - frames crecen (o el contador se reinició): no; procesando.
 *   - frames congelados: acumula ciclos; recrea al llegar a
 *     `unchangedCyclesToRecreate` (3 ciclos × 3 s = 9 s congelado sonando).
 */
internal fun decideOrphanRecreation(
    attachedSessionId: Int,
    discoveredSessionId: Int,
    sessionActivelyPlaying: Boolean,
    beaconFrames: Long,
    state: OrphanBeaconState,
    unchangedCyclesToRecreate: Int = 3
): OrphanDecision {
    val reset = OrphanBeaconState(-1, 0)
    if (!isUsableSessionId(attachedSessionId)) return OrphanDecision(false, reset)
    if (discoveredSessionId != attachedSessionId) return OrphanDecision(false, reset)
    if (!sessionActivelyPlaying) return OrphanDecision(false, reset)
    if (beaconFrames < 0) return OrphanDecision(false, state)
    if (state.lastFrames < 0) return OrphanDecision(false, OrphanBeaconState(beaconFrames, 0))
    if (beaconFrames > state.lastFrames) return OrphanDecision(false, OrphanBeaconState(beaconFrames, 0))
    if (beaconFrames < state.lastFrames) return OrphanDecision(false, OrphanBeaconState(beaconFrames, 0))
    val unchanged = state.unchangedCycles + 1
    return OrphanDecision(
        recreate = unchanged >= unchangedCyclesToRecreate,
        nextState = OrphanBeaconState(beaconFrames, unchanged)
    )
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

    // ── Reintento de attach Omega (Ruta B) ────────────────────────────────────
    // Sesiones vivas sin efecto omega: intentos fallidos consecutivos y
    // momento del último intento (ms). El watchdog reintenta crear el efecto
    // con backoff (OmegaAttachBackoff) mientras la sesión siga viva.
    // Sin esto, una sesión abierta antes de que el módulo/HAL estuviera listo
    // quedaba en efectos stock para siempre (bug sesión 409, 2026-11-07).
    private val omegaRetryAttempts = ConcurrentHashMap<Int, Int>()
    private val omegaLastAttemptMs = ConcurrentHashMap<Int, Long>()

    // v2.5.379 — Sesión real descubierta por el daemon root (dumpsys).
    // discoveredAttachedSessionId = sesión a la que acoplamos omega por
    // descubrimiento (0 = ninguna); lastDiscoveredGen = gen que actuamos.
    // Los sessionIds son dinámicos (YTM era 297 y luego 14177): al cambiar
    // la sesión descubierta hay que liberar la anterior y re-acoplar.
    @Volatile private var discoveredAttachedSessionId: Int = 0
    @Volatile private var lastDiscoveredGen: Long = 0
    // v2.5.386 — BUCLE DE RETROALIMENTACIÓN POR BEACON (huérfano de Ruta B).
    // lastDiscoveredSessionId = última sesión descubierta que publicó el
    // daemon (0 = ninguna; se actualiza en cada GET_STATUS aunque el attach
    // se ignore por shouldAttach=false). activePlayingSessionIds = sesiones
    // con playerState STARTED en el sondeo por reflexión (vacío cuando el ROM
    // anonimiza las configs: entonces se confía en isMusicActive()).
    // orphanBeaconState = línea base del contador de frames del beacon.
    // lastOrphanRecreateMs = última recreación por huérfano (anti-churn:
    // como mucho 1 recreación cada 30 s aunque el beacon siga congelado).
    @Volatile private var lastDiscoveredSessionId: Int = 0
    @Volatile private var activePlayingSessionIds: Set<Int> = emptySet()
    @Volatile private var orphanBeaconState: OrphanBeaconState = OrphanBeaconState(-1, 0)
    @Volatile private var lastOrphanRecreateMs: Long = 0L
    private val orphanRecreateMinIntervalMs: Long = 30_000L
    // v2.5.380 — throttle 1/30 s para los logs de diagnóstico de acople
    // (tryAttachDiscoveredSession corre cada 3 s desde el watchdog; sin
    // throttle el logcat se llenaría de la misma línea).
    private val attachLogLastMs = mutableMapOf<String, Long>()
    private fun logAttachThrottled(key: String, log: () -> Unit) {
        val now = System.currentTimeMillis()
        val last = attachLogLastMs[key] ?: 0L
        if (now - last < 30_000L) return
        attachLogLastMs[key] = now
        log()
    }

    // v2.5.374 — Última clase de fallo al crear omega (ver classifyOmegaFailure).
    // Diagnóstico visible sin logcat: la UI/telemetría puede leerlo.
    @Volatile var lastOmegaFailureKind: OmegaFailureKind = OmegaFailureKind.UNKNOWN
        private set

    // v2.5.374 — Escáner de sesiones activas (ver scanActivePlaybackSessions).
    private val audioManager: AudioManager? by lazy {
        runCatching {
            context.getSystemService(android.content.Context.AUDIO_SERVICE) as AudioManager
        }.getOrNull()
    }

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
        val isVideo:           Boolean = false,
        val sourcePackage:     String? = null
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
    // v2.5.374 — @Synchronized: openSession ahora también la invoca el
    // escáner de sesiones activas (hilo watchdog) además del broadcast
    // (hilo principal). Sin sincronización, dos llamadas concurrentes para
    // el mismo sessionId podían crear dos juegos de efectos y el segundo
    // put() en activeSessions perdía el primero (leak).
    @Synchronized
    fun openSession(
        audioSession: Int,
        sourcePackage: String?,
        // K1 (auditoría 379): bypassGlobalGuard=true permite acoplar una
        // sesión descubierta ANTES de liberar el global, para no dejar a la
        // app sin ningún omega si el acople falla. Solo lo usa
        // tryAttachDiscoveredSession; el resto de llamadores mantienen el
        // guard anti-doble-procesado.
        bypassGlobalGuard: Boolean = false
    ) {

        writeToLogFile(
            "openSession $audioSession source=$sourcePackage"
        )
        if (audioSession <= 0) return
        if (activeSessions.containsKey(audioSession)) return
        // v2.5.375: si el omega global (sesión 0) ya procesa la mezcla de
        // salida, NO crear efectos por sesión (doble procesado: la sesión
        // pasaría por omega dos veces, una por sesión y otra en el mix).
        // La sesión queda cubierta por el global; si el global muere, el
        // sondeo la abre por sesión en el siguiente ciclo del watchdog.
        if (globalOmega != null && !bypassGlobalGuard) {
            Log.d(TAG, "openSession $audioSession omitida: omega global (sesión 0) ya activo")
            return
        }

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

        activeSessions[audioSession] = SessionEffects(eq, bb, virt, loud, dyn, rev, omega, isVideo, sourcePackage)
        applyProfileToSession(audioSession, activeProfile)
        ensureWatchdog()

        Log.i(TAG, "Sesión $audioSession activa: Omega=${omega != null} EQ=${eq != null} BB=${bb != null} " +
                   "Virt=${virt != null} Loud=${loud != null} Dyn=${dyn != null} Rev=${rev != null} isVideo=$isVideo")
    }

    // ── Recuperación tras muerte de audioserver / HAL ────────────────────────
    // Cuando audioserver (o el HAL de audio) reinicia, TODOS los AudioEffect del proceso quedan
    // como objetos muertos pero activeSessions sigue creyendo que la sesión esta activa, y
    // openSession() sale temprano por containsKey: el audio sigue sin DSP hasta reiniciar la app.
    // Un watchdog sondea cada sesion con setEnabled(true) (devuelve el status real de binder) y,
    // si el efecto murio, lo libera y lo recrea con el mismo origen. Funciona con y sin root
    // (Ruta B si el modulo esta, efectos stock si no).
    private val watchdogStarted = java.util.concurrent.atomic.AtomicBoolean(false)
    private val watchdog: java.util.concurrent.ScheduledExecutorService by lazy {
        java.util.concurrent.Executors.newSingleThreadScheduledExecutor { r ->
            Thread(r, "ivanna-fx-watchdog").apply { isDaemon = true }
        }
    }

    private fun ensureWatchdog() {
        if (watchdogStarted.compareAndSet(false, true)) {
            watchdog.scheduleWithFixedDelay(
                {
                    runCatching { recoverDeadSessions() }.onFailure { Log.w(TAG, "watchdog: ${it.message}") }
                    // Reintento de attach Omega para sesiones vivas sin efecto (Ruta B):
                    // una sesión abierta antes de que el módulo/HAL estuviera listo no
                    // debe quedarse en efectos stock para siempre (bug sesión 409).
                    runCatching { retryOmegaAttachForSessions() }.onFailure { Log.w(TAG, "watchdog omega-retry: ${it.message}") }
                    // v2.5.379 — Sesión REAL descubierta por el daemon root:
                    // tiene prioridad sobre el sondeo por reflexión (el
                    // fallback de sesión 0 se eliminó en v2.5.380: no
                    // procesaba en este G85 y callaba a YTM).
                    runCatching { tryAttachDiscoveredSession() }.onFailure { Log.w(TAG, "watchdog discovered-session: ${it.message}") }
                    // v2.5.374 — Sondeo de sesiones activas: abre las sesiones
                    // que el broadcast no avisó (ver scanActivePlaybackSessions).
                    runCatching { scanActivePlaybackSessions() }.onFailure { Log.w(TAG, "watchdog scan: ${it.message}") }
                    // v2.5.386 — Bucle de retroalimentación por beacon: si el
                    // efecto acoplado a la sesión real lleva ~9 s sin procesar
                    // frames mientras la música suena (huérfano), se destruye
                    // y el próximo ciclo lo re-acopla en la misma sesión.
                    runCatching { checkAttachedEffectOrphan() }.onFailure { Log.w(TAG, "watchdog orphan: ${it.message}") }
                    // v2.5.375 — Vida del omega global (sesión 0): si audioserver
                    // murió, el objeto queda muerto y hay que liberarlo para que
                    // el sondeo reintente (por sesión o global).
                    runCatching { checkGlobalOmegaLiveness() }.onFailure { Log.w(TAG, "watchdog global-liveness: ${it.message}") }
                },
                3, 3, java.util.concurrent.TimeUnit.SECONDS
            )
        }
    }

    /**
     * Arranca el escáner periódico de sesiones activas (idempotente).
     * Llamado desde IVANNAApplication.onCreate para que el sondeo corra
     * siempre que el proceso de la app esté vivo, aunque ninguna sesión se
     * haya abierto aún por broadcast (si no, el watchdog nunca arrancaría
     * y el escáner tampoco — el huevo y la gallina del receiver roto).
     */
    fun startSessionScanner() {
        ensureWatchdog()
        Log.i(TAG, "Session scanner arrancado (sondeo de sesiones activas cada 3 s vía watchdog)")
    }

    // ── v2.5.375 — Omega global en la mezcla de salida (sesión 0) ───────────
    // v2.5.380 — FALLBACK ELIMINADO: el omega global se creaba cuando el
    // sondeo solo veía configs anonimizadas, pero NUNCA procesa frames en
    // este G85 (beacon 0) y su creación dejaba a YTM sin audio (el
    // early-return !enabled && isSilent() de omega_process() salía sin
    // copiar input→output). Ya no se crea ningún omega en sesión 0; la vía
    // es el descubrimiento por daemon (tryAttachDiscoveredSession). Esta
    // función y sus campos quedan sin llamadores (solo releaseGlobalOmega
    // sigue usándose para liberar un global preexistente al acoplar).
    private var globalOmega: android.media.audiofx.AudioEffect? = null
    private var globalOmegaLastAttemptMs: Long = 0L
    private val globalOmegaRetryMs = 30_000L
    // La reflexión @hide puede estar bloqueada por la hidden-API blacklist;
    // si lo está, se registra UNA sola vez (no una línea por config cada 3 s).
    private val reflectionBlockLogged = java.util.concurrent.atomic.AtomicBoolean(false)

    /** true si algún omega está acoplado (por sesión o global). */
    private fun hasAnyOmegaAttached(): Boolean =
        globalOmega != null || activeSessions.values.any { it.omega != null }

    /**
     * v2.5.380 — SIN LLAMADORES (fallback eliminado): intentaba acoplar
     * omega a la mezcla de salida (sesión 0). Se conserva por si un global
     * preexistente debe liberarse (releaseGlobalOmega); NO volver a llamarla
     * para crear el global: su creación calla a YTM y nunca procesa frames.
     */
    @Synchronized
    internal fun ensureGlobalOmega(): Boolean {
        if (globalOmega != null) return true
        val now = System.currentTimeMillis()
        if (now - globalOmegaLastAttemptMs < globalOmegaRetryMs) return false
        globalOmegaLastAttemptMs = now
        val omega = createOmegaEffect(0) ?: run {
            Log.w(TAG, "Omega global (sesión 0) no disponible [${lastOmegaFailureKind}] — " +
                "Ruta B sigue sin procesar; próximo reintento en ${globalOmegaRetryMs / 1000} s")
            return false
        }
        globalOmega = omega
        Log.i(TAG, "Ruta B GLOBAL activa: omega acoplado a la mezcla de salida (sesión 0, fallback 375) — " +
            "todas las apps suenan por IVANNA sin descubrir sessionIds")
        AudioBackendSelector.notifyOmegaAttached()
        return true
    }

    /** Libera el omega global (al morir audioserver o al cerrar la app). */
    @Synchronized
    internal fun releaseGlobalOmega() {
        globalOmega?.let { runCatching { it.release() } }
        globalOmega = null
        Log.i(TAG, "Omega global (sesión 0) liberado")
    }

    /** Sondea la vida del omega global; si audioserver murió, lo libera para que el sondeo reintente. */
    @Synchronized
    internal fun checkGlobalOmegaLiveness() {
        val omega = globalOmega ?: return
        val status = runCatching { omega.setEnabled(true) }.getOrDefault(EffectLiveness.DEAD)
        if (!EffectLiveness.isAlive(status)) {
            Log.w(TAG, "Omega global (sesión 0) muerto tras reinicio de audioserver (status=$status) — liberando para reintento")
            releaseGlobalOmega()
        }
    }

    /**
     * Refleja AudioPlaybackConfiguration.getSessionId() (@SystemApi/@hide).
     * Devuelve el sessionId real si el ROM lo expone, -1 si la copia está
     * anonimizada (lo habitual sin MODIFY_AUDIO_ROUTING) o si la reflexión
     * está bloqueada por la hidden-API blacklist. Nunca lanza.
     */
    private fun reflectSessionId(config: AudioPlaybackConfiguration): Int = runCatching {
        val m = config.javaClass.getMethod("getSessionId")
        (m.invoke(config) as? Int) ?: -1
    }.onFailure {
        if (reflectionBlockLogged.compareAndSet(false, true)) {
            Log.w(TAG, "reflectSessionId bloqueado/no disponible (una sola vez): ${it.message}")
        }
    }.getOrDefault(-1)

    /** Refleja AudioPlaybackConfiguration.getClientUid() (@SystemApi/@hide). -1 si anonimizado o bloqueado. */
    private fun reflectClientUid(config: AudioPlaybackConfiguration): Int = runCatching {
        val m = config.javaClass.getMethod("getClientUid")
        (m.invoke(config) as? Int) ?: -1
    }.onFailure {
        if (reflectionBlockLogged.compareAndSet(false, true)) {
            Log.w(TAG, "reflectClientUid bloqueado/no disponible (una sola vez): ${it.message}")
        }
    }.getOrDefault(-1)

    /**
     * v2.5.386 — ¿está sonando AHORA? Refleja AudioPlaybackConfiguration.getPlayerState().
     * getPlayerState()/PLAYER_STATE_STARTED no resuelven en compilación (API oculta en
     * este SDK), así que se accede por reflexión como getSessionId/getClientUid.
     * PLAYER_STATE_STARTED = 2 (AudioPlaybackConfiguration, API 26+). false si bloqueado.
     */
    private fun reflectIsPlaying(config: AudioPlaybackConfiguration): Boolean = runCatching {
        val m = config.javaClass.getMethod("getPlayerState")
        (m.invoke(config) as? Int) == 2
    }.onFailure {
        if (reflectionBlockLogged.compareAndSet(false, true)) {
            Log.w(TAG, "reflectIsPlaying bloqueado/no disponible (una sola vez): ${it.message}")
        }
    }.getOrDefault(false)

    /** Resuelve el paquete de un UID (best-effort, null si no se puede). */
    private fun resolvePackageForUid(uid: Int): String? {
        if (uid <= 0) return null
        return runCatching {
            context.packageManager.getPackagesForUid(uid)?.firstOrNull()
        }.getOrNull()
    }

    /**
     * Sondea las configuraciones de reproducción activas. Llamado cada 3 s
     * por el watchdog. v2.5.375: ya NO es solo diagnóstico — refleja
     * getSessionId() (nombre correcto AOSP; getAudioSessionId() no existe)
     * sobre cada config y abre las sesiones con ID utilizable. Si todas
     * llegan anonimizadas (-1/0, lo habitual sin MODIFY_AUDIO_ROUTING) y
     * ningún omega está acoplado, cae al attach global a la mezcla de
     * salida (sesión 0). El log deja constancia del valor real observado
     * en el dispositivo, para verificar si este ROM anonimiza o no.
     *
     * LÍMITE HONESTO DE AOSP (PlaybackActivityMonitor, frameworks/base):
     * getActivePlaybackConfigurations() solo devuelve las configuraciones
     * REALES al llamador privilegiado (MODIFY_AUDIO_ROUTING,
     * signature|privileged). Sin él, el sistema devuelve copias
     * ANONIMIZADAS (anonymizedCopy) con el sessionId puesto a
     * AUDIO_SESSION_ALLOCATE (-1). Por eso el sondeo es best-effort y la
     * vía que no necesita el ID es el descubrimiento por daemon root
     * (v2.5.380: el fallback global de sesión 0 se eliminó — callaba a YTM
     * y nunca procesaba frames).
     *
     * @return cuántas sesiones abrió el sondeo en este pase.
     */
    // ── Sesión REAL descubierta por el daemon root (Ruta B, v2.5.379) ───────
    // La sesión 0 es un callejón sin salida en este Moto G85: el efecto se
    // crea pero NUNCA recibe frames (effect_frames=0 medido 2026-11-09). Y el
    // sondeo por reflexión no ve sessionIds reales (anonimizados -1/0 sin
    // MODIFY_AUDIO_ROUTING). El daemon (root) ejecuta `dumpsys
    // media.audio_flinger` cada ~3 s y publica el sessionId de música en el
    // snapshot (ABI v11) + GET_STATUS. Este método lee esa evidencia y acopla
    // omega a la sesión REAL en lugar de a la sesión 0. Corre en el watchdog
    // ANTES de scanActivePlaybackSessions.
    //
    // La carrera que mató el hot fix por broadcast (2026-11-09 22:14-22:16):
    // un omega global (sesión 0) preexistente bloqueaba openSession. Por eso
    // aquí se LIBERA el global ANTES de acoplar a la sesión real, en el mismo
    // ciclo del watchdog (defensivo: en v2.5.380 ya no se crea ningún global).
    //
    //   - Sin sesión descubierta (0): no hace nada; el sondeo por reflexión
    //     sigue como best-effort y no se crea ningún efecto (fail-closed).
    //   - Con sesión descubierta válida: libera el omega global (sesión 0, no
    //     procesa) y la sesión anterior si cambió, y acopla omega a la nueva.
    //   - sourcePackage=null: con omega, los efectos stock van en bypass de
    //     todos modos, así que isVideo apenas importa.
    //   - Fail-closed: si el daemon no responde o el JSON no trae la sesión,
    //     no se toca nada.
    @Synchronized
    internal fun tryAttachDiscoveredSession(): Boolean {
        val evidence = runCatching {
            com.ivanna.omega.magisk.OmegaEngineBridge.requestControlPlaneEvidence()
        }.getOrNull()
        if (evidence == null) {
            // v2.5.380 — el fallo era SILENCIOSO (return false sin log) y por
            // eso el diagnóstico de la 379 fue a ciegas. Throttle 1/30 s.
            logAttachThrottled("no-evidence") {
                Log.w(TAG, "tryAttachDiscoveredSession: GET_STATUS sin respuesta/JSON inválido")
            }
            return false
        }
        val discoveredId = evidence.discoveredSessionId
        val discoveredGen = evidence.discoveredSessionGen
        // v2.5.386 — se guarda SIEMPRE (también cuando el attach se ignora):
        // el bucle de huérfano compara la sesión descubierta actual con la
        // acoplada, y sin este campo no sabría si la descubierta cambió.
        lastDiscoveredSessionId = discoveredId
        // NOTA v2.5.380: NO hay fallback a leer el archivo snapshot
        // (/data/adb/ivanna_omega es solo-root; la app no puede leerlo).
        // La única vía es GET_STATUS.
        val omegaAlive = discoveredId > 0 && activeSessions[discoveredId]?.omega != null
        if (!shouldAttachDiscoveredSession(discoveredId, discoveredAttachedSessionId, omegaAlive)) {
            if (discoveredId > 0) {
                lastDiscoveredGen = discoveredGen
                // v2.5.380 — log visible (throttle 1/30 s): antes este caso
                // era silencioso y no se sabía si el descubrimiento llegaba
                // pero se ignoraba.
                logAttachThrottled("ignored") {
                    Log.i(TAG, "tryAttachDiscoveredSession: sesión descubierta $discoveredId (gen=$discoveredGen) pero shouldAttach=false (ya acoplado=$omegaAlive, attached=$discoveredAttachedSessionId) — nada que hacer")
                }
            } else if (discoveredAttachedSessionId != 0) {
                // K4 (auditoría 379): la música paró (discoveredId=0) pero
                // quedó un omega acoplado a una sesión muerta. Cerrarlo y
                // limpiar el estado. v2.5.380: ya no hay fallback global que
                // recrear (se eliminó porque callaba a YTM y nunca procesaba).
                Log.i(TAG, "Música parada (sin sesión descubierta): cerrando omega de la sesión descubierta anterior $discoveredAttachedSessionId")
                closeSession(discoveredAttachedSessionId)
                discoveredAttachedSessionId = 0
            }
            return false
        }
        // Backoff (auditoría 379): si un intento anterior falló, no reintentar
        // hasta que pase el delay. Evita churn en AudioFlinger cada 3 s.
        val lastAttempt = omegaLastAttemptMs[discoveredId] ?: 0L
        if (lastAttempt > 0L) {
            val attempts = omegaRetryAttempts[discoveredId] ?: 0
            if (System.currentTimeMillis() - lastAttempt < OmegaAttachBackoff.delayMs(attempts)) {
                // v2.5.380 — log visible (throttle 1/30 s): el backoff
                // también era un retorno silencioso.
                logAttachThrottled("backoff") {
                    Log.d(TAG, "tryAttachDiscoveredSession: backoff activo para $discoveredId (intento $attempts) — se reintentará después")
                }
                return false
            }
        }
        Log.i(TAG, "Sesión real descubierta por el daemon: $discoveredId (gen=$discoveredGen) — acoplando omega a la sesión real")
        // La sesión descubierta cambió: liberar la anterior.
        if (discoveredAttachedSessionId != 0 && discoveredAttachedSessionId != discoveredId) {
            Log.i(TAG, "Sesión descubierta cambió: $discoveredAttachedSessionId -> $discoveredId — liberando la anterior")
            closeSession(discoveredAttachedSessionId)
            discoveredAttachedSessionId = 0
        }
        // K1-rev (diagnóstico en vivo 23:01 CST): el omega global (sesión 0)
        // YA está en el hilo de salida (AudioOut_15, "1 effects for session
        // 0") y AudioFlinger rechaza crear otro omega en la sesión real con
        // -38 INVALID_OPERATION (exclusividad por salida: un solo omega por
        // hilo). Acoplar PRIMERO nunca funciona mientras el global ocupe el
        // hilo. Por eso se libera el global ANTES de acoplar. v2.5.380: si
        // el acople falla, el global NO se recrea — el fallback a sesión 0
        // se eliminó porque el global nunca procesa frames en este G85 y su
        // creación dejaba a YTM sin audio. Si el acople falla, se reintenta
        // con backoff y YTM sigue sonando sin DSP (fail-closed).
        if (globalOmega != null) {
            Log.i(TAG, "Liberando omega global (sesión 0) ANTES de acoplar a la sesión real $discoveredId (ocupa el hilo, -38)")
            releaseGlobalOmega()
        }
        val wasOpen = activeSessions.containsKey(discoveredId)
        val attached = if (wasOpen) {
            tryAttachOmega(discoveredId)
        } else {
            openSession(discoveredId, null, bypassGlobalGuard = true)
            activeSessions[discoveredId]?.omega != null
        }
        if (attached) {
            discoveredAttachedSessionId = discoveredId
            lastDiscoveredGen = discoveredGen
            Log.i(TAG, "Ruta B acoplada a sesión real descubierta: $discoveredId (gen=$discoveredGen)")
        } else {
            // Registrar el intento fallido (solo vía openSession; tryAttachOmega
            // ya registra el suyo) para que el próximo ciclo respete el backoff.
            if (!wasOpen) {
                val attempts = (omegaRetryAttempts[discoveredId] ?: 0) + 1
                omegaRetryAttempts[discoveredId] = attempts
                omegaLastAttemptMs[discoveredId] = System.currentTimeMillis()
            }
            Log.w(TAG, "No se pudo acoplar omega a la sesión descubierta $discoveredId (se reintentará con backoff; sin sesión descubierta no se crea efecto)")
        }
        return attached
    }

    /**
     * v2.5.386 — BUCLE DE RETROALIMENTACIÓN POR BEACON (huérfano de Ruta B).
     *
     * El efecto omega puede quedar en "Orphan Effect Chains": creado y
     * habilitado, pero sin conectarse a ningún hilo de salida (0 frames).
     * `setEnabled(true)` devuelve éxito aunque sea huérfano, así que ni
     * `recoverDeadSessions()` ni `shouldAttachDiscoveredSession()` lo
     * detectaban: el huérfano quedaba para siempre y Ruta B no sonaba.
     *
     * Este chequeo corre en cada ciclo del watchdog (3 s), DESPUÉS del
     * escáner (que actualiza `activePlayingSessionIds`). Lee el contador de
     * frames del beacon (DSPBridge.effectBeaconFrames()) y, si la sesión
     * descubierta sigue siendo la acoplada, la música sigue sonando y los
     * frames llevan 3 ciclos congelados (~9 s), destruye el efecto huérfano
     * y limpia el backoff para que el próximo ciclo re-acople INMEDIATO en
     * la misma sesión real. Anti-churn: como mucho 1 recreación cada 30 s.
     * Fail-closed: si el beacon no se puede leer, no se destruye nada.
     *
     * @return true si se recreó el efecto huérfano, false en caso contrario.
     */
    @Synchronized
    internal fun checkAttachedEffectOrphan(): Boolean {
        val attached = discoveredAttachedSessionId
        if (attached <= 0) {
            orphanBeaconState = OrphanBeaconState(-1, 0)
            return false
        }
        val frames = runCatching { com.ivanna.omega.dsp.DSPBridge.effectBeaconFrames() }.getOrDefault(-1L)
        // La música suena si el AudioManager lo dice Y (el sondeo reflejó la
        // sesión acoplada como activa, O el ROM anonimizó las configs — en
        // ese caso el conjunto está vacío y no se puede probar que esté en
        // pausa, así que se confía en isMusicActive()).
        val musicActive = runCatching { audioManager?.isMusicActive }.getOrDefault(false) == true
        val playing = musicActive &&
            (activePlayingSessionIds.isEmpty() || activePlayingSessionIds.contains(attached))
        val decision = decideOrphanRecreation(
            attachedSessionId = attached,
            discoveredSessionId = lastDiscoveredSessionId,
            sessionActivelyPlaying = playing,
            beaconFrames = frames,
            state = orphanBeaconState
        )
        orphanBeaconState = decision.nextState
        if (!decision.recreate) return false
        val now = System.currentTimeMillis()
        if (now - lastOrphanRecreateMs < orphanRecreateMinIntervalMs) return false
        lastOrphanRecreateMs = now
        Log.w(TAG, "Ruta B HUÉRFANA: efecto acoplado a sesión $attached sin procesar frames " +
            "(${orphanBeaconState.unchangedCycles} ciclos × 3 s) mientras la música sigue sonando — " +
            "recreando el efecto en la sesión real")
        closeSession(attached)
        discoveredAttachedSessionId = 0
        orphanBeaconState = OrphanBeaconState(-1, 0)
        // closeSession ya limpió el backoff (omegaRetryAttempts/omegaLastAttemptMs):
        // el attach anterior demostró no funcionar, así que el próximo ciclo
        // de watchdog re-acopla INMEDIATO (sin esperar el delay de reintento).
        return true
    }

    @Synchronized
    internal fun scanActivePlaybackSessions(): Int {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) return 0
        val am = audioManager ?: return 0
        val configs: List<AudioPlaybackConfiguration> =
            runCatching { am.activePlaybackConfigurations }.getOrNull() ?: return 0
        if (configs.isEmpty()) {
            // v2.5.386 — sin configs no hay nada sonando: el conjunto de
            // sesiones en reproducción queda vacío (el bucle de huérfano
            // usa isMusicActive() como respaldo, que aquí es false).
            activePlayingSessionIds = emptySet()
            return 0
        }
        var opened = 0
        var reflected = 0
        var anonymized = 0
        // v2.5.386 — sesiones sonando AHORA (playerState STARTED por reflexión),
        // para el bucle de huérfano: un beacon congelado solo es orfandad si la
        // sesión sigue sonando; en pausa los frames congelados son lo esperado.
        val playing = mutableSetOf<Int>()
        for (config in configs) {
            val sessionId = reflectSessionId(config)
            if (!isUsableSessionId(sessionId)) { anonymized++; continue }
            reflected++
            if (reflectIsPlaying(config)) playing.add(sessionId)
            val uid = reflectClientUid(config)
            val pkg = resolvePackageForUid(uid)
            if (!activeSessions.containsKey(sessionId)) {
                openSession(sessionId, pkg)
                opened++
            }
        }
        activePlayingSessionIds = playing
        Log.i(TAG, "scanActivePlaybackSessions: ${configs.size} configs, $reflected con sessionId real, " +
            "$anonymized anonimizadas (-1/0), $opened sesiones abiertas por sondeo")
        // v2.5.380 — ELIMINADO el fallback a omega global (sesión 0).
        // CAUSA RAÍZ del muteo de YTM (2026-11-10 00:08 CST, reproducible):
        // cuando el sondeo no ve sessionIds reales (anonimizados) y ningún
        // omega está acoplado, el fallback creaba el omega global en la
        // sesión 0 (AudioOut_15, donde suena YTM). Ese efecto NUNCA procesa
        // frames en este G85 (beacon frames=0, magic vacío) y su creación
        // deja a YTM sin audio (sigue "reproduciendo" sin sonido) aunque el
        // efecto ni siquiera se acople a la sesión real. La sesión 0 no
        // procesa nada de todos modos, así que el global no aporta nada y
        // solo rompe el audio. La vía correcta es el descubrimiento por
        // daemon (tryAttachDiscoveredSession); si no hay sesión descubierta,
        // NO se crea ningún efecto (fail-closed: YTM sigue sonando sin DSP
        // en vez de quedarse mudo).
        return opened
    }

    private fun probeStatus(fx: SessionEffects): Int {
        val probe: android.media.audiofx.AudioEffect = fx.omega ?: fx.equalizer ?: fx.bassBoost
            ?: fx.virtualizer ?: fx.loudness ?: fx.dynamics ?: fx.reverb
            ?: return EffectLiveness.SUCCESS   // sin efectos creados: nada que sondear
        return runCatching { probe.setEnabled(true) }.getOrDefault(EffectLiveness.DEAD)
    }

    /** Devuelve cuantas sesiones se recrearon. */
    @Synchronized
    internal fun recoverDeadSessions(): Int {
        var recovered = 0
        for ((sid, fx) in activeSessions.entries.toList()) {
            val status = probeStatus(fx)
            if (EffectLiveness.isAlive(status)) continue
            Log.w(TAG, "Sesion $sid: efecto muerto (status=$status) — recreando tras reinicio de audioserver")
            val pkg = fx.sourcePackage
            closeSession(sid)
            openSession(sid, pkg)
            recovered++
        }
        if (recovered > 0) republishOmegaEq()
        return recovered
    }

    // ── Reintento de attach Omega (Ruta B) ────────────────────────────────────
    /**
     * Intenta crear el efecto Omega en una sesión YA ABIERTA que quedó sin él
     * (efectos stock). Si tiene éxito, libera los efectos stock (para no
     * duplicar procesado: en una sesión con omega los stock van en bypass por
     * diseño, ver openSession) y deja la sesión acoplada al efecto nativo.
     * Si falla, registra la causa real y deja la sesión como estaba (stock),
     * para que el watchdog lo reintente con backoff.
     *
     * @return true si el efecto quedó acoplado (o ya lo estaba), false si no
     *         se pudo crear.
     */
    @Synchronized
    fun tryAttachOmega(sessionId: Int): Boolean {
        val fx = activeSessions[sessionId] ?: return false
        if (fx.omega != null) {
            omegaRetryAttempts.remove(sessionId)
            omegaLastAttemptMs.remove(sessionId)
            return true
        }
        val omega = createOmegaEffect(sessionId) ?: run {
            val attempts = (omegaRetryAttempts[sessionId] ?: 0) + 1
            omegaRetryAttempts[sessionId] = attempts
            omegaLastAttemptMs[sessionId] = System.currentTimeMillis()
            Log.d(TAG, "Omega no disponible en sesión $sessionId (intento $attempts, " +
                "próximo reintento en ${OmegaAttachBackoff.delayMs(attempts)} ms)")
            return false
        }
        // Éxito: liberar los stock (el diseño es omega-solo por sesión) y
        // acoplar el efecto nativo.
        releaseEffects(fx)
        activeSessions[sessionId] = fx.copy(
            equalizer = null, bassBoost = null, virtualizer = null,
            loudness = null, dynamics = null, reverb = null, omega = omega
        )
        omegaRetryAttempts.remove(sessionId)
        omegaLastAttemptMs.remove(sessionId)
        applyProfileToSession(sessionId, activeProfile)
        republishOmegaEq()
        Log.i(TAG, "Sesión $sessionId acoplada a libomega_effect nativo por reintento " +
            "(efectos stock liberados para no duplicar procesado)")
        // Ruta B confirmada: avisar al selector para que re-evalúe el backend
        // y detenga el fallback local sin esperar al ciclo de 10 s.
        AudioBackendSelector.notifyOmegaAttached()
        return true
    }

    /**
     * Pase del watchdog: reintenta crear Omega en cada sesión viva sin efecto,
     * respetando el backoff por sesión. Devuelve cuántas sesiones se acoplaron.
     */
    @Synchronized
    internal fun retryOmegaAttachForSessions(): Int {
        val now = System.currentTimeMillis()
        var attached = 0
        for ((sid, fx) in activeSessions.entries.toList()) {
            if (fx.omega != null) continue
            val attempts = omegaRetryAttempts[sid] ?: 0
            val last = omegaLastAttemptMs[sid] ?: 0L
            if (now - last < OmegaAttachBackoff.delayMs(attempts)) continue
            if (tryAttachOmega(sid)) attached++
        }
        return attached
    }

    // ── Cierra y libera efectos de una sesión ─────────────────────────────────
    fun closeSession(audioSession: Int) {
        activeSessions.remove(audioSession)?.let { releaseEffects(it) }
        omegaRetryAttempts.remove(audioSession)
        omegaLastAttemptMs.remove(audioSession)
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
        republishOmegaEq()
    }

    /**
     * v2.5.198: con el efecto nativo (omega) los efectos stock van en bypass, asi que elegir un perfil o
     * mover ISO/SAF/EQ en vivo no cambiaba nada audible: solo el calibrador ISO escribia SET_EQ_BANDS.
     * Ahora la MISMA curva compuesta que recibe el Equalizer stock se publica al daemon. Si el daemon no
     * esta conectado no se afirma nada (devuelve false y queda en el log); el EQ stock sigue como antes.
     */
    @Synchronized
    fun republishOmegaEq(): Boolean {
        if (!com.ivanna.omega.magisk.OmegaEngineBridge.isConnected) return false
        val curve = EffectTuningPolicy.curve(activeProfile.eqBands, tuning, video = isVideoStreamingActive)
        val gains = EffectTuningPolicy.omegaBandsDb(curve)
        val arr = org.json.JSONArray().apply { gains.forEach { put(it.toDouble()) } }
        val ok = com.ivanna.omega.magisk.OmegaEngineBridge.sendCommand(org.json.JSONObject().apply {
            put("action", "SET_EQ_BANDS"); put("gains", arr); put("timestamp", System.currentTimeMillis())
        })
        Log.i(TAG, "Perfil -> daemon SET_EQ_BANDS ${if (ok) "enviado" else "FALLO"}: ${gains.joinToString { "%.1f".format(it) }}")
        return ok
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
        omegaRetryAttempts.clear()
        omegaLastAttemptMs.clear()
        releaseGlobalOmega()   // v2.5.375: liberar también el omega global (sesión 0)
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
        // La causa real viene envuelta en InvocationTargetException con mensaje
        // null; loguear solo it.message ocultaba el diagnóstico (el 07 solo
        // veía "null"). omegaRootCauseMessage desenrolla hasta la causa raíz.
        // v2.5.374: se clasifica el fallo (ver classifyOmegaFailure) para
        // que un -38 (inserción rechazada) o un -3 genérico queden visibles
        // y accionables en vez de un "-3" opaco. El reintento con backoff lo
        // hace el watchdog (retryOmegaAttachForSessions); aquí solo se
        // registra la causa, sin crashear.
        val kind = classifyOmegaFailure(it)
        lastOmegaFailureKind = kind
        Log.d(TAG, "Omega effect no disponible en sesión $session: ${omegaRootCauseMessage(it)} [$kind] ${omegaFailureHint(kind)} " +
            "(si el módulo está instalado, el HAL de efectos no conoce el UUID — ver docs/RUTA_B_DAEMON_FIX.md)")
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
