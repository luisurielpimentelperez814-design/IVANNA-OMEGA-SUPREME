package com.ivanna.omega.audio

import android.content.Context
import android.util.Log
import com.ivanna.omega.core.IvannaNativeLib
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.util.Locale

/** Tipo de contenido que gobierna la calibración conjunta de todos los motores. */
enum class ContentKind(val label: String) {
    AUTO("AUTO"),
    MUSIC("MÚSICA"),
    MOVIE("PELÍCULAS"),
    STREAMING("STREAMING"),
    MAGISTRAL("IVANNA MAGISTRAL")
}

/**
 * Perfiles por contenido sobre los controles REALES ya cableados al motor nativo
 * (EQ, presencia, compresor, ancho/intensidad espacial, exciter). No añade rutas
 * nuevas: escribe el mismo AudioState que usan los sliders y empuja los mismos
 * JNI, así UI, estado y DSP quedan sincronizados.
 *
 * Criterio común: la espacialización se reparte (ancho + intensidad) en vez de
 * apilarse con cada motor al máximo, para que los procesos se complementen y no
 * generen peine/eco.
 */
object ContentProfileEngine {
    private const val TAG = "ContentProfile"
    private const val PREFS = "ivanna_content_profile"
    private const val KEY_SELECTED = "selected"
    /** Lecturas YAMNet (~1 s) consecutivas necesarias para cambiar el perfil automático. */
    private const val SWITCH_STREAK = 3

    data class Profile(
        val eqBass: Float, val eqMid: Float, val eqTreble: Float, val eqPresence: Float,
        val compThresholdDb: Float, val compRatio: Float,
        val compAttackMs: Float, val compReleaseMs: Float,
        val spatialWidth: Float, val spatialIntensity: Float,
        val exciter: Float
    )

    private val MUSIC = Profile(
        eqBass = 1.5f, eqMid = 0.0f, eqTreble = 2.0f, eqPresence = 1.5f,
        // Dinámica conservada: umbral bajo, ratio suave, ataque lento (transitorios intactos).
        compThresholdDb = -22f, compRatio = 1.6f, compAttackMs = 25f, compReleaseMs = 220f,
        // Escena de concierto: ancho alto, intensidad alta.
        spatialWidth = 1.5f, spatialIntensity = 0.90f, exciter = 0.35f
    )

    private val MOVIE = Profile(
        eqBass = 2.0f, eqMid = 1.5f, eqTreble = 1.0f, eqPresence = 3.0f,
        // Diálogo estable: ratio medio, ataque rápido.
        compThresholdDb = -20f, compRatio = 3.0f, compAttackMs = 8f, compReleaseMs = 140f,
        // Escena amplia pero con menos intensidad: el centro (diálogo) no se toca.
        spatialWidth = 1.3f, spatialIntensity = 0.70f, exciter = 0.25f
    )

    private val STREAMING = Profile(
        eqBass = 1.5f, eqMid = 1.0f, eqTreble = 1.2f, eqPresence = 2.0f,
        // Mínimo procesamiento dependiente del tiempo: ataque/release cortos y
        // espacialización contenida para no sumar latencia ni desfase con el video.
        compThresholdDb = -18f, compRatio = 2.4f, compAttackMs = 5f, compReleaseMs = 100f,
        spatialWidth = 1.0f, spatialIntensity = 0.45f, exciter = 0.20f
    )

    private val MAGISTRAL = Profile(
        eqBass = 2.2f, eqMid = 0.8f, eqTreble = 1.8f, eqPresence = 1.6f,
        compThresholdDb = -16f, compRatio = 2.4f, compAttackMs = 12f, compReleaseMs = 110f,
        spatialWidth = 1.32f, spatialIntensity = 0.82f, exciter = 0.55f
    )

    private val _selected = MutableStateFlow(ContentKind.AUTO)
    /** Elección del usuario (AUTO o un perfil fijo). */
    val selected: StateFlow<ContentKind> = _selected.asStateFlow()

    private val _active = MutableStateFlow<ContentKind?>(null)
    /** Perfil realmente aplicado ahora (nunca AUTO). */
    val active: StateFlow<ContentKind?> = _active.asStateFlow()

    @Volatile private var videoSource = false
    @Volatile private var streamingSource = false
    private var candidate: ContentKind? = null
    private var streak = 0
    private val lock = Any()

    /** Paquetes de streaming de video conocidos (prefijos exactos, sin subcadenas ambiguas). */
    private val STREAMING_PKGS = listOf(
        "com.netflix.", "com.amazon.avod", "com.disney.", "com.hbo.", "com.wbd.",
        "com.google.android.youtube", "tv.twitch.", "com.apple.atve", "com.hulu.",
        "com.paramount.", "com.peacocktv."
    )

    fun profileFor(kind: ContentKind): Profile = when (kind) {
        ContentKind.MUSIC -> MUSIC
        ContentKind.MOVIE -> MOVIE
        ContentKind.STREAMING -> STREAMING
        ContentKind.MAGISTRAL, ContentKind.AUTO -> MAGISTRAL
    }

    fun load(context: Context) {
        val name = context.applicationContext
            .getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .getString(KEY_SELECTED, ContentKind.AUTO.name)
        _selected.value = runCatching { ContentKind.valueOf(name ?: "AUTO") }
            .getOrDefault(ContentKind.AUTO)
        if (_selected.value != ContentKind.AUTO) apply(_selected.value)
    }

    fun select(context: Context, kind: ContentKind) {
        _selected.value = kind
        context.applicationContext
            .getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .edit().putString(KEY_SELECTED, kind.name).apply()
        synchronized(lock) { candidate = null; streak = 0 }
        if (kind != ContentKind.AUTO) apply(kind)
    }

    /** Llamado al abrir una sesión de audio: fuente de video / streaming o no. */
    fun noteSource(pkg: String?, isVideo: Boolean) {
        videoSource = isVideo
        val p = pkg?.lowercase(Locale.ROOT) ?: ""
        streamingSource = isVideo && STREAMING_PKGS.any { p.startsWith(it) }
    }

    /** Clasificación automática; devuelve el perfil candidato para el estado actual. */
    fun classify(speech: Float, music: Float): ContentKind = when {
        streamingSource -> ContentKind.STREAMING
        videoSource -> ContentKind.MOVIE
        music >= speech && music > 0.2f -> ContentKind.MUSIC
        else -> ContentKind.MAGISTRAL
    }

    /** Entrada de YAMNet (~1 s). Solo actúa en modo AUTO y con histéresis. */
    fun onClassification(valid: Boolean, speech: Float, music: Float) {
        if (_selected.value != ContentKind.AUTO || !valid) return
        val next = classify(speech, music)
        val switchTo: ContentKind? = synchronized(lock) {
            if (next == _active.value) { candidate = null; streak = 0; return@synchronized null }
            if (next == candidate) streak++ else { candidate = next; streak = 1 }
            if (streak >= SWITCH_STREAK) { candidate = null; streak = 0; next } else null
        }
        if (switchTo != null) apply(switchTo)
    }

    private fun apply(kind: ContentKind) {
        val p = profileFor(kind)
        _active.value = if (kind == ContentKind.AUTO) ContentKind.MAGISTRAL else kind
        val cur = AudioStateManager.audioState.value
        AudioStateManager.updateState {
            it.copy(
                eqBass = p.eqBass, eqMid = p.eqMid, eqTreble = p.eqTreble,
                eqPresence = p.eqPresence,
                compressorThreshold = p.compThresholdDb, compressorRatio = p.compRatio,
                compressorAttack = p.compAttackMs, compressorRelease = p.compReleaseMs,
                spatialWidth = p.spatialWidth, spatialIntensity = p.spatialIntensity,
                exciterAmount = p.exciter
            )
        }
        if (!IvannaNativeLib.isLoaded) return
        runCatching { IvannaNativeLib.nativeSetEQParams(p.eqBass, p.eqMid, p.eqTreble, cur.masterGain) }
        runCatching { IvannaNativeLib.nativeSetPresenceDb(p.eqPresence) }
        runCatching {
            IvannaNativeLib.nativeSetCompressorParams(
                p.compThresholdDb, p.compRatio, p.compAttackMs, p.compReleaseMs)
        }
        runCatching { com.ivanna.omega.spatial.IvannaSpatialEngine.setWidth(p.spatialWidth) }
        runCatching { IvannaNativeLib.nativeSetSpatialWidthDirect(p.spatialWidth) }
        runCatching { IvannaNativeLib.nativeSetSpatialWet(p.spatialIntensity) }
        runCatching { IvannaNativeLib.nativeSetHarmonicGain(p.exciter) }
        Log.i(TAG, "Perfil aplicado: ${kind.label}")
    }
}
