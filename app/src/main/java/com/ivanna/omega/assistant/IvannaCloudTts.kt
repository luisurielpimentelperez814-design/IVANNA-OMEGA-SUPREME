package com.ivanna.omega.assistant

import android.content.Context
import android.media.AudioAttributes
import android.media.MediaPlayer
import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import kotlin.coroutines.resume

/**
 * IvannaCloudTts — voz neuronal en la nube para igualar la naturalidad y
 * expresividad de las voces de ChatGPT (Advanced Voice / gpt-4o-mini-tts).
 *
 * El motor local (android.speech.tts.TextToSpeech, ver IvannaVoiceEngine)
 * puede afinarse en pitch/rate/selección de voz, pero corre sobre el motor
 * TTS que traiga el fabricante del teléfono — nunca va a sonar como un
 * modelo neuronal de generación de habla end-to-end. Por eso esta clase es
 * un motor SEPARADO y OPCIONAL: cuando hay API key configurada, IVANNA habla
 * con esta voz; si no la hay, o falla la red, o falla la síntesis por
 * cualquier razón, devuelve null/false y quien llama (IvannaVoiceEngine)
 * cae automáticamente al TTS local — el mismo patrón de fallback silencioso
 * que ya usa IvannaGeminiAgent con Gemini.
 *
 * NUNCA lanza excepciones hacia afuera: toda esta clase está pensada para
 * fallar en silencio y dejar que el TTS local se haga cargo.
 */
object IvannaCloudTts {

    private const val TAG = "IvannaCloudTts"
    private const val ENDPOINT = "https://api.openai.com/v1/audio/speech"

    // Placeholder — igual que IvannaGeminiAgent.apiKey, se inyecta en runtime
    // (BuildConfig, Settings, etc.) y mientras no se configure, isConfigured
    // es false y esta clase nunca intenta llamar a la red.
    @Volatile private var apiKey: String = "API_KEY_PLACEHOLDER"

    // "shimmer" — voz adulta, cálida, elegante. Nunca una voz "aniñada":
    // ver la nota en IvannaVoiceEngine.scoreVoice() sobre por qué se evita
    // ese registro para el timbre de IVANNA.
    @Volatile var voice: String = "shimmer"
    @Volatile var model: String = "gpt-4o-mini-tts"

    fun setApiKey(key: String) { apiKey = key }
    val isConfigured: Boolean get() = apiKey.isNotBlank() && apiKey != "API_KEY_PLACEHOLDER"

    /**
     * Traduce cada IntentTone de IvannaVoiceEngine a una instrucción de
     * estilo en lenguaje natural para gpt-4o-mini-tts (steerability nativa
     * del modelo) — algo que el TTS local no puede hacer más allá de mover
     * pitch/rate.
     */
    private fun instructionsFor(tone: IvannaVoiceEngine.IntentTone): String = when (tone) {
        IvannaVoiceEngine.IntentTone.SIMPLE      -> "Habla con voz adulta, elegante y cálida, con ritmo natural y cercano, nunca robótica."
        IvannaVoiceEngine.IntentTone.MUSICAL     -> "Tono entusiasta y melódico, como si compartieras algo que de verdad disfrutas."
        IvannaVoiceEngine.IntentTone.TECHNICAL   -> "Tono claro, preciso y calmado, explicando algo técnico con seguridad y sin apuro."
        IvannaVoiceEngine.IntentTone.AFFIRMATION -> "Tono seguro y resolutivo, transmitiendo que la tarea ya quedó hecha."
        IvannaVoiceEngine.IntentTone.PLAYFUL     -> "Tono ligero y juguetón, con una sonrisa audible en la voz."
        IvannaVoiceEngine.IntentTone.EMPATHETIC  -> "Tono suave y comprensivo, bajando el ritmo, mostrando cuidado genuino."
        IvannaVoiceEngine.IntentTone.INTIMATE    -> "Tono bajo y cercano, casi confidencial, ritmo pausado y tranquilo."
        IvannaVoiceEngine.IntentTone.EXCITED     -> "Tono con energía genuina y sorpresa positiva, ritmo un poco más rápido."
        IvannaVoiceEngine.IntentTone.SOOTHING    -> "Tono muy calmado y reconfortante, ritmo lento, pensado para tranquilizar."
        IvannaVoiceEngine.IntentTone.STORYTELLER -> "Tono narrativo, con pausas naturales al cerrar cada idea, como contando algo."
    }

    /**
     * Sintetiza [text] y devuelve un mp3 temporal en cacheDir, o null si no
     * hay API key, no hay red, o la llamada falla por cualquier razón.
     */
    suspend fun synthesize(
        context: Context,
        text: String,
        tone: IvannaVoiceEngine.IntentTone
    ): File? = withContext(Dispatchers.IO) {
        if (!isConfigured || text.isBlank()) return@withContext null
        runCatching {
            val body = JSONObject().apply {
                put("model", model)
                put("voice", voice)
                put("input", text)
                put("instructions", instructionsFor(tone))
                put("response_format", "mp3")
            }.toString()

            val conn = (URL(ENDPOINT).openConnection() as HttpURLConnection).apply {
                requestMethod = "POST"
                setRequestProperty("Authorization", "Bearer $apiKey")
                setRequestProperty("Content-Type", "application/json; charset=utf-8")
                doOutput = true
                connectTimeout = 8_000
                readTimeout = 20_000
            }

            conn.outputStream.use { it.write(body.toByteArray(Charsets.UTF_8)) }

            if (conn.responseCode !in 200..299) {
                val err = runCatching { conn.errorStream?.bufferedReader()?.readText() }.getOrNull()
                Log.w(TAG, "TTS nube respondió ${conn.responseCode}: $err")
                conn.disconnect()
                return@runCatching null
            }

            val outFile = File.createTempFile("ivanna_cloud_tts_", ".mp3", context.cacheDir)
            conn.inputStream.use { input -> outFile.outputStream().use { output -> input.copyTo(output) } }
            conn.disconnect()
            outFile
        }.onFailure { Log.w(TAG, "synthesize() falló, se usará TTS local: ${it.message}") }
            .getOrNull()
    }

    /**
     * Reproduce [file] con MediaPlayer y suspende hasta que termine (o
     * falle). Borra el archivo temporal al finalizar en cualquier caso.
     */
    suspend fun play(file: File): Boolean = suspendCancellableCoroutine { cont ->
        val player = MediaPlayer()
        var finished = false
        fun finish(result: Boolean) {
            if (finished) return
            finished = true
            runCatching { player.release() }
            runCatching { file.delete() }
            if (cont.isActive) cont.resume(result)
        }
        try {
            player.setAudioAttributes(
                AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_ASSISTANT)
                    .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH)
                    .build()
            )
            player.setDataSource(file.absolutePath)
            player.setOnPreparedListener { it.start() }
            player.setOnCompletionListener { finish(true) }
            player.setOnErrorListener { _, _, _ -> finish(false); true }
            cont.invokeOnCancellation {
                runCatching { player.stop() }
                finish(false)
            }
            player.prepareAsync()
        } catch (e: Exception) {
            Log.w(TAG, "play() falló: ${e.message}")
            finish(false)
        }
    }
}
