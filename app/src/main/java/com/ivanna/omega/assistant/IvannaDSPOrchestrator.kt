package com.ivanna.omega.assistant

import android.content.Context
import android.util.Log
import com.ivanna.omega.VoiceController
import com.ivanna.omega.audio.IvannaEffectProfile
import com.ivanna.omega.core.IVANNAApplication
import com.ivanna.omega.ai.gemini.IvannaGeminiAgent
import com.ivanna.omega.assistant.IvannaIntentMapper

/**
 * IvannaDSPOrchestrator — ejecutor de intención musical sobre el motor DSP.
 *
 * Recibe un IvannaMusicalIntentEngine.MusicalPreset y lo aplica de forma
 * coordinada sobre los canales existentes:
 *
 *   1. IvannaGlobalEffectManager.applyProfile()  — perfil EQ/bass/virt/comp
 *   2. VoiceController.executeCommand()           — comando extra (concert_mode, etc.)
 *   3. IvannaConversationalCore.recordAdjustment() — registro para reportes
 *
 * No toca el hilo de audio directamente. No duplica lógica de VoiceController.
 * Solo orquesta los canales existentes en el orden correcto y genera la
 * explicación completa que IVANNA dirá al usuario.
 *
 * Seguridad: todas las llamadas envueltas en runCatching; ante cualquier
 * fallo devuelve una respuesta honesta en vez de silencio o crash.
 */
class IvannaDSPOrchestrator(private val context: Context) {

    private val appContext = context.applicationContext
    private val voiceController = VoiceController(appContext)

    companion object { private const val TAG = "IvannaDSPOrchestrator" }

    data class OrchestrationResult(
        val applied: Boolean,
        val presetName: String,
        val spokenReply: String,       // lo que IVANNA dice por TTS
        val technicalDetail: String    // para el reporte interno
    )

    /**
     * Aplica un MusicalPreset completo sobre el motor DSP.
     * Llamar solo desde Dispatchers.IO.
     */
    fun applyMusicalPreset(
        preset: IvannaMusicalIntentEngine.MusicalPreset,
        songContext: IvannaConversationalCore.SongContext? = null
    ): OrchestrationResult {

        var applied = false

        // 1. Aplicar perfil EQ/bass/virt/comp
        val profileApplied = runCatching {
            val app = appContext as? IVANNAApplication
            if (app != null) {
                app.globalEffectManager.applyProfile(preset.profile)
                Log.i(TAG, "Perfil '${preset.name}' aplicado al motor global")
                true
            } else {
                Log.w(TAG, "IVANNAApplication no disponible — perfil no aplicado")
                false
            }
        }.getOrElse {
            Log.e(TAG, "Error aplicando perfil: ${it.message}")
            false
        }

        // 2. Aplicar comando extra si existe (concert_mode, etc.)
        val extraApplied = preset.extraCommand?.let { cmd ->
            runCatching {
                voiceController.executeCommand(cmd)
                Log.i(TAG, "Comando extra '$cmd' ejecutado")
                true
            }.getOrElse {
                Log.w(TAG, "Comando extra '$cmd' falló: ${it.message}")
                false
            }
        } ?: true

        applied = profileApplied && extraApplied

        // 3. Registrar en el núcleo conversacional
        if (applied) {
            IvannaConversationalCore.recordAdjustment(
                presetName = preset.name,
                technicalDetail = preset.technicalDetail
            )
        }

        // 4. Construir la respuesta hablada
        val songRef = songContext?.let {
            " para ${it.title}${it.artist?.let { a -> " de $a" } ?: ""}"
        } ?: ""

        val spokenReply = if (applied) {
            "${preset.explanation}${ if (songRef.isNotBlank()) " He guardado el perfil$songRef." else "" }"
        } else {
            "Intenté aplicar el perfil ${preset.name}, pero el motor de audio no está disponible ahora mismo."
        }

        return OrchestrationResult(
            applied = applied,
            presetName = preset.name,
            spokenReply = spokenReply,
            technicalDetail = preset.technicalDetail
        )
    }

    /**
     * Aplica un perfil de efecto ya existente por su nombre canónico.
     * Útil para "activa el perfil Spatial" sin necesidad de preset musical.
     */
    fun applyNamedProfile(name: String): OrchestrationResult {
        val profile = IvannaEffectProfile.byName[name]
            ?: return OrchestrationResult(
                applied = false,
                presetName = name,
                spokenReply = "No conozco el perfil '$name'. Los perfiles disponibles son: ${IvannaEffectProfile.byName.keys.joinToString(", ")}.",
                technicalDetail = "perfil no encontrado"
            )

        val applied = runCatching {
            val app = appContext as? IVANNAApplication
            app?.globalEffectManager?.applyProfile(profile)
            app != null
        }.getOrElse { false }

        if (applied) {
            IvannaConversationalCore.recordAdjustment(
                presetName = name,
                technicalDetail = "Perfil nativo $name activado"
            )
        }

        return OrchestrationResult(
            applied = applied,
            presetName = name,
            spokenReply = if (applied) "He activado el perfil $name." else "No pude activar el perfil $name.",
            technicalDetail = "perfil nativo: $name"
        )
    }

    /**
     * Genera un perfil adaptativo para una canción específica basándose en
     * el objetivo del usuario. Combina detección de intención musical con
     * el contexto de canción para producir la mejor configuración.
     *
     * Ejemplo: "Frankenstein de Edgar Winter" + "magistralmente" →
     *   rock 70s épico con concierto y espacialidad moderada.
     */
    fun createSongProfile(
        song: IvannaConversationalCore.SongContext,
        musicalGoal: String
    ): OrchestrationResult {
        // Intentar detectar preset musical desde el objetivo
        val musicalPreset = IvannaMusicalIntentEngine.detect(musicalGoal)

        // Si no hay preset musical específico, usar el perfil de firma de IVANNA
        val effectivePreset = musicalPreset ?: IvannaMusicalIntentEngine.MusicalPreset(
            name = "IVANNA OMEGA",
            profile = IvannaEffectProfile.IVANNA_OMEGA,
            explanation = "He aplicado el perfil signature de IVANNA: máxima calidad, " +
                "dinámica preservada y el sonido definitivo del motor.",
            technicalDetail = "EQ OMEGA, bassBoost 540, virtualizer 460, comp -14dB/3.2:1"
        )

        val songRef = "${song.title}${song.artist?.let { " de $it" } ?: ""}"
        Log.i(TAG, "Perfil para '$songRef': ${effectivePreset.name}")

        val result = applyMusicalPreset(effectivePreset, song)

        return result.copy(
            spokenReply = buildString {
                append("He analizado ${songRef}. ")
                append(effectivePreset.explanation)
                append(" El perfil queda guardado para esta sesión.")
            }
        )
    }
}

    // ═══════════════════════════════════════════════════════════════════════
    // INTEGRACIÓN GEMINI — Ejecución de comandos DSP desde IA
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * Ejecuta un comando DSP crudo proveniente de Gemini.
     * Valida contra whitelist y mapea a acciones reales del motor.
     */
    fun executeCommand(cmd: String): Boolean {
        // Validar contra whitelist global
        if (cmd !in IvannaGeminiAgent.VALID_DSP_COMMANDS) {
            Log.w(TAG, "Comando DSP rechazado (no en whitelist): $cmd")
            return false
        }

        return runCatching {
            val result = when (cmd) {
                "voice_clarity" -> applyNamedProfile("Vocal Clarity")
                "cinema_mode" -> applyNamedProfile("Cinematic")
                "music_mode" -> applyNamedProfile("IVANNA OMEGA")
                "concert_mode" -> applyNamedProfile("Concert Massive")
                "spatial_mode" -> { voiceController.executeCommand("spatial_boost"); OrchestrationResult(true, "spatial", "Modo espacial activado", "spatial_boost") }
                "gentle_mode" -> applyNamedProfile("Abbey Road")
                "flat_mode" -> applyNamedProfile("Flat")
                "volume_up" -> { voiceController.executeCommand("volume_up"); OrchestrationResult(true, "volume", "Volumen aumentado", "volume_up") }
                "volume_down" -> { voiceController.executeCommand("volume_down"); OrchestrationResult(true, "volume", "Volumen reducido", "volume_down") }
                "bass_boost" -> { voiceController.executeCommand("bass_boost"); OrchestrationResult(true, "bass", "Graves potenciados", "bass_boost") }
                "treble_reduce" -> { voiceController.executeCommand("treble_reduce"); OrchestrationResult(true, "treble", "Agudos reducidos", "treble_reduce") }
                "auto_optimize" -> { voiceController.executeCommand("auto_optimize"); OrchestrationResult(true, "auto", "Audio optimizado automáticamente", "auto_optimize") }
                "studio_reference" -> applyNamedProfile("Studio Reference")
                "bass_boost_preset" -> applyNamedProfile("Bass Boost")
                "vocal_clarity_preset" -> applyNamedProfile("Vocal Clarity")
                "live_room_preset" -> applyNamedProfile("Live Room")
                "cinematic_preset" -> applyNamedProfile("Cinematic")
                "electronic_preset" -> applyNamedProfile("Electronic")
                "acoustic_preset" -> applyNamedProfile("Acoustic")
                "rock_preset" -> applyNamedProfile("Rock 70s")
                "podcast_preset" -> applyNamedProfile("Podcast")
                else -> {
                    Log.w(TAG, "Comando conocido pero no mapeado: $cmd")
                    OrchestrationResult(false, cmd, "Comando no implementado", "unmapped")
                }
            }
            result.applied
        }.getOrElse {
            Log.e(TAG, "Error ejecutando comando '$cmd': ${it.message}")
            false
        }
    }

    /**
     * Ejecuta una acción DSP desde el CognitiveCore.
     * Wrapper tipado sobre executeCommand().
     */
    fun executeAction(action: IvannaIntentMapper.DSPAction): Boolean {
        return executeCommand(action.command)
    }

}
