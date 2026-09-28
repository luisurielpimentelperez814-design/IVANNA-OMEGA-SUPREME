package com.ivanna.omega.assistant

import android.content.Context
import android.media.AudioAttributes
import android.os.Build
import android.speech.tts.TextToSpeech
import android.speech.tts.Voice
import android.util.Log
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import java.util.Locale

/**
 * VoiceProfile — identidad vocal de IVANNA (super-refinada).
 *
 * Voz femenina adulta, elegante, cálida y casi humana — sofisticada sin
 * artificialidad. Ajustes de oído crítico sobre la versión anterior:
 *  - Pitch 1.30 → 1.18: 1.30 rozaba el territorio "chipmunk" (voz de
 *    dibujito) en voces neurales; 1.18 es mujer joven real — aguda
 *    sin ser aniñada. El dulzor lo da la elección de voz, no el pitch.
 *  - Rate 1.05 → 0.98: hablar ligeramente por debajo del ritmo nominal
 *    del motor deja respirar las frases (los TTS apuran demasiado por
 *    defecto — la calma lee como seguridad y cercanía, no lentitud).
 *  - breathiness: la cadena humana introduce micro-respiraciones; se
 *    simulan insertando comas de respiración en humanize().
 *
 * Locale chain: es-MX primero (calidez latina), luego es-US, es-ES, es genérico.
 */
data class VoiceProfile(
    val name: String = "IVANNA",
    val locale: Locale = Locale("es", "MX"),
    val pitch: Float = 1.18f,
    val speechRate: Float = 0.98f,
    val preferNeuralVoices: Boolean = true,
    val fallbackLocales: List<Locale> = listOf(
        Locale("es", "US"),
        Locale("es", "ES"),
        Locale("es")
    )
)

enum class VoiceState { IDLE, INITIALIZING, READY, SPEAKING, UNAVAILABLE }

/**
 * IvannaVoiceEngine — motor de síntesis de voz ultra-refinado de IVANNA.
 *
 * Arquitectura híbrida (nube + local), transparente para quien llama:
 *  - speak()/speakWithIntent() intentan primero IvannaCloudTts (voz
 *    neuronal, igualando la naturalidad de ChatGPT) cuando hay API key
 *    configurada.
 *  - Si no hay key, no hay red, o la síntesis/reproducción falla por
 *    cualquier razón, caen automáticamente al motor local
 *    (android.speech.tts.TextToSpeech) sin que el llamador tenga que saber
 *    ni importarle cuál de los dos habló.
 *  - Todo el trabajo de "casi humana, no robótica" que ya vivía aquí
 *    (scoreVoice, humanize, segmentación natural, curvas de pitch/rate por
 *    IntentTone) se conserva intacto como el motor local — y como el
 *    fallback universal, sigue siendo el único camino que garantiza que
 *    IVANNA siempre pueda hablar, con o sin internet.
 *
 * Mejoras sobre la versión anterior:
 *  - Locale chain es-MX → es-US → es-ES → es (voz más cálida y latina).
 *  - scoreVoice() con WaveNet/Studio/neural + elegante/cálida/femenino.
 *  - humanize(): post-procesado de texto para micro-pausas naturales.
 *  - IntentTone.PLAYFUL y EMPATHETIC para chistes y respuestas empáticas.
 */
class IvannaVoiceEngine(
    context: Context,
    private val profile: VoiceProfile = VoiceProfile()
) {

    companion object { private const val TAG = "IvannaVoiceEngine" }

    private val appContext = context.applicationContext
    private val engineScope = CoroutineScope(SupervisorJob() + Dispatchers.Main)
    private var currentSpeechJob: Job? = null

    private val _state = MutableStateFlow(VoiceState.IDLE)
    val state: StateFlow<VoiceState> = _state.asStateFlow()

    @Volatile var selectedVoiceName: String? = null
        private set

    private var tts: TextToSpeech? = null
    private var utteranceCounter = 0L

    init {
        _state.value = VoiceState.INITIALIZING
        tts = TextToSpeech(appContext) { status ->
            if (status == TextToSpeech.SUCCESS) {
                configureVoice()
                _state.value = VoiceState.READY
                Log.i(TAG, "Voz IVANNA lista (${selectedVoiceName ?: "default del sistema"})")
            } else {
                _state.value = VoiceState.UNAVAILABLE
                Log.w(TAG, "TTS init falló (status=$status)")
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
            val localeChain = listOf(profile.locale) + profile.fallbackLocales
            var pool: List<Voice> = emptyList()
            for (loc in localeChain) {
                pool = voices.filter {
                    it.locale.language == "es" && it.locale.country == loc.country &&
                    !it.isNetworkConnectionRequired
                }
                if (pool.isNotEmpty()) break
            }
            if (pool.isEmpty()) pool = voices.filter { it.locale.language == "es" && !it.isNetworkConnectionRequired }
            if (pool.isEmpty()) pool = voices.filter { it.locale.language == "es" }
            if (pool.isEmpty()) { Log.w(TAG, "Sin voces españolas"); return@runCatching }
            val best = pool.maxByOrNull { scoreVoice(it) } ?: return@runCatching
            t.voice = best
            selectedVoiceName = best.name
            Log.i(TAG, "Voz → ${best.name} | q=${best.quality} | locale=${best.locale}")
        }.onFailure { Log.w(TAG, "Selección de voz falló: ${it.message}") }
    }

    private fun scoreVoice(v: Voice): Int {
        var s = 0
        val n = v.name.lowercase()
        s += when {
            v.quality >= Voice.QUALITY_VERY_HIGH -> 8
            v.quality >= Voice.QUALITY_HIGH      -> 5
            v.quality >= Voice.QUALITY_NORMAL    -> 2
            else                                  -> 0
        }
        if (n.contains("wavenet"))  s += 8
        if (n.contains("neural"))   s += 7
        if (n.contains("studio"))   s += 6
        if (n.contains("natural"))  s += 5
        if (n.contains("premium"))  s += 4
        if (n.contains("enhanced")) s += 3
        if (n.contains("female") || n.contains("mujer") || n.contains("femenin") || n.contains("woman")) s += 10
        // Elegante y cálida > "joven/niña": una voz adulta bien timbrada suena
        // más natural y sofisticada en TTS que forzar un registro aniñado.
        if (n.contains("elegant") || n.contains("elegante") || n.contains("warm")   ||
            n.contains("calida")  || n.contains("cálida")   || n.contains("smooth") ||
            n.contains("clear")   || n.contains("nova")     || n.contains("aria"))   s += 15
        if (n.contains("sweet") || n.contains("dulce") || n.contains("soft") || n.contains("suave"))     s += 8
        if (n.contains("-f-") || n.endsWith("-f") || n.contains("_female") ||
            n.contains("lucia") || n.contains("sofia") || n.contains("mia") ||
            n.contains("valeria") || n.contains("elena")) s += 5
        s -= when {
            v.latency >= Voice.LATENCY_VERY_HIGH -> 4
            v.latency >= Voice.LATENCY_HIGH      -> 2
            else                                  -> 0
        }
        s += when (v.locale.country) { "MX" -> 3; "US" -> 2; "ES" -> 1; else -> 0 }
        return s
    }

    /**
     * Humaniza el texto para micro-pausas y pronunciación natural en TTS.
     *
     * Capas de prosodia (lo que separa "robótica" de "casi humana"):
     *  1. Normalización de unidades a palabras hablables.
     *  2. Respiración antes de conjunciones de arrastre (y/pero/además/
     *     entonces/porque) cuando la frase ya va larga — el hablante real
     *     toma aire ahí; la coma produce la pausa en el TTS.
     *  3. Puntos suspensivos en "mira", "escucha", "fíjate" — marca de
     *     anticipación que baja la velocidad percibida sin tocar el rate.
     *  4. Comas respiratorias cada ~65 caracteres sin pausa (existente).
     */
    private fun humanize(text: String): String {
        var t = text
        t = t.replace(Regex("\\.([A-ZÁÉÍÓÚÑ])"), ". $1")
        t = t.replace(Regex("\\bkHz\\b"), "kilohercios")
        t = t.replace(Regex("\\bHz\\b"),  "hercios")
        t = t.replace(Regex("\\bdB\\b"),  "decibeles")
        t = t.replace(Regex("\\bEQ\\b"),  "ecualizador")
        t = t.replace(Regex("\\bDSP\\b"), "procesador de señal")
        t = t.replace(Regex("\\((\\d+)\\s*kHz\\)")) { ", ${it.groupValues[1]} kilohercios," }
        t = t.replace(Regex("\\((\\d+)\\s*Hz\\)"))  { ", ${it.groupValues[1]} hercios," }
        t = t.replace(Regex("\\(([^)]{1,25})\\)"), "$1")
        // Anticipación suave en marcadores de atención (solo al inicio de
        // oración o tras pausa — no en medio de una enumeración).
        t = t.replace(Regex("(?i)(^|[.!?]\\s+)(mira|escucha|fíjate|fijate|oye)\\b"), "$1$2… ")
        // Respiración antes de conjunciones cuando la cláusula previa ya
        // supera ~40 caracteres (el hablante toma aire, no encadena).
        t = t.replace(Regex("([^,;:.!?\\n]{40,}?)\\s+(pero|además|entonces|porque|aunque|mientras)\\b"), "$1, $2")
        // Comas respiratorias en frases largas
        val words = t.split(" ")
        val sb = StringBuilder()
        var charsSinPausa = 0
        for (word in words) {
            val hasPause = word.endsWith(",") || word.endsWith(".") ||
                           word.endsWith("!") || word.endsWith("?") ||
                           word.endsWith(";") || word.endsWith(":") ||
                           word.endsWith("…")
            if (charsSinPausa > 65 && !hasPause && word.length > 3) {
                sb.append("$word, "); charsSinPausa = 0
            } else {
                sb.append("$word "); charsSinPausa += word.length + 1
                if (hasPause) charsSinPausa = 0
            }
        }
        return sb.toString().trim().replace(Regex("  +"), " ")
    }

    enum class IntentTone {
        SIMPLE, MUSICAL, TECHNICAL, AFFIRMATION, PLAYFUL, EMPATHETIC,
        INTIMATE,    // voz baja y cercana — susurro cálido, confidencias
        EXCITED,     // sorpresa/entusiasmo genuino (sube pitch y ritmo)
        SOOTHING,    // calma profunda — bad news o el usuario frustrado
        STORYTELLER  // narrativa: ritmo pausado con caídas de frase
    }

    /**
     * Punto de entrada público: habla [text] con tono neutro. Intenta la voz
     * en la nube primero (ver clase doc); si no aplica o falla, usa el TTS
     * local automáticamente.
     */
    fun speak(text: String) = speakWithIntent(text, IntentTone.SIMPLE)

    /**
     * Punto de entrada público con matiz emocional/estilístico. Un solo
     * método para ambos motores: en la nube, [tone] se traduce a una
     * instrucción de estilo (IvannaCloudTts.instructionsFor); en local, a
     * curvas de pitch/rate (ver [speakWithIntentLocal]).
     */
    fun speakWithIntent(text: String, tone: IntentTone) {
        if (_state.value != VoiceState.READY && _state.value != VoiceState.SPEAKING) return
        if (text.isBlank()) return
        // Solo una elocución a la vez: cancela cualquier intento anterior
        // (nube o local) antes de arrancar el nuevo, para que nunca se
        // solapen dos voces ni queden jobs colgados.
        currentSpeechJob?.cancel()
        currentSpeechJob = engineScope.launch {
            val spokenByCloud = trySpeakCloud(text, tone)
            if (!spokenByCloud) speakWithIntentLocal(text, tone)
        }
    }

    /**
     * Intenta la voz en la nube. Devuelve true si de verdad llegó a
     * reproducir audio; false ante cualquier fallo (sin key, sin red,
     * error HTTP, MediaPlayer, etc.) — nunca lanza, siempre deja la puerta
     * abierta al fallback local.
     */
    private suspend fun trySpeakCloud(text: String, tone: IntentTone): Boolean {
        if (!IvannaCloudTts.isConfigured) return false
        val processed = humanize(text)
        val file = IvannaCloudTts.synthesize(appContext, processed, tone) ?: return false
        _state.value = VoiceState.SPEAKING
        val ok = runCatching { IvannaCloudTts.play(file) }.getOrDefault(false)
        _state.value = VoiceState.READY
        return ok
    }

    /**
     * Habla con el motor local (android.speech.tts), aplicando la curva de
     * pitch/rate del [tone] alrededor de la elocución. Es el fallback
     * universal — siempre disponible, sin red.
     */
    private fun speakWithIntentLocal(text: String, tone: IntentTone) {
        val t = tts ?: return
        // Curvas prosódicas de oído: la diferencia entre "robótica" y
        // "humana" vive en cuánto varían rate+pitch dentro de la frase.
        // Cada tono tiene su propia firma — ninguno es neutro plano.
        val (rate, pitch) = when (tone) {
            IntentTone.SIMPLE      -> profile.speechRate            to profile.pitch
            IntentTone.MUSICAL     -> (profile.speechRate * 0.92f) to (profile.pitch * 1.03f)
            IntentTone.TECHNICAL   -> (profile.speechRate * 0.88f) to (profile.pitch * 0.99f)
            IntentTone.AFFIRMATION -> (profile.speechRate * 1.03f) to (profile.pitch * 1.01f)
            IntentTone.PLAYFUL     -> (profile.speechRate * 1.06f) to (profile.pitch * 1.06f)
            IntentTone.EMPATHETIC  -> (profile.speechRate * 0.88f) to (profile.pitch * 0.96f)
            IntentTone.INTIMATE    -> (profile.speechRate * 0.82f) to (profile.pitch * 0.94f)
            IntentTone.EXCITED     -> (profile.speechRate * 1.10f) to (profile.pitch * 1.08f)
            IntentTone.SOOTHING    -> (profile.speechRate * 0.80f) to (profile.pitch * 0.95f)
            IntentTone.STORYTELLER -> (profile.speechRate * 0.90f) to (profile.pitch * 1.00f)
        }
        runCatching { t.setSpeechRate(rate); t.setPitch(pitch) }
        speakLocal(text)
        runCatching { t.setSpeechRate(profile.speechRate); t.setPitch(profile.pitch) }
    }

    /** Elocución local cruda, sin ajuste de tono (usada por speakWithIntentLocal). */
    private fun speakLocal(text: String) {
        val t = tts ?: return
        if (_state.value != VoiceState.READY && _state.value != VoiceState.SPEAKING) return
        val processed = humanize(text)
        val segments  = splitIntoNaturalSegments(processed)
        _state.value  = VoiceState.SPEAKING
        val listener  = object : android.speech.tts.UtteranceProgressListener() {
            override fun onStart(utteranceId: String?) {}
            override fun onDone(utteranceId: String?) {
                if (utteranceId?.endsWith("_last") == true) _state.value = VoiceState.READY
            }
            @Deprecated("deprecated in API 21")
            override fun onError(utteranceId: String?) { _state.value = VoiceState.READY }
            override fun onError(utteranceId: String?, errorCode: Int) { _state.value = VoiceState.READY }
        }
        t.setOnUtteranceProgressListener(listener)
        // Micro-prosodia por segmento (lo que separa "humana" de "plana"):
        // la voz humana nunca sostiene pitch/rate constantes dentro de una
        // frase. Curva natural: apertura ligeramente más alta y rápida,
        // núcleo estable, y caída suave al cerrar. Los multiplicadores son
        // deliberadamente sutiles (±4%) — suficiente para romper la monotonía
        // sin sonar a caricatura. La base la fija el tono (speakWithIntentLocal).
        runCatching {
            segments.forEachIndexed { index, segment ->
                val isLast = index == segments.lastIndex
                val id   = "ivanna_${++utteranceCounter}${if (isLast) "_last" else ""}"
                val mode = if (index == 0) TextToSpeech.QUEUE_FLUSH else TextToSpeech.QUEUE_ADD
                val n = segments.size
                val pos = if (n <= 1) 0.5f else index.toFloat() / (n - 1)  // 0..1 a lo largo de la frase
                val pitchCurve = when {
                    pos < 0.25f -> 1.03f          // apertura: un punto arriba
                    pos > 0.75f -> 0.96f          // cierre: caída suave
                    else        -> 1.00f          // núcleo: estable
                }
                val rateCurve = when {
                    pos < 0.25f -> 1.02f
                    pos > 0.75f -> 0.97f
                    else        -> 1.00f
                }
                // El tono ya fijó rate/pitch base en el TTS; aquí solo se
                // modula el segmento actual. Se reestablece al salir.
                runCatching {
                    t.setPitch(profile.pitch * pitchCurve)
                    t.setSpeechRate(profile.speechRate * rateCurve)
                }
                t.speak(segment.trim(), mode, null, id)
            }
        }.onFailure { Log.w(TAG, "speak falló: ${it.message}"); _state.value = VoiceState.READY }
    }

    private fun splitIntoNaturalSegments(text: String): List<String> {
        if (text.length <= 100) return listOf(text)
        val bySentence = text.split(Regex("(?<=[.!?])\\s+")).filter { it.isNotBlank() }
        if (text.length <= 250 || bySentence.size <= 1) {
            return if (bySentence.size <= 1) listOf(text) else bySentence
        }
        return bySentence.flatMap { sentence ->
            if (sentence.length > 80) {
                sentence.split(Regex("(?<=,)\\s+|(?<=:)\\s+"))
                    .filter { it.isNotBlank() }.takeIf { it.size > 1 } ?: listOf(sentence)
            } else listOf(sentence)
        }
    }

    fun stop() {
        currentSpeechJob?.cancel()
        runCatching { tts?.stop() }
        if (_state.value == VoiceState.SPEAKING) _state.value = VoiceState.READY
    }

    fun release() {
        stop()
        engineScope.cancel()
        runCatching { tts?.shutdown() }
        tts = null
        _state.value = VoiceState.IDLE
    }
}
