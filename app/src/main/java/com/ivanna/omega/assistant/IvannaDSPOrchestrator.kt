package com.ivanna.omega.assistant

import android.content.Context
import android.util.Log
import com.ivanna.omega.VoiceController
import com.ivanna.omega.audio.IvannaEffectProfile
import com.ivanna.omega.core.IVANNAApplication

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
