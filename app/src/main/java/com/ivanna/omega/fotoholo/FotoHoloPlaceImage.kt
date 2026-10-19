package com.ivanna.omega.fotoholo

import android.content.Context
import com.ivanna.omega.ai.gemini.GeminiRest
import org.json.JSONArray
import org.json.JSONObject

/**
 * Generación NATIVA de una imagen ilustrativa de un lugar, ambientada con el contexto musical.
 *
 * Por qué existe: el panel web llamaba a `/api/fotoholo/render-place` (server.ts), pero el WebView
 * del APK bloquea toda petición que no sea un asset empaquetado, así que en el teléfono esa ruta
 * nunca podía responder. Aquí se llama a Gemini directamente con la clave del asistente, con los
 * mismos límites y la misma salvedad de honestidad que el servidor.
 *
 * Privacidad: solo viaja TEXTO (lugar, detalles y contexto musical agregado). Nunca audio.
 * La imagen es un concepto generado por IA, no una reconstrucción verificada del lugar.
 */
object FotoHoloPlaceImage {
    const val MAX_PLACE = 240
    const val MAX_DETAILS = 500
    const val MAX_CONTEXT = 1400
    const val READ_TIMEOUT_MS = 90_000

    /** El primero admite 4K; el segundo es el respaldo si el primero no existe o está saturado. */
    val MODEL_CHAIN = listOf("gemini-3-pro-image-preview", "gemini-2.5-flash-image")

    class Image(val mimeType: String, val bytes: ByteArray, val model: String, val requestedSize: String)

    fun clean(raw: String, max: Int): String = raw.trim().take(max)

    /** Puro: el prompt de dirección de arte (mismo criterio que el servidor). */
    fun buildPrompt(place: String, details: String, musicContext: String): String =
        "Create one highly photorealistic, cinematic 16:9 wide image of this place or environment: $place" +
            ". Additional visual direction: " +
            details.ifBlank { "natural physically plausible lighting, realistic materials, high-detail architecture, credible perspective" } +
            ". MUSIC-SYNCHRONIZED IMMERSIVE ART DIRECTION: " +
            musicContext.ifBlank { "No live music analysis supplied; create a neutral ambience that fits the requested place." } +
            ". The location must feel like a believable place the listener could mentally step into while hearing the current music." +
            " Match architecture, era, lighting, crowd density, stage/set design, colors, atmosphere, weather and camera position to the described genre and venue;" +
            " treat current signal energy and low/high-frequency balance as subtle mood cues, never as facts about the song." +
            " Compose as a single cinematic environment with strong foreground/midground/background depth, realistic materials," +
            " physically plausible global illumination, fine surface detail and natural lens response." +
            " No labels, UI, text, watermarks or logos. Do not claim to identify the song from audio features." +
            " If a named real location has no reference image, make an explicitly illustrative interpretation rather than claiming documentary accuracy." +
            " This is an AI-generated immersive visual concept, not verified survey data."

    /** Puro: cuerpo de la petición. Solo el primer modelo pide 4K; el respaldo usa su tamaño por defecto. */
    fun buildBody(prompt: String, want4k: Boolean): String {
        val image = JSONObject().put("aspectRatio", "16:9")
        if (want4k) image.put("imageSize", "4K")
        return JSONObject()
            .put("contents", JSONArray().put(JSONObject().put("role", "user")
                .put("parts", JSONArray().put(JSONObject().put("text", prompt)))))
            .put("generationConfig", JSONObject()
                .put("responseModalities", JSONArray().put("IMAGE").put("TEXT"))
                .put("imageConfig", image))
            .toString()
    }

    /** Puro: extrae (mime, base64) de la respuesta; falla con motivo si el modelo no devolvió imagen. */
    fun parseImage(body: String): Pair<String, String> {
        val candidates = JSONObject(body).optJSONArray("candidates") ?: error("sin candidatos")
        for (i in 0 until candidates.length()) {
            val parts = candidates.getJSONObject(i).optJSONObject("content")?.optJSONArray("parts") ?: continue
            for (j in 0 until parts.length()) {
                val inline = parts.getJSONObject(j).optJSONObject("inlineData") ?: continue
                val mime = inline.optString("mimeType")
                val data = inline.optString("data")
                if (mime.startsWith("image/") && data.isNotEmpty()) return mime to data
            }
        }
        error("el modelo no devolvió una imagen")
    }

    fun extensionFor(mime: String): String = when (mime) {
        "image/jpeg" -> "jpg"
        "image/webp" -> "webp"
        else -> "png"
    }

    /** Bloqueante (llamar fuera del hilo principal). Lanza con la causa real si Google rechaza. */
    fun render(context: Context, key: String, place: String, details: String, musicContext: String): Image {
        val prompt = buildPrompt(clean(place, MAX_PLACE), clean(details, MAX_DETAILS), clean(musicContext, MAX_CONTEXT))
        var last: GeminiRest.ApiError? = null
        for ((index, model) in MODEL_CHAIN.withIndex()) {
            val r = GeminiRest.generate(context, key, model, buildBody(prompt, want4k = index == 0), READ_TIMEOUT_MS)
            if (r.ok) {
                val (mime, b64) = parseImage(r.body)
                val bytes = android.util.Base64.decode(b64, android.util.Base64.DEFAULT)
                return Image(mime, bytes, model, if (index == 0) "3840x2160" else "predeterminado del modelo")
            }
            last = GeminiRest.parseError(r.http, r.body)
            if (r.http != 404 && r.http != 429 && r.http != 503) break
        }
        error(GeminiRest.explain(last ?: GeminiRest.ApiError(0, "", "", "sin respuesta")))
    }
}
