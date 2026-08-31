package com.ivanna.omega.assistant

import android.util.Log

/**
 * IvannaLanguageCore — capa semántica de IVANNA.
 *
 * Convierte lenguaje humano → intención acústica estructurada.
 * Opera en local, sin LLM externo. Especializada en audio.
 *
 * Por encima de IvannaIntentMapper (que es stateless):
 *  - Mantiene contexto de la conversación (últimas N intenciones).
 *  - Usa IvannaAudioKnowledgeBase para enriquecer la respuesta hablada.
 *  - Desambigua cuando el input es ambiguo con el contexto previo.
 *  - Reconoce expresiones de estado emocional y las mapea a perfiles DSP.
 */
object IvannaLanguageCore {

    private const val TAG = "IvannaLanguageCore"
    private const val CONTEXT_WINDOW = 12  // ventana ampliada para sesiones largas

    // Historial de intenciones de esta sesión (context window)
    private val intentHistory = ArrayDeque<ParsedIntent>(CONTEXT_WINDOW + 1)

    data class ParsedIntent(
        val raw: String,
        val acousticIntent: AcousticIntent,
        val confidence: Float,       // 0..1 — qué tan seguro está el parser
        val contextUsed: Boolean,    // si se usó el historial para desambiguar
        val knowledgeSnippet: String // frase explicativa del KB (vacía si no aplica)
    )

    enum class AcousticIntent {
        // Claridad
        VOICE_CLARITY,        // "mejora las voces", "claridad vocal"
        DIALOG_ENHANCEMENT,   // "no entiendo los diálogos", "actores se escuchan mal"
        // Inmersión
        MOVIE_IMMERSION,      // "más cine", "modo película", "inmersión"
        MUSIC_FULLNESS,       // "modo música", "más cuerpo", "más grave"
        CONCERT_LIVE,         // "como en concierto", "sala en vivo"
        SPATIAL_EXPANSION,    // "más espacio", "más amplio", "surround"
        // Fatiga / bienestar
        LISTENING_FATIGUE,    // "estoy cansado", "me duele la cabeza", "bajar energía"
        GENTLE_MODE,          // "suaviza el sonido", "menos agresivo"
        // Técnica
        FLAT_NEUTRAL,         // "sonido plano", "neutro", "sin efectos"
        VOLUME_UP,
        VOLUME_DOWN,
        BASS_BOOST,           // "más bajos", "más graves"
        TREBLE_REDUCE,        // "menos agudos", "muy brillante"
        // Intención musical avanzada (FASE 4)
        MUSICAL_INTENT,       // "épico", "Abbey Road", "vinilo premium", "magistralmente"
        SONG_PROFILE_REQUEST, // "pon Frankenstein de Edgar Winter y configúralo magistralmente"
        SESSION_REPORT,       // "muéstrame qué hiciste", "¿qué cambiaste en esta canción?"
        PROFILE_LIST,         // "¿qué perfiles puedes hacer?", "muéstrame los presets"
        // Sistema
        DIAGNOSE,             // "¿cómo estás?", "estado del sistema"
        EXPLAIN,              // "¿por qué?", "¿qué cambiaste?"
        OPTIMIZE,             // "optimiza", "ahorra batería"
        // Desconocida
        UNKNOWN
    }

    /** Punto de entrada principal. Mantiene contexto entre llamadas. */
    fun parse(rawText: String, scene: String = "UNKNOWN"): ParsedIntent {
        val t = rawText.lowercase().trim()
        val (intent, conf) = classify(t, scene)
        val kb = if (intent != AcousticIntent.UNKNOWN)
            IvannaAudioKnowledgeBase.snippetFor(intent) else ""

        val parsed = ParsedIntent(
            raw = rawText,
            acousticIntent = intent,
            confidence = conf,
            contextUsed = wasContextUsed(intent),
            knowledgeSnippet = kb
        )
        intentHistory.addLast(parsed)
        if (intentHistory.size > CONTEXT_WINDOW) intentHistory.removeFirst()
        Log.d(TAG, "parse: \"$t\" → $intent (conf=${"%.2f".format(conf)})")
        return parsed
    }

    /** Convierte ParsedIntent → comando canónico que VoiceController entiende. */
    fun toCommand(intent: AcousticIntent): String = when (intent) {
        AcousticIntent.VOICE_CLARITY,
        AcousticIntent.DIALOG_ENHANCEMENT -> "voice_clarity"
        AcousticIntent.MOVIE_IMMERSION    -> "cinema_mode"
        AcousticIntent.MUSIC_FULLNESS     -> "music_mode"
        AcousticIntent.CONCERT_LIVE       -> "concert_mode"
        AcousticIntent.SPATIAL_EXPANSION  -> "spatial_mode"
        AcousticIntent.LISTENING_FATIGUE,
        AcousticIntent.GENTLE_MODE        -> "gentle_mode"
        AcousticIntent.FLAT_NEUTRAL       -> "flat_mode"
        AcousticIntent.VOLUME_UP          -> "volume_up"
        AcousticIntent.VOLUME_DOWN        -> "volume_down"
        AcousticIntent.BASS_BOOST         -> "bass_boost"
        AcousticIntent.TREBLE_REDUCE      -> "treble_reduce"
        AcousticIntent.MUSICAL_INTENT     -> "musical_intent"
        AcousticIntent.SONG_PROFILE_REQUEST -> "song_profile"
        AcousticIntent.SESSION_REPORT     -> "session_report"
        AcousticIntent.PROFILE_LIST       -> "profile_list"
        AcousticIntent.DIAGNOSE           -> "diagnose"
        AcousticIntent.EXPLAIN            -> "explain"
        AcousticIntent.OPTIMIZE           -> "optimize"
        AcousticIntent.UNKNOWN            -> "none"
    }

    /** Genera la respuesta hablada final, enriquecida con knowledge si procede. */
    fun spokenResponse(parsed: ParsedIntent): String {
        val base = baseResponse(parsed.acousticIntent)
        return if (parsed.knowledgeSnippet.isNotBlank() && parsed.confidence < 0.95f)
            "$base ${parsed.knowledgeSnippet}"
        else base
    }

    // ── Clasificador ─────────────────────────────────────────────────────────

    private fun classify(t: String, scene: String): Pair<AcousticIntent, Float> {
        // Expresiones emocionales / de estado → perfiles acústicos
        if (hits(t, "estoy cansado", "me cansé", "mucho tiempo escuchando",
                  "me duele la cabeza", "me duele el oído", "fatiga", "cansancio")) {
            return AcousticIntent.LISTENING_FATIGUE to 0.92f
        }

        // Diálogo / voces
        if (hits(t, "no entiendo", "no se entiende", "diálogo", "dialogo",
                  "actores", "personajes", "hablan muy bajo", "no se escuchan",
                  "que dijo", "qué dijo")) {
            return AcousticIntent.DIALOG_ENHANCEMENT to 0.94f
        }
        if (hits(t, "mejora las voces", "voces más claras", "voces mas claras",
                  "claridad vocal", "mejorar voces", "voces al frente")) {
            return AcousticIntent.VOICE_CLARITY to 0.97f
        }

        // Inmersión / modos
        if (hits(t, "modo cine", "modo película", "modo pelicula", "para películas",
                  "para peliculas", "más cine", "mas cine", "inmersión", "inmersion")) {
            return AcousticIntent.MOVIE_IMMERSION to 0.96f
        }
        if (hits(t, "modo música", "modo musica", "para música", "para musica",
                  "escuchar música", "más cuerpo", "mas cuerpo")) {
            return AcousticIntent.MUSIC_FULLNESS to 0.95f
        }
        if (hits(t, "concierto", "en vivo", "sala de conciertos", "como en un teatro")) {
            return AcousticIntent.CONCERT_LIVE to 0.93f
        }
        if (hits(t, "más espacio", "mas espacio", "más amplio", "mas amplio",
                  "abre el sonido", "surround", "más ancho", "mas ancho",
                  "espacial", "3d", "binaural")) {
            return AcousticIntent.SPATIAL_EXPANSION to 0.95f
        }

        // Suavidad
        if (hits(t, "suaviza", "menos agresivo", "más suave", "mas suave",
                  "gentle", "descansa el oído", "relaja")) {
            return AcousticIntent.GENTLE_MODE to 0.91f
        }

        // Plano / neutro
        if (hits(t, "plano", "neutro", "neutral", "sin efectos", "normal",
                  "desactiva todo", "quita los efectos")) {
            return AcousticIntent.FLAT_NEUTRAL to 0.97f
        }

        // Volumen
        if (hits(t, "sube", "subir", "más alto", "mas alto", "más fuerte",
                  "mas fuerte", "louder")) {
            return AcousticIntent.VOLUME_UP to 0.96f
        }
        if (hits(t, "baja", "bajar", "más bajo", "mas bajo", "más suave",
                  "mas suave", "quieter")) {
            return AcousticIntent.VOLUME_DOWN to 0.96f
        }

        // Graves / agudos
        if (hits(t, "más bajos", "mas bajos", "más graves", "mas graves",
                  "refuerza los bajos", "bass")) {
            return AcousticIntent.BASS_BOOST to 0.94f
        }
        if (hits(t, "menos agudos", "mas agudos menos", "muy brillante",
                  "treble", "demasiado brillante", "corta los agudos")) {
            return AcousticIntent.TREBLE_REDUCE to 0.93f
        }

        // Sistema
        if (hits(t, "optimiza", "ahorra batería", "ahorra bateria",
                  "mejor rendimiento", "menos consumo")) {
            return AcousticIntent.OPTIMIZE to 0.94f
        }
        if (hits(t, "cómo estás", "como estas", "estado del sistema",
                  "todo bien", "hay algún problema", "hay algun problema",
                  "diagnóstico", "diagnostico")) {
            return AcousticIntent.DIAGNOSE to 0.95f
        }
        if (hits(t, "por qué", "por que", "qué cambiaste", "que cambiaste",
                  "explícame", "explicame", "qué hiciste", "que hiciste")) {
            return AcousticIntent.EXPLAIN to 0.97f
        }

        // ── Reporte de sesión: "¿qué hiciste?", "muéstrame el perfil" ──────────
        if (hits(t, "qué hiciste", "que hiciste con", "muéstrame qué", "muestrame que",
                  "qué cambiaste en", "que cambiaste en", "cuéntame qué", "cuentame que",
                  "reporte", "informe", "resumen de lo que", "qué le hiciste",
                  "que le hiciste", "cómo quedó", "como quedo")) {
            return AcousticIntent.SESSION_REPORT to 0.96f
        }

        // ── Lista de perfiles disponibles ────────────────────────────────────
        if (hits(t, "qué perfiles", "que perfiles", "qué puedes hacer",
                  "que puedes hacer", "muéstrame los presets", "muestrame los presets",
                  "qué presets", "que presets", "qué configuraciones", "que configuraciones",
                  "opciones disponibles", "qué estilos", "que estilos")) {
            return AcousticIntent.PROFILE_LIST to 0.97f
        }

        // ── Perfil de canción: "pon X de Y y configúralo magistralmente" ─────
        val songKeywords = listOf("pon ", "toca ", "reproduce ", "configura ", "ajusta ",
            "quiero escuchar ", "ponme ", "pon a sonar ")
        val goalKeywords = listOf("magistralmente", "magistral", "épico", "epico",
            "genialmente", "increíble", "increible", "perfecto", "lo mejor posible",
            "configurarlo bien", "la mejor configuración", "la mejor configuracion")
        val hasSongKeyword = songKeywords.any { t.contains(it) }
        val hasGoalKeyword = goalKeywords.any { t.contains(it) }
        val hasDe = t.contains(" de ")   // "Frankenstein de Edgar Winter"
        if ((hasSongKeyword || hasDe) && hasGoalKeyword) {
            return AcousticIntent.SONG_PROFILE_REQUEST to 0.95f
        }

        // ── Lenguaje musical natural expandido ──────────────────────────────
        // Frases que el usuario no formularía como comandos técnicos pero que
        // expresan claramente una intención acústica.
        if (hits(t, "más alma", "mas alma", "que tenga alma", "más emoción", "mas emocion",
                  "más expresivo", "que emocione", "más feeling", "mas feeling",
                  "que respire", "más aire", "mas aire", "menos amontonado",
                  "más pegada", "mas pegada", "que pegue más", "que golpee",
                  "quiero sentir el escenario", "sentir el escenario", "más escenario",
                  "mas escenario", "que los instrumentos estén separados",
                  "como disco de colección", "disco de coleccion",
                  "que se distingan", "distinguir los instrumentos")) {
            return AcousticIntent.MUSICAL_INTENT to 0.91f
        }

        // ── Intención musical: detectar si IvannaMusicalIntentEngine lo reconoce
        if (IvannaMusicalIntentEngine.detect(t) != null) {
            return AcousticIntent.MUSICAL_INTENT to 0.93f
        }

        // ── Contexto: si el último intent fue spatial y el usuario dice "más",
        // es probable que quiera más espacialidad (encadenamiento implícito). ──
        val last = intentHistory.lastOrNull()?.acousticIntent
        if (hits(t, "más", "mas", "un poco más", "un poco mas") && last != null
            && last != AcousticIntent.UNKNOWN) {
            return last to 0.72f  // baja confianza — inferido por contexto
        }

        return AcousticIntent.UNKNOWN to 0.0f
    }

    private fun wasContextUsed(intent: AcousticIntent): Boolean {
        val last = intentHistory.lastOrNull() ?: return false
        return last.acousticIntent == intent && last.confidence < 0.80f
    }

    private fun hits(text: String, vararg needles: String) =
        needles.any { text.contains(it) }

    private fun baseResponse(intent: AcousticIntent): String = when (intent) {
        AcousticIntent.VOICE_CLARITY       -> "He priorizado las voces para que queden al frente del escenario."
        AcousticIntent.DIALOG_ENHANCEMENT  -> "He realzado los diálogos para que cada palabra quede clara."
        AcousticIntent.MOVIE_IMMERSION     -> "He activado el modo cine, con escena amplia y voces centradas."
        AcousticIntent.MUSIC_FULLNESS      -> "He activado el perfil de música, con más cuerpo y calidez."
        AcousticIntent.CONCERT_LIVE        -> "He activado el modo concierto, como en una sala en vivo."
        AcousticIntent.SPATIAL_EXPANSION   -> "He ampliado el campo espacial para dar más aire a la escena."
        AcousticIntent.LISTENING_FATIGUE   -> "Veo que llevas un rato escuchando. He suavizado el sonido para que descanses el oído."
        AcousticIntent.GENTLE_MODE         -> "He activado el modo suave, con menos énfasis en los extremos."
        AcousticIntent.FLAT_NEUTRAL        -> "He dejado el sonido neutro, sin coloración."
        AcousticIntent.VOLUME_UP           -> "He subido el volumen un poco."
        AcousticIntent.VOLUME_DOWN         -> "He bajado el volumen un poco."
        AcousticIntent.BASS_BOOST          -> "He reforzado los graves."
        AcousticIntent.TREBLE_REDUCE       -> "He reducido los agudos para que suene menos brillante."
        AcousticIntent.MUSICAL_INTENT      -> "Procesando tu intención musical..."
        AcousticIntent.SONG_PROFILE_REQUEST -> "Analizando la canción para crear el perfil perfecto..."
        AcousticIntent.SESSION_REPORT      -> "Aquí está el resumen de lo que hice en esta sesión."
        AcousticIntent.PROFILE_LIST        -> IvannaMusicalIntentEngine.availablePresetsDescription()
        AcousticIntent.DIAGNOSE            -> "Déjame revisar el estado del sistema acústico."
        AcousticIntent.EXPLAIN             -> "Te cuento la última decisión que tomé."
        AcousticIntent.OPTIMIZE            -> "Voy a revisar el sistema y optimizar el consumo."
        AcousticIntent.UNKNOWN             -> "Eso aún no lo sé hacer. Puedo mejorar las voces, dar más espacio, crear perfiles musicales como 'épico' o 'Abbey Road', o ajustar el volumen."
    }

    fun clearHistory() { intentHistory.clear() }
}
