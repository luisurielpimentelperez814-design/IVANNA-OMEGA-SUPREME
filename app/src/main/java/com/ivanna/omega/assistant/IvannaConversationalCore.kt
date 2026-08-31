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

    data class SessionContext(
        val turns: List<ConversationTurn> = emptyList(),
        val currentSong: SongContext? = null,
        val lastAppliedPreset: String? = null,
        val sessionAdjustments: List<AdjustmentSummary> = emptyList(),
        val accumulatedIntents: List<String> = emptyList()   // para comandos encadenados
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
     * Extrae el contexto de canción del texto si se menciona un artista/canción.
     * Patrones: "pon X de Y", "configura X", "X de Y magistralmente", etc.
     */
    fun extractSongContext(rawText: String, userGoal: String? = null): SongContext? {
        val t = rawText.trim()

        // Patrón: "X de Y" donde X es título e Y es artista
        val dePattern = Regex(
            """(?:pon|toca|reproduce|pon a sonar|configura|ajusta)?\s*["""]?(.+?)["""]?\s+de\s+(.+?)(?:\s+(?:y|magistralmente|magistral|épico|epico|genial|increíble|increible|como|con|para).*)?$""",
            RegexOption.IGNORE_CASE
        )
        dePattern.find(t)?.let { m ->
            val title  = m.groupValues[1].trim().ifBlank { null } ?: return@let
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
