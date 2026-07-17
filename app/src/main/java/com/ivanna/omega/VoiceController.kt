package com.ivanna.omega

import android.content.Context
import android.util.Log
import com.ivanna.omega.ai.YamnetClassifier
import com.ivanna.omega.audio.IvannaEffectProfile
import com.ivanna.omega.core.IVANNAApplication
import com.ivanna.omega.core.ParameterStore
import com.ivanna.omega.dsp.ConcertMode

/**
 * VoiceController — routing de perfiles disparado por clasificación de audio
 * (YAMNet) y/o por comandos de texto explícitos.
 *
 * IMPORTANTE — LÍMITE HONESTO DE ESTA CLASE:
 * NO hace Speech-to-Text. Reconocer palabras habladas ("subir volumen",
 * "modo cine", etc.) requiere un motor STT (Vosk, Whisper, Google STT) que
 * este proyecto todavía no integra. Mientras eso no exista:
 *   - [processAudio]  ⇒ solo puede decidir según lo que YAMNet clasifica
 *                        (música, voz, ruido). No sabe QUÉ dice la voz.
 *   - [processCommand] ⇒ recibe un String ya reconocido (el que produzca
 *                        un STT externo cuando se conecte) y decide qué
 *                        preset aplicar.
 * La versión anterior de este archivo simulaba STT devolviendo siempre la
 * cadena "subir volumen": eso se eliminó para no mentir en la telemetría.
 */
class VoiceController(private val context: Context) {

    private val classifier = YamnetClassifier(context)
    private val params = ParameterStore(context)

    /** Diccionario de sinónimos por comando (para cuando exista STT real). */
    private val commands: Map<String, List<String>> = mapOf(
        "volume_up"    to listOf("louder", "increase volume", "subir volumen", "up", "más alto"),
        "volume_down"  to listOf("softer", "decrease volume", "bajar volumen", "down", "más bajo"),
        "cinema_mode"  to listOf("movie", "film", "cinema", "dolby", "modo cine"),
        "music_mode"   to listOf("music", "song", "pop", "rock", "modo música"),
        "flat_mode"    to listOf("flat", "neutral", "normal", "modo plano"),
        "spatial_mode" to listOf("spatial", "surround", "3d", "modo espacial"),
        "concert_mode" to listOf("concert", "live", "hall", "reverb", "modo concierto")
    )

    /**
     * Ruta por CLASIFICACIÓN de audio (sin STT). Devuelve el "hint" de perfil
     * más adecuado para el contenido detectado por YAMNet, o "none" si la
     * confianza es baja o el clasificador no está disponible.
     */
    fun processAudio(audioBuffer: FloatArray): String {
        val result = classifier.classify(audioBuffer)
        if (!result.isValid) return "none"
        return when {
            result.music  > 0.7f -> "music_mode"
            result.speech > 0.7f -> "flat_mode"       // voz → no colorear
            else                 -> "none"
        }
    }

    /**
     * Ruta por COMANDO DE TEXTO explícito (para integraciones futuras con STT
     * o con un botón de UI que dispare un comando textual). Devuelve el nombre
     * canónico del comando o "none" si no encaja ninguno.
     */
    fun processCommand(text: String): String {
        val lower = text.lowercase().trim()
        if (lower.isEmpty()) return "none"
        for ((cmd, keywords) in commands) {
            if (keywords.any { lower.contains(it) }) return cmd
        }
        return "none"
    }

    fun executeCommand(cmd: String) {
        val app = context.applicationContext as? IVANNAApplication ?: run {
            Log.w(TAG, "executeCommand($cmd) ignorado: context no es IVANNAApplication")
            return
        }
        when (cmd) {
            "volume_up" -> {
                val vol = params.getMasterVolume().coerceIn(0f, 1f)
                params.setMasterVolume((vol + 0.05f).coerceAtMost(1f))
                Log.i(TAG, "Volumen: ${params.getMasterVolume()}")
            }
            "volume_down" -> {
                val vol = params.getMasterVolume().coerceIn(0f, 1f)
                params.setMasterVolume((vol - 0.05f).coerceAtLeast(0f))
                Log.i(TAG, "Volumen: ${params.getMasterVolume()}")
            }
            "cinema_mode"  -> app.globalEffectManager.applyProfile(IvannaEffectProfile.SPATIAL)
                              .also { Log.i(TAG, "Modo cine (SPATIAL) activado") }
            "music_mode"   -> app.globalEffectManager.applyProfile(IvannaEffectProfile.WARM)
                              .also { Log.i(TAG, "Modo música (WARM) activado") }
            "flat_mode"    -> {
                app.globalEffectManager.applyProfile(IvannaEffectProfile.FLAT)
                ConcertMode.enabled = false
                Log.i(TAG, "Modo plano (FLAT) activado, ConcertMode OFF")
            }
            "spatial_mode" -> app.globalEffectManager.applyProfile(IvannaEffectProfile.SPATIAL)
                              .also { Log.i(TAG, "Modo espacial (SPATIAL) activado") }
            "concert_mode" -> {
                ConcertMode.shared.setRoomSize(0.7f)
                ConcertMode.enabled = true
                Log.i(TAG, "Modo concierto activado (roomSize=0.7)")
            }
            "none" -> Unit
            else   -> Log.w(TAG, "Comando desconocido: $cmd")
        }
    }

    companion object { private const val TAG = "VoiceController" }
}
