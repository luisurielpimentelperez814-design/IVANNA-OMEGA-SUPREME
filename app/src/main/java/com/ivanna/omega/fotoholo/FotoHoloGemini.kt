package com.ivanna.omega.fotoholo

import android.content.Context
import com.google.firebase.Firebase
import com.google.firebase.ai.ai
import com.google.firebase.ai.type.GenerativeBackend
import com.google.firebase.ai.type.generationConfig as firebaseGenerationConfig
import com.ivanna.omega.ai.gemini.GeminiRest
import com.ivanna.omega.assistant.core.SecureConfigurationManager
import com.ivanna.omega.core.CloudSyncManager
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject

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
                val prompt = "Eres ingeniero de mezcla. Espectro: graves=${"%.2f".format(low)}, agudos=${"%.2f".format(high)}. " +
                    "Elige UN entorno de esta lista exacta: ${scenes.joinToString(" | ")}. " +
                    "Devuelve JSON {\"scene\": string, \"intensity\": number entre 0 y 1}."
                val failures = mutableListOf<String>()

                // 1) Misma vía que el asistente: Firebase AI Logic (sin clave viajando por la red).
                if (CloudSyncManager.ensureFirebaseAppReady(context)) {
                    val viaFirebase = runCatching {
                        val model = Firebase.ai(backend = GenerativeBackend.googleAI()).generativeModel(
                            modelName = GeminiRest.MODEL_CHAIN.first(),
                            generationConfig = firebaseGenerationConfig { responseMimeType = "application/json" }
                        )
                        parse(model.generateContent(prompt).text.orEmpty(), scenes)
                    }
                    viaFirebase.getOrNull()?.let { return@runCatching it }
                    failures += "Firebase AI: ${viaFirebase.exceptionOrNull()?.message?.take(140)}"
                }

                // 2) REST directo con la clave del asistente, con el motivo real si Google la rechaza.
                SecureConfigurationManager.initialize(context)
                val key = GeminiRest.cleanKey(SecureConfigurationManager.getApiKey())
                if (key.isBlank()) {
                    failures += "no hay API key guardada en el asistente"
                } else {
                    val body = JSONObject()
                        .put("contents", JSONArray().put(JSONObject().put("parts", JSONArray().put(JSONObject().put("text", prompt)))))
                        .put("generationConfig", JSONObject().put("responseMimeType", "application/json"))
                    val viaRest = runCatching { parse(GeminiRest.generateText(context, key, body.toString()), scenes) }
                    viaRest.getOrNull()?.let { return@runCatching it }
                    failures += "API key: ${viaRest.exceptionOrNull()?.message}"
                }
                error(failures.joinToString(" · "))
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
