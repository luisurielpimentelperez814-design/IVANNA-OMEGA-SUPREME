package com.ivanna.omega.assistant

import android.util.Log
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * IvannaConversationalCore — gestión de contexto conversacional amplio y persistente.
 *
 * Proporciona a IVANNA la memoria de intención que necesita para responder de forma
 * coherente a lo largo de varias órdenes relacionadas. Mantiene:
 *
 *   - Ventana de contexto de 12 turnos (suficiente para una sesión de escucha activa).
 *   - Nombre de la última canción/artista mencionado ("Frankenstein de Edgar Winter").
 *   - Último perfil musical aplicado y sus parámetros — para encadenar ("y hazla más brillante").
 *   - Intención dominante acumulada: cuando el usuario dice "también" o "además",
 *     se combina con la intención previa en vez de sobreescribirla.
 *   - Historial de ajustes de esta sesión para reportes detallados.
 *
 * Stateful pero thread-safe: MutableStateFlow para el estado público, solo
 * accedido desde Dispatchers.IO en IvannaAssistant.
 *
 * No duplica IvannaContextMemory (SharedPreferences entre sesiones): este core
 * vive solo en RAM durante la sesión activa. Al terminar la sesión,
 * IvannaAssistant.clearMemory() limpia ambos.
 */
object IvannaConversationalCore {

    private const val TAG = "IvannaConversationalCore"
    private const val MAX_TURNS = 12

    // ── Turno de conversación ────────────────────────────────────────────────

    data class ConversationTurn(
        val userText: String,
        val detectedIntent: String,           // nombre canónico de la intención
        val appliedProfile: String?,          // perfil DSP aplicado (null si ninguno)
        val ivannaReply: String,
        val timestampMs: Long = System.currentTimeMillis()
    )

    // ── Estado de sesión ─────────────────────────────────────────────────────

    /**
     * Preferencias que el usuario expresa durante la sesión y que IVANNA
     * mantiene activas hasta que se contradicen o la sesión termina.
     * Son efímeras (RAM únicamente, no se persisten en SharedPreferences).
     *
     * Ejemplos reales que generan preferencias temporales:
     *   "no me gustan los bajos muy fuertes" → bassPreference = LOW
     *   "prefiero escuchar suave" → loudnessPreference = SOFT
     *   "que no haya mucha reverberación" → spatialPreference = DRY
     */
    data class TemporalPreferences(
        val bassPreference: BassPreference = BassPreference.NEUTRAL,
        val loudnessPreference: LoudnessPreference = LoudnessPreference.NEUTRAL,
        val spatialPreference: SpatialPreference = SpatialPreference.NEUTRAL,
        val warmthPreference: WarmthPreference = WarmthPreference.NEUTRAL,
        val detailPreference: DetailPreference = DetailPreference.NEUTRAL
    )

    enum class BassPreference    { LOW, NEUTRAL, HIGH }
    enum class LoudnessPreference { SOFT, NEUTRAL, LOUD }
    enum class SpatialPreference  { DRY, NEUTRAL, WIDE }
    enum class WarmthPreference   { BRIGHT, NEUTRAL, WARM }
    enum class DetailPreference   { SMOOTH, NEUTRAL, DETAILED }

    data class SessionContext(
        val turns: List<ConversationTurn> = emptyList(),
        val currentSong: SongContext? = null,
        val lastAppliedPreset: String? = null,
        val lastDSPChanges: List<String> = emptyList(),       // últimos cambios DSP aplicados
        val sessionAdjustments: List<AdjustmentSummary> = emptyList(),
        val accumulatedIntents: List<String> = emptyList(),   // para comandos encadenados
        val temporalPreferences: TemporalPreferences = TemporalPreferences()
    )

    data class SongContext(
        val title: String,
        val artist: String?,
        val genre: String? = null,              // detectado o inferido
        val userGoal: String? = null            // "magistralmente", "épico", etc.
    )

    data class AdjustmentSummary(
        val presetName: String,
        val technicalDetail: String,
        val timestampMs: Long = System.currentTimeMillis()
    )

    private val _context = MutableStateFlow(SessionContext())
    val context: StateFlow<SessionContext> = _context.asStateFlow()

    // ── API pública ──────────────────────────────────────────────────────────

    /**
     * Registra el resultado de un turno completado — llamar después de ejecutar
     * la respuesta, no antes, para que el historial refleje lo que realmente pasó.
     */
    fun recordTurn(
        userText: String,
        intentName: String,
        appliedProfile: String?,
        ivannaReply: String
    ) {
        val turn = ConversationTurn(
            userText = userText,
            detectedIntent = intentName,
            appliedProfile = appliedProfile,
            ivannaReply = ivannaReply
        )
        val ctx = _context.value
        val newTurns = (ctx.turns + turn).takeLast(MAX_TURNS)
        _context.value = ctx.copy(
            turns = newTurns,
            lastAppliedPreset = appliedProfile ?: ctx.lastAppliedPreset
        )
        Log.d(TAG, "Turno registrado: [$intentName] → ${appliedProfile ?: "sin preset"}")
    }

    /**
     * Registra un ajuste aplicado a la sesión (para el reporte "¿qué hiciste?").
     */
    fun recordAdjustment(presetName: String, technicalDetail: String) {
        val adj = AdjustmentSummary(presetName, technicalDetail)
        val ctx = _context.value
        _context.value = ctx.copy(
            sessionAdjustments = ctx.sessionAdjustments + adj,
            lastAppliedPreset = presetName
        )
    }

    /**
     * Registra un cambio DSP específico (e.g. "EQ banda 250Hz +180mB").
     * Mantiene los 8 cambios más recientes.
     */
    fun recordDSPChange(changeDescription: String) {
        val ctx = _context.value
        val updated = (ctx.lastDSPChanges + changeDescription).takeLast(8)
        _context.value = ctx.copy(lastDSPChanges = updated)
        Log.d(TAG, "Cambio DSP registrado: $changeDescription")
    }

    /**
     * Actualiza preferencias temporales del usuario a partir de lenguaje natural.
     * Son activas solo durante la sesión; se pierden en [clear].
     *
     * Ejemplo: "no me gustan los bajos muy fuertes" → BassPreference.LOW
     */
    fun updateTemporalPreferences(rawText: String) {
        val t = rawText.lowercase().trim()
        val current = _context.value.temporalPreferences
        var updated = current

        if (hits(t, "no me gustan los bajos", "menos bajos", "bajos más suaves",
                  "quita bajos", "bajos suaves")) {
            updated = updated.copy(bassPreference = BassPreference.LOW)
        } else if (hits(t, "más bajos", "más graves", "quiero más bajos")) {
            updated = updated.copy(bassPreference = BassPreference.HIGH)
        }

        if (hits(t, "prefiero escuchar suave", "no muy fuerte", "más tranquilo", "escucha suave")) {
            updated = updated.copy(loudnessPreference = LoudnessPreference.SOFT)
        } else if (hits(t, "más fuerte", "más potente", "subir más")) {
            updated = updated.copy(loudnessPreference = LoudnessPreference.LOUD)
        }

        if (hits(t, "no mucha reverberación", "sin reverb", "menos espacial",
                  "más seco", "mas seco", "sin eco", "menos reverberación")) {
            updated = updated.copy(spatialPreference = SpatialPreference.DRY)
        } else if (hits(t, "más espacial", "más ancho", "más reverb", "más envolvente")) {
            updated = updated.copy(spatialPreference = SpatialPreference.WIDE)
        }

        if (hits(t, "muy brillante", "muy agudo", "más cálido", "mas calido", "menos frío")) {
            updated = updated.copy(warmthPreference = WarmthPreference.WARM)
        } else if (hits(t, "más brillante", "más claridad", "más aire en los agudos")) {
            updated = updated.copy(warmthPreference = WarmthPreference.BRIGHT)
        }

        if (hits(t, "quiero más detalle", "más detalles", "separación instrumental")) {
            updated = updated.copy(detailPreference = DetailPreference.DETAILED)
        } else if (hits(t, "más suave", "menos detalle", "más cómodo")) {
            updated = updated.copy(detailPreference = DetailPreference.SMOOTH)
        }

        if (updated != current) {
            _context.value = _context.value.copy(temporalPreferences = updated)
            Log.d(TAG, "Preferencias temporales actualizadas: $updated")
        }
    }

    /**
     * Extrae el contexto de canción del texto si se menciona un artista/canción.
     * Patrones: "pon X de Y", "configura X", "X de Y magistralmente", etc.
     */
    fun extractSongContext(rawText: String, userGoal: String? = null): SongContext? {
        val t = rawText.trim()

        // Patrón: "X de Y" donde X es título e Y es artista
        val dePattern = Regex(
            Regex("(?:pon|toca|reproduce|pon a sonar|configura|ajusta)?\\s*[\"“”]?(.*?)[\"“”]?\\s+de\\s+(.*?)(?:\\s+(?:y|magistralmente|magistral|épico|epico|genial|increíble|increible|como|con|para).*)?$"),
            RegexOption.IGNORE_CASE
        )
        dePattern.find(t)?.let { m ->
            // FIX (build CI): title quedaba String? porque el Elvis con
            // return@let devuelve Unit (no Nothing) y no hay smart-cast.
            // Segundo fallo: takeIf{it...} no resolvia 'it' en el parser
            // del CI Linux. Solucion definitiva: ifBlank{""} devuelve
            // String NO nulo — sin 'it', sin nullable, sin smart-cast.
            val title  = m.groupValues[1].trim().ifBlank { "" }
            val artist = m.groupValues[2].trim().ifBlank { null }
            if (title.length >= 3) {
                val song = SongContext(title = title, artist = artist, userGoal = userGoal)
                val ctx = _context.value
                _context.value = ctx.copy(currentSong = song)
                Log.d(TAG, "Canción detectada: '$title' de '$artist' — objetivo: $userGoal")
                return song
            }
        }

        // Si hay una canción previa en contexto y el usuario dice "esta canción" / "esa canción"
        if (hits(t, "esta canción", "esta cancion", "esa canción", "esa cancion",
                  "la misma", "la que está", "la que esta", "la canción actual")) {
            return _context.value.currentSong
        }

        return null
    }

    /**
     * Determina si el turno actual es un encadenamiento de la orden anterior.
     * Retorna true si el usuario está modificando el estado previo, no iniciando
     * una petición nueva. Esto permite combinar intenciones en vez de sobreescribir.
     */
    fun isChainedIntent(rawText: String): Boolean {
        val t = rawText.lowercase().trim()
        return hits(t,
            "y también", "y tambien", "además", "ademas", "y además", "y ademas",
            "también", "tambien", "más aún", "mas aun", "aparte", "encima",
            "y más", "y mas", "y encima", "pero más", "pero mas", "y hazla",
            "y hazlo", "y ponla", "y ponlo", "y añade", "y anade"
        )
    }

    /**
     * Genera el reporte hablado de todos los ajustes aplicados en la sesión.
     * Responde a "¿qué hiciste con X?" / "muéstrame qué hiciste".
     */
    fun generateSessionReport(songTitle: String? = null): String {
        val ctx = _context.value
        val adjustments = ctx.sessionAdjustments
        if (adjustments.isEmpty()) {
            return "Aún no he aplicado ningún ajuste en esta sesión."
        }

        val songRef = songTitle ?: ctx.currentSong?.let { "${it.title}${it.artist?.let { a -> " de $a" } ?: ""}" }
            ?: "la señal de audio"

        return buildString {
            append("Para $songRef he creado el siguiente perfil:\n")
            adjustments.forEach { adj ->
                append("• ${adj.presetName}: ${adj.technicalDetail}\n")
            }
            if (adjustments.size > 1) {
                append("En total ${adjustments.size} ajustes aplicados en esta sesión.")
            }
        }.trim()
    }

    /**
     * Genera la respuesta para el intent MUSICAL_PROFILE_REPORT:
     * "Muéstrame qué hiciste con Frankenstein."
     * Formato optimizado para TTS (frases cortas, numeradas).
     */
    fun generateVoiceReport(): String {
        val ctx = _context.value
        val adjustments = ctx.sessionAdjustments
        if (adjustments.isEmpty()) return "Todavía no he aplicado ningún perfil en esta sesión."

        val song = ctx.currentSong
        val intro = if (song != null)
            "Aquí está lo que hice para ${song.title}${song.artist?.let { " de $it" } ?: ""}. "
        else
            "Aquí está el perfil que apliqué. "

        return intro + when {
            adjustments.size == 1 -> {
                val a = adjustments.first()
                "Activé el preset ${a.presetName}. ${a.technicalDetail}."
            }
            else -> {
                val steps = adjustments.mapIndexed { i, a ->
                    "${i + 1}. ${a.presetName}: ${a.technicalDetail}"
                }.joinToString(". ")
                "$steps. Total: ${adjustments.size} ajustes."
            }
        }
    }

    /**
     * Devuelve el resumen de contexto actual para enriquecer el prompt
     * de IvannaCognitiveCore — qué se ha hecho recientemente.
     */
    fun contextSummary(): String {
        val ctx = _context.value
        return buildString {
            ctx.currentSong?.let { append("Canción actual: ${it.title}${it.artist?.let { a -> " de $a" } ?: ""}. ") }
            ctx.lastAppliedPreset?.let { append("Último preset: $it. ") }
            if (ctx.turns.isNotEmpty()) {
                append("Historial reciente: ${ctx.turns.takeLast(3).joinToString(" → ") { it.detectedIntent }}.")
            }
        }.trim()
    }

    /** Limpia el estado de sesión al llamar a clearMemory(). */
    fun clear() {
        _context.value = SessionContext()
        Log.d(TAG, "Contexto conversacional limpiado.")
    }

    private fun hits(text: String, vararg needles: String): Boolean =
        needles.any { text.contains(it) }
}
