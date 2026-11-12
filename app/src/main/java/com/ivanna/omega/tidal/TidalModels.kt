package com.ivanna.omega.tidal

import org.json.JSONArray
import org.json.JSONObject

/**
 * TIDAL — modelos de datos + parsers defensivos (v2.5.416).
 *
 * Fuente: respuestas reales de api.tidal.com/v1 tal como las devuelve la
 * librería comunitaria tidalapi (EbbLabs/python-tidal). Los parsers usan
 * opt* en todo: un campo ausente o con tipo inesperado nunca tumba la app,
 * el ítem se salta o queda con valor por defecto.
 *
 * Sin tokens ni credenciales en estas clases: solo datos públicos de
 * catálogo (títulos, artistas, duraciones).
 */

data class TidalTrack(
    val id: Long,
    val title: String,
    val artist: String,
    val album: String,
    val durationSec: Int,
    val isrc: String? = null,
    val trackNumber: Int = 0,
    val volumeNumber: Int = 1
)

data class TidalAlbum(
    val id: Long,
    val title: String,
    val artist: String,
    val coverId: String? = null,
    val trackCount: Int = 0,
    val releaseDate: String? = null
)

data class TidalArtist(
    val id: Long,
    val name: String,
    val pictureId: String? = null
)

data class TidalPlaylist(
    val uuid: String,
    val title: String,
    val trackCount: Int = 0,
    val type: String? = null
)

data class TidalSearchResult(
    val tracks: List<TidalTrack>,
    val albums: List<TidalAlbum>,
    val artists: List<TidalArtist>,
    val playlists: List<TidalPlaylist>,
    val topHitTrack: TidalTrack? = null
)

object TidalModels {

    fun parseTrack(obj: JSONObject): TidalTrack? {
        val id = obj.optLong("id", -1L)
        if (id < 0) return null
        val title = obj.optString("title", "").trim()
        if (title.isEmpty()) return null
        val artists = obj.optJSONArray("artists")
        val artist = firstArtistName(artists) ?: obj.optString("artist", "")
        val albumObj = obj.optJSONObject("album")
        val album = albumObj?.optString("title", "") ?: obj.optString("album", "")
        return TidalTrack(
            id = id,
            title = title,
            artist = artist,
            album = album,
            durationSec = obj.optInt("duration", 0),
            isrc = obj.optString("isrc", "").takeIf { it.isNotEmpty() },
            trackNumber = obj.optInt("trackNumber", 0),
            volumeNumber = obj.optInt("volumeNumber", 1)
        )
    }

    fun parseAlbum(obj: JSONObject): TidalAlbum? {
        val id = obj.optLong("id", -1L)
        if (id < 0) return null
        val title = obj.optString("title", "").trim()
        if (title.isEmpty()) return null
        return TidalAlbum(
            id = id,
            title = title,
            artist = firstArtistName(obj.optJSONArray("artists")) ?: "",
            coverId = obj.optString("cover", "").takeIf { it.isNotEmpty() },
            trackCount = obj.optInt("numberOfTracks", 0),
            releaseDate = obj.optString("releaseDate", "").takeIf { it.isNotEmpty() }
        )
    }

    fun parseArtist(obj: JSONObject): TidalArtist? {
        val id = obj.optLong("id", -1L)
        if (id < 0) return null
        val name = obj.optString("name", "").trim()
        if (name.isEmpty()) return null
        return TidalArtist(
            id = id,
            name = name,
            pictureId = obj.optString("picture", "").takeIf { it.isNotEmpty() }
        )
    }

    fun parsePlaylist(obj: JSONObject): TidalPlaylist? {
        val uuid = obj.optString("uuid", "").trim()
        if (uuid.isEmpty()) return null
        return TidalPlaylist(
            uuid = uuid,
            title = obj.optString("title", "").ifEmpty { "Sin título" },
            trackCount = obj.optInt("numberOfTracks", 0),
            type = obj.optString("type", "").takeIf { it.isNotEmpty() }
        )
    }

    /** Respuesta de GET search: { artists:{items[]}, albums:{items[]}, tracks:{items[]}, playlists:{items[]}, topHit:{...} } */
    fun parseSearch(root: JSONObject): TidalSearchResult {
        val tracks = parseItemList(root.optJSONObject("tracks")) { parseTrack(it) }
        val albums = parseItemList(root.optJSONObject("albums")) { parseAlbum(it) }
        val artists = parseItemList(root.optJSONObject("artists")) { parseArtist(it) }
        val playlists = parseItemList(root.optJSONObject("playlists")) { parsePlaylist(it) }
        val topHit = root.optJSONObject("topHit")?.let { hit ->
            val value = hit.optJSONObject("value")
            if (hit.optString("type", "") == "TRACKS" && value != null) parseTrack(value) else null
        }
        return TidalSearchResult(tracks, albums, artists, playlists, topHit)
    }

    /** Respuesta paginada genérica: { items: [...], totalNumberOfItems: N } */
    fun parseTrackList(root: JSONObject): List<TidalTrack> =
        parseItemList(root) { parseTrack(it) }

    fun parseAlbumList(root: JSONObject): List<TidalAlbum> =
        parseItemList(root) { parseAlbum(it) }

    fun parseArtistList(root: JSONObject): List<TidalArtist> =
        parseItemList(root) { parseArtist(it) }

    fun parsePlaylistList(root: JSONObject): List<TidalPlaylist> =
        parseItemList(root) { parsePlaylist(it) }

    fun totalItems(root: JSONObject): Int = root.optInt("totalNumberOfItems", -1)

    private fun <T> parseItemList(container: JSONObject?, parse: (JSONObject) -> T?): List<T> {
        val items: JSONArray = container?.optJSONArray("items") ?: return emptyList()
        val out = ArrayList<T>(items.length())
        for (i in 0 until items.length()) {
            val obj = items.optJSONObject(i) ?: continue
            val parsed = runCatching { parse(obj) }.getOrNull() ?: continue
            out.add(parsed)
        }
        return out
    }

    private fun firstArtistName(artists: JSONArray?): String? {
        if (artists == null || artists.length() == 0) return null
        return artists.optJSONObject(0)?.optString("name", "")?.takeIf { it.isNotEmpty() }
    }
}
