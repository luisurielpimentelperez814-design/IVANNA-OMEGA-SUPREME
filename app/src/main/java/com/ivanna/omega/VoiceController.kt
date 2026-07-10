package com.ivanna.omega

import android.content.Context
import android.util.Log
import com.ivanna.omega.ai.YamnetClassifier
import com.ivanna.omega.core.IVANNAApplication
import com.ivanna.omega.core.ParameterStore
import com.ivanna.omega.dsp.ConcertMode

class VoiceController(private val context: Context) {
    private val classifier = YamnetClassifier(context)
    private val commands = mapOf(
        "volume_up" to listOf("louder", "increase volume", "subir volumen", "up"),
        "volume_down" to listOf("softer", "decrease volume", "bajar volumen", "down"),
        "cinema_mode" to listOf("movie", "film", "cinema", "dolby"),
        "music_mode" to listOf("music", "song", "pop", "rock"),
        "flat_mode" to listOf("flat", "neutral", "normal"),
        "spatial_mode" to listOf("spatial", "surround", "3d"),
        "concert_mode" to listOf("concert", "live", "hall", "reverb")
    )

    fun processCommand(audioBuffer: FloatArray): String {
        val label = classifier.classify(audioBuffer).let { result ->
            if (result.isValid) {
                when {
                    result.speech > 0.7f -> "speech"
                    result.music > 0.7f -> "music"
                    else -> "unknown"
                }
            } else "unknown"
        }

        // Si es voz, intentar detectar comando
        if (label == "speech") {
            for ((cmd, keywords) in commands) {
                for (kw in keywords) {
                    if (audioBufferToString(audioBuffer).contains(kw, ignoreCase = true)) {
                        return cmd
                    }
                }
            }
        }
        return "none"
    }

    private fun audioBufferToString(buffer: FloatArray): String {
        // Simulación: convertir buffer a texto (en realidad necesitarías STT)
        // Por ahora, devolvemos una cadena fija para demostración
        return "subir volumen"
    }

    fun executeCommand(cmd: String) {
        val app = context.applicationContext as? IVANNAApplication ?: return
        val params = ParameterStore(context)

        when (cmd) {
            "volume_up" -> {
                val vol = params.getMasterVolume().coerceAtMost(1f)
                params.setMasterVolume((vol + 0.05f).coerceAtMost(1f))
                Log.i("VoiceController", "Volumen: ${params.getMasterVolume()}")
            }
            "volume_down" -> {
                val vol = params.getMasterVolume().coerceAtLeast(0f)
                params.setMasterVolume((vol - 0.05f).coerceAtLeast(0f))
                Log.i("VoiceController", "Volumen: ${params.getMasterVolume()}")
            }
            "cinema_mode" -> {
                app.globalEffectManager.applyProfile(
                    com.ivanna.omega.audio.IvannaEffectProfile.SPATIAL
                )
                Log.i("VoiceController", "Modo cine activado")
            }
            "music_mode" -> {
                app.globalEffectManager.applyProfile(
                    com.ivanna.omega.audio.IvannaEffectProfile.WARM
                )
                Log.i("VoiceController", "Modo música activado")
            }
            "flat_mode" -> {
                app.globalEffectManager.applyProfile(
                    com.ivanna.omega.audio.IvannaEffectProfile.FLAT
                )
                Log.i("VoiceController", "Modo plano activado")
            }
            "spatial_mode" -> {
                app.globalEffectManager.applyProfile(
                    com.ivanna.omega.audio.IvannaEffectProfile.SPATIAL
                )
                Log.i("VoiceController", "Modo espacial activado")
            }
            "concert_mode" -> {
                val concert = ConcertMode()
                concert.setRoomSize(0.7f)
                Log.i("VoiceController", "Modo concierto activado")
            }
        }
    }
}
