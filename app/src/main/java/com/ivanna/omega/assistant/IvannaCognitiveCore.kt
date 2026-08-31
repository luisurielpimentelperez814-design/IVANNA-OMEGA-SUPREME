package com.ivanna.omega.assistant

import android.util.Log
import com.ivanna.omega.agent.AgentApi
import com.ivanna.omega.agent.IvannaAgentCore
import com.ivanna.omega.core.IvannaNativeLib
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * IvannaCognitiveCore — capa de razonamiento de IVANNA.
 *
 * Se sitúa entre IvannaLanguageCore (qué quiso decir el usuario)
 * y IvannaAgentCore (qué hace el sistema). Su trabajo:
 *
 *   1. Enriquecer la intención con contexto real del DSP y la escena.
 *   2. Verificar si la intención es segura (no pide algo que dañe
 *      el pipeline, p.ej. subir volumen con clipping activo).
 *   3. Decidir si ejecutar, modificar o rechazar la petición.
 *   4. Producir la decisión con su motivo (para explicabilidad).
 *
 * Nunca toca el hilo de audio. Nunca bloquea. Opera en Dispatchers.IO.
 */
object IvannaCognitiveCore {

    private const val TAG = "IvannaCognitiveCore"

    data class CognitiveDecision(
        val execute: Boolean,
        val commandOverride: String? = null,  // sustituye el comando si es más seguro
        val reason: String,
        val warningForUser: String? = null   // nulo si no hay advertencia
    )

    private val _lastDecision = MutableStateFlow<CognitiveDecision?>(null)
    val lastDecision: StateFlow<CognitiveDecision?> = _lastDecision.asStateFlow()

    /**
     * Razona sobre [intent] en el contexto del sistema real y devuelve
     * la decisión definitiva: ejecutar / modificar / rechazar.
     */
    fun reason(intent: IvannaLanguageCore.ParsedIntent): CognitiveDecision {
        val scene   = IvannaAgentCore.state.value.perception.scene.name
        val rms     = IvannaAgentCore.state.value.perception.rms
        val clips   = runCatching { IvannaNativeLib.nativeGetClipCount() }.getOrDefault(0)
        val thermal = IvannaAgentCore.state.value.health.thermalLoad

        val decision = when (intent.acousticIntent) {

            IvannaLanguageCore.AcousticIntent.VOLUME_UP -> {
                when {
                    clips > 5 -> CognitiveDecision(
                        execute = false,
                        reason = "Hay clipping activo ($clips eventos). Subir más podría distorsionar.",
                        warningForUser = "Hay distorsión activa ahora mismo. No es seguro subir más el volumen."
                    )
                    thermal >= 0.8f -> CognitiveDecision(
                        execute = true,
                        commandOverride = "volume_up_safe",
                        reason = "Temperatura alta: subida limitada al 50% para proteger el SoC.",
                        warningForUser = "El dispositivo está caliente. He subido el volumen solo un poco."
                    )
                    else -> CognitiveDecision(execute = true, reason = "Señal limpia, temperatura normal.")
                }
            }

            IvannaLanguageCore.AcousticIntent.SPATIAL_EXPANSION -> {
                when {
                    thermal >= 0.75f -> CognitiveDecision(
                        execute = true,
                        commandOverride = "spatial_mode_lite",
                        reason = "Temperatura alta: espacialidad reducida al 50% para no saturar el SoC.",
                        warningForUser = "El dispositivo está caliente. He aplicado espacialidad reducida."
                    )
                    scene == "VOICE" -> CognitiveDecision(
                        execute = true,
                        commandOverride = "voice_clarity",
                        reason = "El clasificador detecta voz. Activar VOICE_CLARITY es más apropiado que SPATIAL.",
                        warningForUser = "Detecto voz activa. He priorizado la claridad vocal en vez del espacio."
                    )
                    else -> CognitiveDecision(execute = true, reason = "Música/otra señal. Espacialidad completa.")
                }
            }

            IvannaLanguageCore.AcousticIntent.LISTENING_FATIGUE -> {
                // La fatiga auditiva merece una respuesta empática + reducción real.
                CognitiveDecision(
                    execute = true,
                    reason = "Usuario indicó fatiga. Activar modo gentle + bajar 10% el volumen.",
                    warningForUser = null
                )
            }

            IvannaLanguageCore.AcousticIntent.BASS_BOOST -> {
                when {
                    clips > 3 -> CognitiveDecision(
                        execute = false,
                        reason = "Clipping activo — reforzar graves empeoraría la distorsión.",
                        warningForUser = "Hay distorsión activa. Reforzar los graves la aumentaría. Primero baja un poco el volumen."
                    )
                    else -> CognitiveDecision(execute = true, reason = "Señal dentro de límites.")
                }
            }

            // Intenciones musicales avanzadas — siempre se ejecutan (FASE 4)
            // La lógica real está en IvannaDSPOrchestrator vía IvannaAssistant.
            // CognitiveCore solo verifica que no haya condición de riesgo crítica.
            IvannaLanguageCore.AcousticIntent.MUSICAL_INTENT,
            IvannaLanguageCore.AcousticIntent.SONG_PROFILE_REQUEST -> {
                when {
                    thermal >= 0.85f -> CognitiveDecision(
                        execute = true,
                        commandOverride = "analog_mode",  // preset más ligero térmicamente
                        reason = "Temperatura muy alta: preset analógico en vez del musical solicitado.",
                        warningForUser = "El dispositivo está caliente. He aplicado el perfil analógico, más suave para el SoC."
                    )
                    else -> CognitiveDecision(
                        execute = true,
                        reason = "Intención musical validada — contexto acústico favorable."
                    )
                }
            }

            // Reportes y listas — siempre se ejecutan, no hay riesgo de audio
            IvannaLanguageCore.AcousticIntent.SESSION_REPORT,
            IvannaLanguageCore.AcousticIntent.PROFILE_LIST -> CognitiveDecision(
                execute = true,
                reason = "Solicitud informativa — sin acción sobre el audio."
            )

            IvannaLanguageCore.AcousticIntent.UNKNOWN -> CognitiveDecision(
                execute = false,
                reason = "Intención no reconocida."
            )

            else -> CognitiveDecision(
                execute = true,
                reason = "Intención acústica válida en el contexto actual."
            )
        }

        _lastDecision.value = decision
        Log.d(TAG, "reason[${intent.acousticIntent}]: execute=${decision.execute} — ${decision.reason}")
        return decision
    }

    /** Devuelve una descripción del estado cognitivo actual para la UI. */
    fun statusSummary(): String {
        val s = IvannaAgentCore.state.value
        return buildString {
            append("Escena: ${s.perception.scene.name}")
            if (s.health.clipCount > 0) append(" · ${s.health.clipCount} clips")
            if (s.health.thermalLoad >= 0.6f)
                append(" · Térmico: ${"%.0f".format(s.health.thermalLoad * 100)}%")
        }
    }

    /**
     * Limpia el estado cognitivo de la sesión actual (FASE 13).
     * Llamado por IvannaAssistant.clearMemory() para garantizar que los
     * paneles de inteligencia en la UI también se reseteen junto al
     * ListenerProfile y al LanguageCore history.
     */
    fun clearDecision() {
        _lastDecision.value = null
        IvannaAcousticBrain.clear()
        Log.d(TAG, "Decisión cognitiva limpiada por clearMemory()")
    }

    /**
     * Explica la última decisión usando DecisionHistory + AudioKnowledgeBase (FASE 14).
     * Se invoca cuando IvannaLanguageCore detecta el intent EXPLAIN.
     */
    fun explainLastDecision(): String {
        val recent = IvannaAgentCore.recentDecisions()
        if (recent.isEmpty()) return "Aún no he tomado ninguna decisión en esta sesión."

        val last = recent.last()
        val kbIntent = when (last.action) {
            "voice-focus"    -> IvannaLanguageCore.AcousticIntent.VOICE_CLARITY
            "cinematic"      -> IvannaLanguageCore.AcousticIntent.MOVIE_IMMERSION
            "music-balanced" -> IvannaLanguageCore.AcousticIntent.MUSIC_FULLNESS
            "protect",
            "clip-relief"    -> IvannaLanguageCore.AcousticIntent.GENTLE_MODE
            "latency-relief" -> IvannaLanguageCore.AcousticIntent.OPTIMIZE
            else             -> IvannaLanguageCore.AcousticIntent.UNKNOWN
        }
        val kbNote = IvannaAudioKnowledgeBase.snippetFor(kbIntent)
        return buildString {
            append("Mi última decisión fue «${last.action}» ")
            append("en escena ${last.scene.name}. Razón: ${last.reason}.")
            if (kbNote.isNotBlank()) append(" $kbNote")
        }
    }

    /**
     * Comprueba si hay motivo para una advertencia proactiva de fatiga,
     * independientemente de la intención actual (FASE 13 + 14 + AcousticBrain).
     *
     * Antes solo miraba profile.shouldSuggestGentle (contador de reportes).
     * Ahora delega en IvannaAcousticBrain.fuse(), que además fusiona la
     * duración real de la sesión de escucha — así IVANNA puede detectar
     * fatiga incluso si el usuario nunca la reportó antes, solo por llevar
     * mucho tiempo escuchando. Retorna null si no hay ningún riesgo fusionado.
     */
    fun proactiveFatigueCheck(profile: IvannaListenerProfile): CognitiveDecision? {
        val insight = IvannaAcousticBrain.fuse(profile)
        if (!insight.fatigueRisk) return null
        val rms = IvannaAgentCore.state.value.perception.rms
        if (rms < 0.05f) return null  // sin reproducción activa, no hay riesgo
        val fatigueKb = IvannaAudioKnowledgeBase.snippetFor(
            IvannaLanguageCore.AcousticIntent.LISTENING_FATIGUE
        )
        return CognitiveDecision(
            execute         = true,
            commandOverride = insight.recommendation ?: "gentle_mode",
            reason          = "${insight.explanation} $fatigueKb",
            warningForUser  = insight.explanation
        )
    }
}
