package com.ivanna.omega.tidal

import java.io.BufferedReader
import java.io.InputStreamReader
import java.net.HttpURLConnection
import java.net.URL
import java.nio.charset.StandardCharsets

/**
 * TIDAL — transporte HTTP (v2.5.416).
 *
 * Sin OkHttp/Retrofit en el proyecto: HttpURLConnection estándar.
 * La interfaz TidalHttp existe para que los tests inyecten respuestas
 * falsas sin tocar la red (nunca se martilla la API de Tidal en CI).
 *
 * Límites de abuso (documentados por tidalapi): 429 → backoff 60 s (o el
 * Retry-After del servidor); respuesta anti-abuso → 30 min sin reintentar.
 * Reintentar a lo loco prolonga el bloqueo y puede acabar en baneo de la
 * cuenta: el cliente respeta el backoff y no lo finge.
 */

data class TidalHttpResponse(
    val status: Int,
    val body: String,
    val retryAfterSec: Long? = null
)

interface TidalHttp {
    fun request(
        method: String,
        url: String,
        headers: Map<String, String> = emptyMap(),
        body: String? = null
    ): TidalHttpResponse

    /** GET binario (segmentos DASH): devuelve los bytes tal cual, sin conversión de charset. */
    fun requestBytes(
        url: String,
        headers: Map<String, String> = emptyMap()
    ): ByteArray
}

/** Errores de la API de Tidal. Nunca incluye tokens ni credenciales en el mensaje. */
class TidalApiException(
    message: String,
    val status: Int = -1,
    val retryAfterSec: Long? = null,
    /** 401/403 o error de token (invalid_client / invalid_grant): hay que re-loguear. */
    val isAuthError: Boolean = false,
    /** 429 o 5xx: reintentable con backoff. */
    val isRetryable: Boolean = false
) : Exception(message)

/** Implementación real con HttpURLConnection. Timeouts cortos: fail-closed, no colgarse. */
class UrlConnectionTidalHttp(
    private val connectTimeoutMs: Int = 15_000,
    private val readTimeoutMs: Int = 30_000
) : TidalHttp {

    override fun request(
        method: String,
        url: String,
        headers: Map<String, String>,
        body: String?
    ): TidalHttpResponse {
        val conn = (URL(url).openConnection() as HttpURLConnection).apply {
            requestMethod = method
            connectTimeout = connectTimeoutMs
            readTimeout = readTimeoutMs
            instanceFollowRedirects = true
            headers.forEach { (k, v) -> setRequestProperty(k, v) }
            if (body != null) {
                doOutput = true
                outputStream.use { it.write(body.toByteArray(StandardCharsets.UTF_8)) }
            }
        }
        return try {
            val status = conn.responseCode
            val stream = if (status in 200..299) conn.inputStream else conn.errorStream
            val text = stream?.let {
                BufferedReader(InputStreamReader(it, StandardCharsets.UTF_8)).use { r -> r.readText() }
            } ?: ""
            val retryAfter = conn.getHeaderField("Retry-After")?.toLongOrNull()
            TidalHttpResponse(status, text, retryAfter)
        } finally {
            conn.disconnect()
        }
    }

    override fun requestBytes(url: String, headers: Map<String, String>): ByteArray {
        val conn = (URL(url).openConnection() as HttpURLConnection).apply {
            requestMethod = "GET"
            connectTimeout = connectTimeoutMs
            readTimeout = readTimeoutMs
            instanceFollowRedirects = true
            headers.forEach { (k, v) -> setRequestProperty(k, v) }
        }
        return try {
            val status = conn.responseCode
            if (status !in 200..299) {
                throw TidalApiException("Descarga de Tidal falló (HTTP $status)")
            }
            conn.inputStream.use { it.readBytes() }
        } finally {
            conn.disconnect()
        }
    }
}

object TidalBackoff {
    /** Backoff de 429: Retry-After del servidor, o 60 s por defecto (tidalapi). */
    const val RATE_LIMIT_BACKOFF_MS: Long = 60_000L
    /** Anti-abuso: 30 min sin reintentar. */
    const val ABUSE_BACKOFF_MS: Long = 30 * 60_000L

    fun delayFor(ex: TidalApiException, attempt: Int): Long {
        if (ex.status == 429) {
            val base = ex.retryAfterSec?.let { it * 1_000L } ?: RATE_LIMIT_BACKOFF_MS
            return base
        }
        // 5xx: backoff exponencial suave, tope 60 s.
        return minOf(5_000L shl attempt, 60_000L)
    }
}
