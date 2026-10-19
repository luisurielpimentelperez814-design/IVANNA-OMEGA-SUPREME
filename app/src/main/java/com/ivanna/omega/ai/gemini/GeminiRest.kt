package com.ivanna.omega.ai.gemini

import android.content.Context
import android.content.pm.PackageManager
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.security.MessageDigest

/**
 * Cliente REST mínimo y honesto para la API de Gemini.
 *
 * Existe porque un "HTTP 403" a secas no dice nada: Google responde con un cuerpo
 * JSON (`error.status`, `error.message`, `error.details[].reason`) que distingue
 * entre clave restringida a otra app, API no habilitada, clave bloqueada por filtración,
 * cuota o región. Aquí se lee ese cuerpo y se traduce a una causa accionable,
 * en lugar de esconderla.
 *
 * Solo envía el texto que se le pasa (nunca audio). La clave viaja únicamente en
 * el header `x-goog-api-key`, nunca en la URL.
 */
object GeminiRest {
    data class ApiError(val http: Int, val status: String, val reason: String, val message: String)

    data class Response(val http: Int, val body: String) {
        val ok: Boolean get() = http in 200..299
    }

    private const val ENDPOINT = "https://generativelanguage.googleapis.com/v1beta/models/"

    /** Modelos probados en orden; solo se salta al siguiente si el actual no existe o está saturado. */
    val MODEL_CHAIN = listOf("gemini-3.8-flash", "gemini-3.6-flash", "gemini-3.5-flash-lite")

    /** Quita espacios, saltos de línea y comillas que se cuelan al pegar la clave. */
    fun cleanKey(raw: String): String =
        raw.trim().trim('"', '\'', '`').filter { !it.isWhitespace() }

    /** Puro y testeable: extrae status/reason/message del cuerpo de error de Google. */
    fun parseError(http: Int, body: String): ApiError {
        val err = runCatching { JSONObject(body).optJSONObject("error") }.getOrNull()
            ?: return ApiError(http, "", "", body.take(200))
        val details = err.optJSONArray("details")
        var reason = ""
        if (details != null) {
            for (i in 0 until details.length()) {
                val r = details.optJSONObject(i)?.optString("reason").orEmpty()
                if (r.isNotEmpty()) { reason = r; break }
            }
        }
        val message = err.optString("message").take(300)
        // Some Google gateways omit details[].reason but still identify the
        // Android application restriction in the actual error message.
        if (reason.isBlank() && "android client application" in message.lowercase()) {
            reason = "API_KEY_ANDROID_APP_BLOCKED"
        }
        return ApiError(http, err.optString("status"), reason, message)
    }

    /** Puro y testeable: causa probable en español, según lo que Google respondió de verdad. */
    fun explain(e: ApiError): String {
        val m = e.message.lowercase()
        val cause = when {
            e.reason == "API_KEY_ANDROID_APP_BLOCKED" || "android client application" in m ->
                "la clave está restringida a otra app Android (paquete/huella SHA-1 no coinciden)"
            e.reason == "API_KEY_SERVICE_BLOCKED" ->
                "la clave está limitada a otra API; en AI Studio/Cloud debe permitir «Generative Language API»"
            e.reason == "SERVICE_DISABLED" || "has not been used in project" in m || "is disabled" in m ->
                "la API Generative Language no está habilitada en el proyecto de esa clave"
            "leaked" in m ->
                "Google bloqueó la clave por considerarla filtrada; hay que crear una nueva"
            e.reason == "API_KEY_INVALID" || "api key not valid" in m || e.http == 400 && "key" in m ->
                "Google no reconoce la clave (copia incompleta o con caracteres extra)"
            e.http == 429 || e.status == "RESOURCE_EXHAUSTED" ->
                "cuota o límite de peticiones agotado; espera unos minutos"
            "location is not supported" in m || "user location" in m ->
                "Gemini no está disponible para la región/red actual"
            e.http == 401 || e.reason == "ACCESS_TOKEN_TYPE_UNSUPPORTED" ->
                "Google rechazó el tipo de clave (formato «AQ.» no aceptado en este endpoint)"
            e.http == 403 ->
                "permiso denegado por Google para esta clave"
            e.http == 404 ->
                "el modelo solicitado no existe para esta clave"
            else -> "respuesta inesperada de Google"
        }
        val detail = e.message.ifBlank { e.status }.take(160)
        return "HTTP ${e.http}: $cause" + if (detail.isNotBlank()) " — Google dice: «$detail»" else ""
    }

    /** SHA-1 (hex mayúsculas, sin ':') del certificado con el que está firmada esta app. */
    fun signingSha1(context: Context): String? = runCatching {
        val pm = context.packageManager
        @Suppress("DEPRECATION")
        val info = pm.getPackageInfo(context.packageName, PackageManager.GET_SIGNING_CERTIFICATES)
        val sig = info.signingInfo?.apkContentsSigners?.firstOrNull() ?: return@runCatching null
        MessageDigest.getInstance("SHA-1").digest(sig.toByteArray())
            .joinToString("") { "%02X".format(it) }
    }.getOrNull()

    /**
     * POST generateContent. Los headers X-Android-Package/Cert hacen que una clave
     * restringida a «Aplicaciones Android» sea aceptada; en claves sin restricción no cambian nada.
     */
    fun generate(context: Context, key: String, model: String, bodyJson: String, readTimeoutMs: Int = 25_000): Response {
        val conn = URL("$ENDPOINT$model:generateContent").openConnection() as HttpURLConnection
        return try {
            conn.requestMethod = "POST"; conn.doOutput = true
            conn.connectTimeout = 10_000; conn.readTimeout = readTimeoutMs
            conn.setRequestProperty("Content-Type", "application/json")
            conn.setRequestProperty("x-goog-api-key", key)
            conn.setRequestProperty("X-Android-Package", context.packageName)
            signingSha1(context)?.let { conn.setRequestProperty("X-Android-Cert", it) }
            conn.outputStream.use { it.write(bodyJson.toByteArray()) }
            val code = conn.responseCode
            val txt = (if (code in 200..299) conn.inputStream else conn.errorStream)
                ?.bufferedReader()?.readText().orEmpty()
            Response(code, txt)
        } finally { conn.disconnect() }
    }

    /** Prueba la cadena de modelos; devuelve el texto o lanza con la causa real explicada. */
    fun generateText(context: Context, key: String, bodyJson: String): String {
        var last: ApiError? = null
        for (model in MODEL_CHAIN) {
            val r = generate(context, key, model, bodyJson)
            if (r.ok) {
                return JSONObject(r.body).getJSONArray("candidates").getJSONObject(0)
                    .getJSONObject("content").getJSONArray("parts").getJSONObject(0).getString("text")
            }
            val e = parseError(r.http, r.body)
            last = e
            // Solo se reintenta con otro modelo si éste no existe o está saturado.
            if (r.http != 404 && r.http != 429 && r.http != 503) break
        }
        error(explain(last ?: ApiError(0, "", "", "sin respuesta")))
    }
}
