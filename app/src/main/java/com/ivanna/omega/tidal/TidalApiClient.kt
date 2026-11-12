package com.ivanna.omega.tidal

import org.json.JSONObject
import java.net.URLEncoder
import java.nio.charset.StandardCharsets

/**
 * TIDAL — cliente de la API v1 (v2.5.417).
 *
 * Endpoints verificados contra tidalapi (EbbLabs/python-tidal) y el PR
 * cliamp #313 (ago 2026). Base: https://api.tidal.com/v1/
 *
 * - GET sessions → sessionId, countryCode, userId
 * - GET search?query&limit&offset&types=TRACKS,ALBUMS,ARTISTS,PLAYLISTS
 * - GET albums/{id}/tracks, artists/{id}/albums, playlists/{uuid}/tracks
 * - GET users/{id}/playlistsAndFavoritePlaylists
 * - GET tracks/{id}/playbackinfopostpaywall?playbackmode=STREAM&audioquality=..&assetpresentation=FULL
 * - GET users/{user_id}/subscription (check_login)
 *
 * Todas las llamadas van con `Authorization: Bearer <access_token>`.
 * Errores: 401/403 → isAuthError (re-login); 429 → backoff 60 s o
 * Retry-After; 5xx → reintento con backoff. El `sleeper` es inyectable
 * para que los tests verifiquen el backoff sin dormir de verdad.
 */

data class TidalSessionInfo(
    val sessionId: String,
    val countryCode: String,
    val userId: Long
)

/** Respuesta cruda de playbackinfopostpaywall: el manifiesto va en base64. */
data class TidalPlaybackInfo(
    val trackId: Long,
    val audioQualityDelivered: String,
    val manifestMimeType: String,
    val manifestBase64: String,
    val bitDepth: Int?,
    val sampleRate: Int?
)

open class TidalApiClient(
    private val http: TidalHttp,
    private val baseUrl: String = "https://api.tidal.com/v1/",
    private val sleeper: (Long) -> Unit = { Thread.sleep(it) },
    /**
     * Parámetros de sesión (v2.5.417): tidalapi inyecta `countryCode` y
     * `sessionId` en TODA petición a api.tidal.com/v1. La 416 no los
     * mandaba y Tidal respondía HTTP 400 en búsqueda y playback. Se leen
     * en vivo del store (vacíos antes del login: `sessions` no los lleva).
     */
    private val countryCodeProvider: () -> String = { "" },
    private val sessionIdProvider: () -> String = { "" }
) {

    companion object {
        const val QUALITY_LOW = "LOW"               // AAC 96 kbps
        const val QUALITY_HIGH = "HIGH"             // AAC 320 kbps
        const val QUALITY_LOSSLESS = "LOSSLESS"     // FLAC 16-bit/44.1 kHz
        const val QUALITY_HI_RES = "HI_RES_LOSSLESS" // FLAC hasta 24-bit/192 kHz
        const val DEFAULT_QUALITY = QUALITY_LOSSLESS
        private const val MAX_ATTEMPTS = 3
    }

    // ── Sesión ────────────────────────────────────────────────────────────

    fun getSession(token: String): TidalSessionInfo {
        val json = getJson(token, "sessions")
        return TidalSessionInfo(
            sessionId = json.optString("sessionId", ""),
            countryCode = json.optString("countryCode", ""),
            userId = json.optLong("userId", -1L)
        )
    }

    /** check_login(): 200 = sesión válida; cualquier error = no válida. */
    fun checkLogin(token: String, userId: Long): Boolean {
        return runCatching {
            getJson(token, "users/$userId/subscription")
            true
        }.getOrDefault(false)
    }

    // ── Catálogo ──────────────────────────────────────────────────────────

    fun search(
        token: String,
        query: String,
        limit: Int = 20,
        offset: Int = 0,
        types: String = "TRACKS,ALBUMS,ARTISTS,PLAYLISTS"
    ): TidalSearchResult {
        val params = mapOf(
            "query" to query,
            "limit" to limit.toString(),
            "offset" to offset.toString(),
            "types" to types
        )
        return TidalModels.parseSearch(getJson(token, "search", params))
    }

    fun albumTracks(token: String, albumId: Long, limit: Int = 100, offset: Int = 0): List<TidalTrack> {
        val params = mapOf("limit" to limit.toString(), "offset" to offset.toString())
        return TidalModels.parseTrackList(getJson(token, "albums/$albumId/tracks", params))
    }

    fun playlistTracks(token: String, uuid: String, limit: Int = 100, offset: Int = 0): List<TidalTrack> {
        val params = mapOf("limit" to limit.toString(), "offset" to offset.toString())
        return TidalModels.parseTrackList(getJson(token, "playlists/$uuid/tracks", params))
    }

    fun artistAlbums(token: String, artistId: Long, limit: Int = 50, offset: Int = 0): List<TidalAlbum> {
        val params = mapOf("limit" to limit.toString(), "offset" to offset.toString())
        return TidalModels.parseAlbumList(getJson(token, "artists/$artistId/albums", params))
    }

    fun userPlaylists(token: String, userId: Long): List<TidalPlaylist> {
        return TidalModels.parsePlaylistList(
            getJson(token, "users/$userId/playlistsAndFavoritePlaylists")
        )
    }

    // ── Playback ──────────────────────────────────────────────────────────

    open fun playbackInfo(token: String, trackId: Long, audioQuality: String): TidalPlaybackInfo {
        val params = mapOf(
            "playbackmode" to "STREAM",
            "audioquality" to audioQuality,
            "assetpresentation" to "FULL"
        )
        val json = getJson(token, "tracks/$trackId/playbackinfopostpaywall", params)
        return TidalPlaybackInfo(
            trackId = trackId,
            audioQualityDelivered = json.optString("audioQuality", audioQuality),
            manifestMimeType = json.optString("manifestMimeType", ""),
            manifestBase64 = json.optString("manifest", ""),
            bitDepth = json.optInt("bitDepth", -1).takeIf { it > 0 },
            sampleRate = json.optInt("sampleRate", -1).takeIf { it > 0 }
        )
    }

    // ── HTTP con backoff ──────────────────────────────────────────────────

    private fun getJson(token: String, path: String, params: Map<String, String> = emptyMap()): JSONObject {
        // Parámetros de sesión primero (tidalapi los inyecta en toda petición).
        val allParams = LinkedHashMap<String, String>()
        val cc = countryCodeProvider()
        if (cc.isNotBlank()) allParams["countryCode"] = cc
        val sid = sessionIdProvider()
        if (sid.isNotBlank()) allParams["sessionId"] = sid
        allParams.putAll(params)
        val query = allParams.entries.joinToString("&") { (k, v) ->
            "${encode(k)}=${encode(v)}"
        }
        val url = baseUrl + path + if (query.isNotEmpty()) "?$query" else ""
        val headers = mapOf(
            "Authorization" to "Bearer $token",
            "Accept" to "application/json",
            "User-Agent" to "IVANNA-Tidal/2.5.424 (Android; com.ivanna.omega)"
        )
        var attempt = 0
        while (true) {
            val resp = runCatching { http.request("GET", url, headers) }
                .getOrElse { throw TidalApiException("Sin conexión con Tidal: ${it.message}", isRetryable = true) }
            when (resp.status) {
                in 200..299 -> return runCatching { JSONObject(resp.body) }
                    .getOrElse { throw TidalApiException("Respuesta de Tidal ilegible") }
                401, 403 -> throw TidalApiException(
                    "Sesión de Tidal no válida (hay que volver a entrar)",
                    status = resp.status, isAuthError = true
                )
                429 -> {
                    val ex = TidalApiException(
                        "Tidal pide esperar (límite de peticiones)",
                        status = 429, retryAfterSec = resp.retryAfterSec, isRetryable = true
                    )
                    if (attempt >= MAX_ATTEMPTS - 1) throw ex
                    sleeper(TidalBackoff.delayFor(ex, attempt))
                    attempt++
                }
                in 500..599 -> {
                    val ex = TidalApiException(
                        "Tidal falló en su lado (HTTP ${resp.status})",
                        status = resp.status, isRetryable = true
                    )
                    if (attempt >= MAX_ATTEMPTS - 1) throw ex
                    sleeper(TidalBackoff.delayFor(ex, attempt))
                    attempt++
                }
                else -> throw TidalApiException(
                    "Tidal respondió HTTP ${resp.status}" +
                        resp.body.take(200).takeIf { it.isNotBlank() }?.let { ": $it" }.orEmpty()
                )
            }
        }
    }

    private fun encode(value: String): String =
        URLEncoder.encode(value, StandardCharsets.UTF_8.name())
}
