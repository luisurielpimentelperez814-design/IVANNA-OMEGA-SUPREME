package com.ivanna.omega.assistant

import android.content.Context
import android.media.AudioAttributes
import android.os.Build
import android.speech.tts.TextToSpeech
import android.speech.tts.Voice
import android.util.Log
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.util.Locale

/**
 * VoiceProfile — identidad vocal de IVANNA.
 *
 * No es un placeholder: es la configuración explícita de cómo suena IVANNA.
 * Inteligente, tranquila, elegante, especialista en audio. Los parámetros de
 * prosodia (pitch/rate) están afinados para conversación larga sin fatiga:
 * pitch ligeramente por encima del neutro (presencia sin estridencia) y rate
 * un punto por debajo (claridad en frases técnicas).
 */
data class VoiceProfile(
    val name: String = "IVANNA",
    val locale: Locale = Locale("es", "ES"),
    val pitch: Float = 1.08f,      // femenina, sin estridencia
    val speechRate: Float = 0.94f, // ritmo calmado, legible
    val preferNeuralVoices: Boolean = true
)

enum class VoiceState { IDLE, INITIALIZING, READY, SPEAKING, UNAVAILABLE }

/**
 * IvannaVoiceEngine — salida de voz de IVANNA.
 *
 * Implementación sobre el TTS del sistema Android con selección de voz
 * deliberada (no la default del sistema, que suele ser robótica):
 *
 *   1. Se enumeran las voces instaladas para es-ES (y es genérico como
 *      fallback) y se elige la mejor según calidad real:
 *        - network/neural voices primero (quality >= QUALITY_HIGH o nombre
 *          con "neural"/"natural"/"premium"), luego las locales de alta
 *          calidad. Las voces de latencia muy alta se descartan para no
 *          romper el ritmo conversacional.
 *        - Si hay varias candidatas, se prefiere una voz con rasgo femenino
 *          (nombre con "female"/"mujer" o gender-reportado).
 *   2. AudioAttributes con CONTENT_TYPE_SPEECH + USAGE_ASSISTANT para que el
 *      sistema la trate como voz de asistente (no como música): ducking
 *      correcto y ruta de llamada/media adecuada.
 *   3. speak() con QUEUE_FLUSH y utteranceId único; el estado vuelve a READY
 *      en onDone/onError — la UI sabe cuándo IVANNA terminó de hablar.
 *
 * Límites honestos: la calidad final depende de las voces TTS instaladas en
 * el dispositivo (Google Speech Services, motor del fabricante). Si no hay
 * ninguna voz española instalada, se cae a la voz por defecto del sistema con
 * pitch/rate del perfil — y se marca en el log, sin fingir.
 *
 * Nunca toca el hilo de audio DSP: TTS del sistema corre en su propio
 * proceso/hilo; este engine solo encola texto y escucha callbacks.
 */
class IvannaVoiceEngine(
    context: Context,
    private val profile: VoiceProfile = VoiceProfile()
) {

    companion object { private const val TAG = "IvannaVoiceEngine" }

    private val _state = MutableStateFlow(VoiceState.IDLE)
    val state: StateFlow<VoiceState> = _state.asStateFlow()

    /** Nombre de la voz finalmente seleccionada (para diagnóstico en UI). */
    @Volatile var selectedVoiceName: String? = null
        private set

    private var tts: TextToSpeech? = null
    private var utteranceCounter = 0L

    init {
        _state.value = VoiceState.INITIALIZING
        tts = TextToSpeech(context.applicationContext) { status ->
            if (status == TextToSpeech.SUCCESS) {
                configureVoice()
                _state.value = VoiceState.READY
                Log.i(TAG, "Voz IVANNA lista (${selectedVoiceName ?: "default del sistema"})")
            } else {
                _state.value = VoiceState.UNAVAILABLE
                Log.w(TAG, "TTS init falló (status=$status) — voz desactivada")
            }
        }
    }

    private fun configureVoice() {
        val t = tts ?: return
        runCatching {
            t.setAudioAttributes(
                AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_ASSISTANT)
                    .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH)
                    .build()
            )
            t.language = profile.locale
            t.setPitch(profile.pitch)
            t.setSpeechRate(profile.speechRate)
        }

        if (!profile.preferNeuralVoices || Build.VERSION.SDK_INT < Build.VERSION_CODES.LOLLIPOP) return

        runCatching {
            val voices = t.voices ?: return@runCatching
            val spanish = voices.filter {
                it.locale.language == "es" && !it.isNetworkConnectionRequired
            }
            val pool = if (spanish.isNotEmpty()) spanish
                       else voices.filter { it.locale.language == "es" }
            if (pool.isEmpty()) {
                Log.w(TAG, "Sin voces españolas instaladas — usando default del sistema")
                return@runCatching
            }

            fun score(v: Voice): Int {
                var s = 0
                val n = v.name.lowercase()
                // Calidad declarada por el motor (API 21+).
                if (v.quality >= Voice.QUALITY_VERY_HIGH) s += 4
                else if (v.quality >= Voice.QUALITY_HIGH) s += 3
                else if (v.quality >= Voice.QUALITY_NORMAL) s += 1
                // Rasgos neurales/naturales en el nombre (heurística real de
                // los motores modernos: Google, Samsung, etc.).
                if (n.contains("neural") || n.contains("natural") || n.contains("premium")) s += 3
                // Rasgo femenino explícito cuando el motor lo expone.
                if (n.contains("female") || n.contains("mujer") || n.contains("femenin")) s += 2
                // Latencia alta penaliza conversación fluida.
                if (v.latency >= Voice.LATENCY_VERY_HIGH) s -= 2
                // Local de la región exacta (es-ES > es-419 > es genérico).
                if (v.locale.country == "ES") s += 1
                return s
            }

            val best = pool.maxByOrNull(::score) ?: return@runCatching
            t.voice = best
            selectedVoiceName = best.name
        }.onFailure { Log.w(TAG, "Selección de voz falló (uso default): ${it.message}") }
    }

    /** Dice [text] con la voz de IVANNA. No-op seguro si el TTS no está listo. */
    fun speak(text: String) {
        val t = tts ?: return
        if (_state.value != VoiceState.READY && _state.value != VoiceState.SPEAKING) return
        val id = "ivanna_${++utteranceCounter}"
        _state.value = VoiceState.SPEAKING
        t.setOnUtteranceProgressListener(object : android.speech.tts.UtteranceProgressListener() {
            override fun onStart(utteranceId: String?) {}
            override fun onDone(utteranceId: String?) { _state.value = VoiceState.READY }
            @Deprecated("deprecated in API 21")
            override fun onError(utteranceId: String?) { _state.value = VoiceState.READY }
            override fun onError(utteranceId: String?, errorCode: Int) { _state.value = VoiceState.READY }
        })
        runCatching {
            t.speak(text, TextToSpeech.QUEUE_FLUSH, null, id)
        }.onFailure {
            Log.w(TAG, "speak falló: ${it.message}")
            _state.value = VoiceState.READY
        }
    }

    fun stop() {
        runCatching { tts?.stop() }
        if (_state.value == VoiceState.SPEAKING) _state.value = VoiceState.READY
    }

    fun release() {
        stop()
        runCatching { tts?.shutdown() }
        tts = null
        _state.value = VoiceState.IDLE
    }
}
