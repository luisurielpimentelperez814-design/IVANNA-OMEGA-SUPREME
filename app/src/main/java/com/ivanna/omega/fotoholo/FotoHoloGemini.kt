package com.ivanna.omega.fotoholo

import android.content.Context
import com.ivanna.omega.assistant.core.SecureConfigurationManager
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL

/**
 * Sugerencia de entorno FotoHolo con Gemini usando la API key ya configurada en el
 * asistente (SecureConfigurationManager). Solo envía proporciones espectrales agregadas
 * (graves/agudos), nunca audio. La escena devuelta se valida contra la lista local.
 */
object FotoHoloGemini {
    data class Suggestion(val scene: String, val intensity: Float)

    suspend fun suggest(context: Context, low: Float, high: Float, scenes: List<String>): Result<Suggestion> =
        withContext(Dispatchers.IO) {
            runCatching {
                SecureConfigurationManager.initialize(context)
                val key = SecureConfigurationManager.getApiKey()
                require(key.isNotBlank()) { "configura tu API key de Gemini en el asistente" }
                val prompt = "Eres ingeniero de mezcla. Espectro: graves=${"%.2f".format(low)}, agudos=${"%.2f".format(high)}. " +
                    "Elige UN entorno de esta lista exacta: ${scenes.joinToString(" | ")}. " +
                    "Devuelve JSON {\"scene\": string, \"intensity\": number entre 0 y 1}."
                val body = JSONObject()
                    .put("contents", JSONArray().put(JSONObject().put("parts", JSONArray().put(JSONObject().put("text", prompt)))))
                    .put("generationConfig", JSONObject().put("responseMimeType", "application/json"))
                val conn = URL("https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent")
                    .openConnection() as HttpURLConnection
                try {
                    conn.requestMethod = "POST"; conn.doOutput = true
                    conn.connectTimeout = 10_000; conn.readTimeout = 20_000
                    conn.setRequestProperty("Content-Type", "application/json")
                    conn.setRequestProperty("x-goog-api-key", key)
                    conn.outputStream.use { it.write(body.toString().toByteArray()) }
                    val code = conn.responseCode
                    val txt = (if (code in 200..299) conn.inputStream else conn.errorStream)?.bufferedReader()?.readText().orEmpty()
                    check(code in 200..299) { "HTTP $code" }
                    val out = JSONObject(txt).getJSONArray("candidates").getJSONObject(0)
                        .getJSONObject("content").getJSONArray("parts").getJSONObject(0).getString("text")
                    parse(out, scenes)
                } finally { conn.disconnect() }
            }
        }

    /** Puro: valida escena y acota intensidad. */
    fun parse(json: String, scenes: List<String>): Suggestion {
        val o = JSONObject(json.trim().removePrefix("```json").removeSuffix("```").trim())
        val raw = o.optString("scene")
        val scene = scenes.firstOrNull { it.equals(raw, ignoreCase = true) } ?: error("escena no válida: $raw")
        val i = o.optDouble("intensity", 0.5).toFloat()
        return Suggestion(scene, if (i.isFinite()) i.coerceIn(0f, 1f) else 0.5f)
    }
}
