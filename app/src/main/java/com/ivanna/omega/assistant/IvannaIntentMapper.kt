package com.ivanna.omega.assistant

import android.content.Context
import com.ivanna.omega.VoiceController
import com.ivanna.omega.agent.AgentApi
import org.json.JSONObject

/**
 * IvannaIntentMapper — traductor de lenguaje natural → agente/acción.
 *
 * NO es un parser genérico: es la capa semántica especializada en audio.
 * Recibe el texto reconocido por el ASR y produce UN plan ejecutable sobre
 * los canales YA existentes (IvannaAgentCore / AgentApi / VoiceController),
 * nunca sobre el hilo de audio ni sobre parámetros DSP crudos.
 *
 * Diseño:
 *   - Intent = intención clasificada + agente destino + respuesta hablada.
 *   - Cada intención conoce su respuesta natural (la que IVANNA dirá por TTS),
 *     así la conversación es coherente con lo que realmente se ejecutó.
 *   - Lo que no se entiende cae en UNKNOWN con una respuesta honesta — nunca
 *     finge haber hecho algo.
 */
object IvannaIntentMapper {

    enum class AgentTarget { NONE, DECISION, DSP_CONTROL, HEALTH, OPTIMIZATION, EXPLAINABILITY }

    data class Intent(
        val target: AgentTarget,
        val command: String,          // comando canónico interno
        val spokenResponse: String    // lo que IVANNA responde por voz
    )

    /** Traduce texto hablado (ya en minúsculas) a una intención ejecutable. */
    fun map(rawText: String): Intent {
        val t = rawText.lowercase().trim()
        if (t.isEmpty()) return Intent(AgentTarget.NONE, "none", "No te escuché bien. ¿Puedes repetirlo?")

        // ── Explicabilidad: "¿por qué hiciste esto?" / "¿qué cambiaste?" ────
        if (matchesAny(t, "por qué", "por que", "porque cambiaste", "qué hiciste",
                       "que hiciste", "qué cambiaste", "que cambiaste", "explícame",
                       "explicame", "qué pasó", "que pasó")) {
            return Intent(AgentTarget.EXPLAINABILITY, "explain",
                "Te cuento la última decisión que tomé.")
        }

        // ── Claridad vocal / diálogo ─────────────────────────────────────────
        if (matchesAny(t, "mejora las voces", "mejorar las voces", "voces más claras",
                       "voces mas claras", "mejora el diálogo", "mejorar el diálogo",
                       "claridad vocal", "que se escuchen las voces", "escuchar mejor las voces",
                       "diálogo", "dialogo", "voces")) {
            return Intent(AgentTarget.DECISION, "flat_mode",
                "He ajustado la claridad vocal para que el diálogo quede al frente.")
        }

        // ── Espacio / amplitud ───────────────────────────────────────────────
        if (matchesAny(t, "más espacio", "mas espacio", "más amplio", "mas amplio",
                       "más abierto", "mas abierto", "abre el sonido", "más espacial",
                       "mas espacial", "modo espacial", "surround", "más ancho", "mas ancho")) {
            return Intent(AgentTarget.DSP_CONTROL, "spatial_mode",
                "He ampliado el escenario espacial para dar más aire a la escena.")
        }

        // ── Música / cine / concierto / plano ────────────────────────────────
        if (matchesAny(t, "modo música", "modo musica", "para música", "para musica", "escuchar música")) {
            return Intent(AgentTarget.DSP_CONTROL, "music_mode",
                "He activado el perfil de música, con más cuerpo y calidez.")
        }
        if (matchesAny(t, "modo cine", "modo película", "modo pelicula", "para películas", "para peliculas", "modo cine")) {
            return Intent(AgentTarget.DSP_CONTROL, "cinema_mode",
                "He activado el modo cine, con escena amplia y voces centradas.")
        }
        if (matchesAny(t, "modo concierto", "como en vivo", "sala de conciertos", "concierto")) {
            return Intent(AgentTarget.DSP_CONTROL, "concert_mode",
                "He activado el modo concierto, con la sensación de una sala en vivo.")
        }
        if (matchesAny(t, "modo plano", "sonido neutro", "sin efectos", "plano", "neutral")) {
            return Intent(AgentTarget.DSP_CONTROL, "flat_mode",
                "He dejado el sonido neutro, sin coloración.")
        }

        // ── Volumen ──────────────────────────────────────────────────────────
        if (matchesAny(t, "sube el volumen", "subir volumen", "más alto", "mas alto",
                       "más fuerte", "mas fuerte", "sube volumen", "subele")) {
            return Intent(AgentTarget.DSP_CONTROL, "volume_up",
                "He subido el volumen un poco.")
        }
        if (matchesAny(t, "baja el volumen", "bajar volumen", "más bajo", "mas bajo",
                       "más suave", "mas suave", "baja volumen", "bajale")) {
            return Intent(AgentTarget.DSP_CONTROL, "volume_down",
                "He bajado el volumen un poco.")
        }

        // ── Batería / rendimiento / salud ────────────────────────────────────
        if (matchesAny(t, "optimiza batería", "optimizar batería", "ahorra batería",
                       "ahorrar batería", "ahorra energía", "menos consumo",
                       "optimiza el rendimiento", "mejora el rendimiento", "que no gaste")) {
            return Intent(AgentTarget.OPTIMIZATION, "optimize",
                "Voy a revisar el estado del sistema y a optimizar el consumo.")
        }

        // ── Estado / diagnóstico ─────────────────────────────────────────────
        if (matchesAny(t, "cómo estás", "como estas", "estado del sistema",
                       "cómo va el audio", "como va el audio", "diagnóstico", "diagnostico",
                       "todo bien", "hay algún problema", "hay algun problema")) {
            return Intent(AgentTarget.HEALTH, "diagnose",
                "Déjame revisar el estado del sistema acústico.")
        }

        // ── Desconocida ──────────────────────────────────────────────────────
        return Intent(AgentTarget.NONE, "unknown",
            "Eso aún no lo sé hacer. Puedo mejorar las voces, dar más espacio, ajustar el volumen u optimizar el sistema.")
    }

    /**
     * Ejecuta la intención sobre los canales existentes y devuelve la frase
     * final que IVANNA debe decir. Nunca lanza; ante cualquier fallo devuelve
     * una respuesta honesta.
     */
    fun execute(context: Context, intent: Intent, voiceController: VoiceController?): String {
        return runCatching {
            when (intent.target) {
                AgentTarget.EXPLAINABILITY -> {
                    val j = AgentApi.explainLastDecision()
                    if (j.optBoolean("ok")) {
                        "Lo último que hice fue ${j.optString("action")} porque ${j.optString("reason")}."
                    } else {
                        "Todavía no he tomado ninguna decisión en esta sesión."
                    }
                }
                AgentTarget.OPTIMIZATION -> {
                    val j = AgentApi.requestOptimization()
                    if (j.optString("action") == "none") {
                        "El sistema ya está sano; no hizo falta corregir nada."
                    } else if (j.optBoolean("ok")) {
                        "He aplicado una optimización: ${j.optString("reason")}."
                    } else {
                        "No pude aplicar la optimización ahora mismo."
                    }
                }
                AgentTarget.HEALTH -> {
                    val j = AgentApi.diagnose()
                    if (j.optBoolean("healthy")) {
                        "Todo va bien: el motor está estable y sin problemas."
                    } else {
                        val issues = j.optJSONArray("issues")
                        if (issues != null && issues.length() > 0) {
                            "Detecté esto: ${issues.getString(0)}."
                        } else "No pude leer el diagnóstico completo."
                    }
                }
                AgentTarget.DSP_CONTROL, AgentTarget.DECISION -> {
                    if (voiceController != null) {
                        voiceController.executeCommand(intent.command)
                        intent.spokenResponse
                    } else {
                        "Entendí la orden, pero el controlador de voz no está disponible."
                    }
                }
                AgentTarget.NONE -> intent.spokenResponse
            }
        }.getOrElse {
            "Algo falló al ejecutar esa orden."
        }
    }

    private fun matchesAny(text: String, vararg needles: String): Boolean =
        needles.any { text.contains(it) }
}
