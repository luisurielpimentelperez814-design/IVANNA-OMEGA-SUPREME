package com.ivanna.omega.tidal

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

/**
 * TIDAL — tests de parsing y de cliente de API (v2.5.416).
 * Fixtures con la forma real de las respuestas de api.tidal.com/v1
 * (estructura documentada por tidalapi). Sin red.
 */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [33])
class TidalApiParsingTest {

    companion object {
        const val SEARCH_JSON = """{
          "artists": {"items": [{"id": 123, "name": "Héctor Lavoe", "picture": "abc"}], "totalNumberOfItems": 1},
          "albums": {"items": [{"id": 456, "title": "El Cantante", "artists": [{"name": "Héctor Lavoe"}], "numberOfTracks": 10, "cover": "def"}], "totalNumberOfItems": 1},
          "tracks": {"items": [{"id": 789, "title": "El Cantante", "artists": [{"name": "Héctor Lavoe"}], "album": {"title": "El Cantante"}, "duration": 312, "isrc": "USXX1234567"}], "totalNumberOfItems": 1},
          "playlists": {"items": [{"uuid": "uuid-1", "title": "Salsa dura", "numberOfTracks": 25}], "totalNumberOfItems": 1},
          "topHit": {"type": "TRACKS", "value": {"id": 789, "title": "El Cantante", "artists": [{"name": "Héctor Lavoe"}], "album": {"title": "El Cantante"}, "duration": 312}}
        }"""

        const val PLAYBACK_BTS_JSON = """{
          "trackId": 789,
          "audioMode": "STEREO",
          "audioQuality": "LOSSLESS",
          "manifestMimeType": "application/vnd.tidal.bts",
          "manifestHash": "abc",
          "manifest": "eyJ1cmxzIjpbImh0dHBzOi8vY2RuLmV4YW1wbGUvdHJhY2suZmxhYyJdLCJjb2RlY3MiOiJGTEFDIiwibWltZVR5cGUiOiJhdWRpby9mbGFjIiwiZW5jcnlwdGlvblR5cGUiOiJOT05FIn0=",
          "bitDepth": 16,
          "sampleRate": 44100
        }"""
    }

    @Test
    fun parseSearch_allSections() {
        val res = TidalModels.parseSearch(JSONObject(SEARCH_JSON))
        assertEquals(1, res.tracks.size)
        assertEquals(789L, res.tracks[0].id)
        assertEquals("El Cantante", res.tracks[0].title)
        assertEquals("Héctor Lavoe", res.tracks[0].artist)
        assertEquals("El Cantante", res.tracks[0].album)
        assertEquals(312, res.tracks[0].durationSec)
        assertEquals("USXX1234567", res.tracks[0].isrc)
        assertEquals(1, res.albums.size)
        assertEquals(456L, res.albums[0].id)
        assertEquals(10, res.albums[0].trackCount)
        assertEquals(1, res.artists.size)
        assertEquals("Héctor Lavoe", res.artists[0].name)
        assertEquals(1, res.playlists.size)
        assertEquals("uuid-1", res.playlists[0].uuid)
        assertEquals("Salsa dura", res.playlists[0].title)
        assertEquals(789L, res.topHitTrack?.id)
    }

    @Test
    fun parseTrack_defensive_missingFields() {
        assertNull(TidalModels.parseTrack(JSONObject("{}")))
        assertNull(TidalModels.parseTrack(JSONObject("""{"id": 1}""")))
        assertNull(TidalModels.parseTrack(JSONObject("""{"id": 1, "title": "  "}""")))
        val minimal = TidalModels.parseTrack(JSONObject("""{"id": 5, "title": "X"}"""))
        assertEquals(5L, minimal?.id)
        assertEquals("", minimal?.artist)
        assertEquals(0, minimal?.durationSec)
    }

    @Test
    fun parseTrackList_skipsMalformedItems() {
        val root = JSONObject("""{"items": [{"id": 1, "title": "A"}, {"title": "sin id"}, {"id": 2, "title": "B"}], "totalNumberOfItems": 3}""")
        val tracks = TidalModels.parseTrackList(root)
        assertEquals(2, tracks.size)
        assertEquals(3, TidalModels.totalItems(root))
    }

    @Test
    fun apiClient_search_usesQueryParams() {
        val http = FakeHttp(getHandler = { TidalHttpResponse(200, SEARCH_JSON) })
        val client = TidalApiClient(http)
        val res = client.search("token-abc", "Héctor Lavoe", limit = 20)
        assertEquals("El Cantante", res.tracks[0].title)
        val url = http.requestedUrls.single()
        assertTrue(url.contains("search?"))
        assertTrue(url.contains("query=H%C3%A9ctor+Lavoe"))
        assertTrue(url.contains("types=TRACKS%2CALBUMS%2CARTISTS%2CPLAYLISTS"))
        assertTrue(url.contains("limit=20"))
    }

    @Test
    fun apiClient_playbackInfo_parsesManifestFields() {
        val http = FakeHttp(getHandler = { TidalHttpResponse(200, PLAYBACK_BTS_JSON) })
        val client = TidalApiClient(http)
        val info = client.playbackInfo("token-abc", 789L, TidalApiClient.QUALITY_LOSSLESS)
        assertEquals(789L, info.trackId)
        assertEquals("LOSSLESS", info.audioQualityDelivered)
        assertEquals("application/vnd.tidal.bts", info.manifestMimeType)
        assertEquals(16, info.bitDepth)
        assertEquals(44100, info.sampleRate)
        assertTrue(info.manifestBase64.isNotBlank())
        assertTrue(http.requestedUrls.single().contains("tracks/789/playbackinfopostpaywall"))
        assertTrue(http.requestedUrls.single().contains("audioquality=LOSSLESS"))
    }

    @Test
    fun apiClient_401_isAuthError() {
        val http = FakeHttp(getHandler = { TidalHttpResponse(401, """{"error": "unauthorized"}""") })
        val client = TidalApiClient(http)
        try {
            client.search("token-malo", "x")
            fail("debió lanzar TidalApiException")
        } catch (e: TidalApiException) {
            assertTrue(e.isAuthError)
            assertEquals(401, e.status)
        }
    }

    @Test
    fun apiClient_429_backsOff60sAndRetries() {
        var calls = 0
        val delays = mutableListOf<Long>()
        val http = FakeHttp(getHandler = {
            calls++
            if (calls <= 2) TidalHttpResponse(429, "") else TidalHttpResponse(200, SEARCH_JSON)
        })
        val client = TidalApiClient(http, sleeper = { delays.add(it) })
        val res = client.search("token-abc", "x")
        assertEquals(3, calls)
        assertEquals(listOf(60_000L, 60_000L), delays)
        assertEquals(1, res.tracks.size)
    }

    @Test
    fun apiClient_429_threeTimes_throws() {
        val http = FakeHttp(getHandler = { TidalHttpResponse(429, "") })
        val delays = mutableListOf<Long>()
        val client = TidalApiClient(http, sleeper = { delays.add(it) })
        try {
            client.search("token-abc", "x")
            fail("debió lanzar tras 3 intentos")
        } catch (e: TidalApiException) {
            assertEquals(429, e.status)
            assertTrue(e.isRetryable)
        }
        assertEquals(2, delays.size)
    }

    @Test
    fun apiClient_429_honorsRetryAfter() {
        var calls = 0
        val delays = mutableListOf<Long>()
        val http = FakeHttp(getHandler = {
            calls++
            if (calls == 1) TidalHttpResponse(429, "", retryAfterSec = 120L)
            else TidalHttpResponse(200, SEARCH_JSON)
        })
        val client = TidalApiClient(http, sleeper = { delays.add(it) })
        client.search("token-abc", "x")
        assertEquals(listOf(120_000L), delays)
    }

    @Test
    fun backoff_5xx_growsWithAttempt() {
        assertEquals(5_000L, TidalBackoff.delayFor(TidalApiException("x", status = 500, isRetryable = true), 0))
        assertEquals(10_000L, TidalBackoff.delayFor(TidalApiException("x", status = 500, isRetryable = true), 1))
        assertEquals(60_000L, TidalBackoff.delayFor(TidalApiException("x", status = 500, isRetryable = true), 10))
    }
}
