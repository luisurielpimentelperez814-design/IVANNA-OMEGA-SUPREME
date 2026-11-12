package com.ivanna.omega.tidal

import android.content.Context
import android.net.Uri
import androidx.test.core.app.ApplicationProvider
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import java.io.File

/**
 * TIDAL — tests del controlador de reproducción (v2.5.416).
 * El reproductor es falso (sin MediaExtractor); el scope es Unconfined,
 * así que la resolución de cada tema corre síncrona en el hilo del test.
 * Verifica: resolve-on-play, avance al terminar, STOPPED transitorio,
 * stop del usuario, fin de cola, errores fail-closed.
 */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [33])
class TidalPlayerControllerTest {

    private class FakeSink : TidalPlayerSink {
        override var state: TidalSinkState = TidalSinkState.IDLE
        val playedUris = mutableListOf<Uri>()
        var stopCalls = 0

        override fun play(uri: Uri) {
            playedUris.add(uri)
            state = TidalSinkState.PLAYING
        }

        override fun pause() { state = TidalSinkState.PAUSED }
        override fun resume() { state = TidalSinkState.PLAYING }
        override fun stop() {
            stopCalls++
            state = TidalSinkState.STOPPED
        }
    }

    private class FakeApi(http: TidalHttp) : TidalApiClient(http) {
        val playbackCalls = mutableListOf<Long>()
        override fun playbackInfo(token: String, trackId: Long, audioQuality: String): TidalPlaybackInfo {
            playbackCalls.add(trackId)
            return TidalPlaybackInfo(trackId, "LOSSLESS", "application/vnd.tidal.bts", "e30=", 16, 44100)
        }
    }

    private class FakeAuth(
        http: TidalHttp,
        store: TidalTokenStore,
        var token: String? = "token-1"
    ) : TidalAuth(http, store) {
        override fun getValidAccessToken(): String? = token
    }

    private class FakeResolver(http: TidalHttp) : TidalStreamResolver(http) {
        val resolveCalls = mutableListOf<Long>()
        var failure: Exception? = null
        override fun resolve(info: TidalPlaybackInfo, cacheDir: File): ResolvedStream {
            resolveCalls.add(info.trackId)
            failure?.let { throw it }
            return ResolvedStream(
                "https://cdn.example/track_${info.trackId}.flac",
                "FLAC 16-bit/44.1 kHz"
            )
        }
    }

    private val t1 = TidalTrack(1L, "El Cantante", "Héctor Lavoe", "El Cantante", 312)
    private val t2 = TidalTrack(2L, "Periódico de Ayer", "Héctor Lavoe", "El Cantante", 280)
    private val t3 = TidalTrack(3L, "Mi Gente", "Héctor Lavoe", "El Cantante", 300)

    private lateinit var sink: FakeSink
    private lateinit var api: FakeApi
    private lateinit var auth: FakeAuth
    private lateinit var resolver: FakeResolver
    private lateinit var controller: TidalPlayerController
    private lateinit var cacheDir: File

    @Before
    fun setup() {
        val context: Context = ApplicationProvider.getApplicationContext()
        cacheDir = File(context.cacheDir, "tidal-controller-test").apply { mkdirs() }
        val http = FakeHttp()
        sink = FakeSink()
        api = FakeApi(http)
        auth = FakeAuth(http, FakeTokenStore())
        resolver = FakeResolver(http)
        controller = TidalPlayerController(
            sink, api, auth, resolver, cacheDir,
            scope = CoroutineScope(Dispatchers.Unconfined)
        )
    }

    @Test
    fun playQueue_resolvesOnlyFirstTrackAtPlayTime() {
        controller.playQueue(listOf(t1, t2, t3), 0)
        assertEquals(listOf(1L), resolver.resolveCalls)
        assertEquals(listOf(1L), api.playbackCalls)
        assertEquals(listOf(Uri.parse("https://cdn.example/track_1.flac")), sink.playedUris)
        assertEquals(t1, controller.currentTrack.value)
        assertEquals("FLAC 16-bit/44.1 kHz", controller.qualityLabel.value)
        assertEquals(0, controller.queueIndex.value)
    }

    @Test
    fun endOfTrack_advancesToNext() {
        controller.playQueue(listOf(t1, t2, t3), 0)
        controller.onPlayerStateChanged(TidalSinkState.PLAYING)
        controller.onPlayerStateChanged(TidalSinkState.STOPPED)
        assertEquals(listOf(1L, 2L), resolver.resolveCalls)
        assertEquals(t2, controller.currentTrack.value)
        assertEquals(1, controller.queueIndex.value)
        assertEquals(2, sink.playedUris.size)
    }

    @Test
    fun transientStoppedAfterPlay_doesNotAdvance() {
        controller.playQueue(listOf(t1, t2), 0)
        // El poll ve el STOPPED transitorio del play() interno ANTES del PLAYING.
        controller.onPlayerStateChanged(TidalSinkState.STOPPED)
        assertEquals(listOf(1L), resolver.resolveCalls)
        assertEquals(t1, controller.currentTrack.value)
        // Luego PLAYING y el fin real sí avanza.
        controller.onPlayerStateChanged(TidalSinkState.PLAYING)
        controller.onPlayerStateChanged(TidalSinkState.STOPPED)
        assertEquals(listOf(1L, 2L), resolver.resolveCalls)
        assertEquals(t2, controller.currentTrack.value)
    }

    @Test
    fun userStop_doesNotAdvance() {
        controller.playQueue(listOf(t1, t2), 0)
        controller.onPlayerStateChanged(TidalSinkState.PLAYING)
        controller.stop()
        controller.onPlayerStateChanged(TidalSinkState.STOPPED)
        // v2.5.421: el PLAYING ya prefetcheó t2 (resolveCalls [1,2]); el
        // stop lo invalida y no avanza.
        assertEquals(listOf(1L, 2L), resolver.resolveCalls)
        assertNull(controller.currentTrack.value)
        assertEquals(1, sink.stopCalls)
    }

    @Test
    fun nextAndPrevious() {
        controller.playQueue(listOf(t1, t2, t3), 0)
        controller.onPlayerStateChanged(TidalSinkState.PLAYING)
        controller.next()
        assertEquals(t2, controller.currentTrack.value)
        controller.next()
        assertEquals(t3, controller.currentTrack.value)
        controller.previous()
        assertEquals(t2, controller.currentTrack.value)
        controller.previous()
        assertEquals(t1, controller.currentTrack.value)
    }

    @Test
    fun endOfQueue_reportsFinished() {
        controller.playQueue(listOf(t1), 0)
        controller.onPlayerStateChanged(TidalSinkState.PLAYING)
        controller.onPlayerStateChanged(TidalSinkState.STOPPED)
        assertEquals("Fin de la cola", controller.statusMessage.value)
        assertNull(controller.currentTrack.value)
        assertEquals(-1, controller.queueIndex.value)
    }

    @Test
    fun pauseAndResume_doNotAdvance() {
        controller.playQueue(listOf(t1, t2), 0)
        controller.onPlayerStateChanged(TidalSinkState.PLAYING)
        controller.pause()
        controller.onPlayerStateChanged(TidalSinkState.PAUSED)
        controller.resume()
        controller.onPlayerStateChanged(TidalSinkState.PLAYING)
        // v2.5.421: el primer PLAYING prefetcheó t2 ([1,2]); el segundo
        // PLAYING no lo repite (ya está prefetcheado) ni avanza.
        assertEquals(listOf(1L, 2L), resolver.resolveCalls)
        assertEquals(t1, controller.currentTrack.value)
    }

    // ── v2.5.421: PREFETCH ──────────────────────────────────────────────

    @Test
    fun prefetch_nextTrackReadyBeforeAdvance() {
        controller.playQueue(listOf(t1, t2, t3), 0)
        // Al sonar t1, el siguiente (t2) ya se resolvió en segundo plano.
        controller.onPlayerStateChanged(TidalSinkState.PLAYING)
        assertEquals(listOf(1L, 2L), resolver.resolveCalls)
        assertEquals(listOf(1L, 2L), api.playbackCalls)
        // Al terminar t1, el avance usa el prefetch: SIN resolver de nuevo.
        controller.onPlayerStateChanged(TidalSinkState.STOPPED)
        assertEquals(listOf(1L, 2L), resolver.resolveCalls)
        assertEquals(t2, controller.currentTrack.value)
        assertEquals(2, sink.playedUris.size)
        assertEquals(Uri.parse("https://cdn.example/track_2.flac"), sink.playedUris[1])
    }

    @Test
    fun prefetch_invalidatedByStop() {
        controller.playQueue(listOf(t1, t2), 0)
        controller.onPlayerStateChanged(TidalSinkState.PLAYING)
        assertEquals(listOf(1L, 2L), resolver.resolveCalls)
        controller.stop()
        // Tras el stop, volver a poner la cola resuelve todo de nuevo:
        // el prefetch viejo no se reutiliza.
        controller.playQueue(listOf(t1, t2), 0)
        assertEquals(listOf(1L, 2L, 1L), resolver.resolveCalls)
        controller.onPlayerStateChanged(TidalSinkState.PLAYING)
        assertEquals(listOf(1L, 2L, 1L, 2L), resolver.resolveCalls)
    }

    @Test
    fun expiredSession_reportsRelogin() {
        auth.token = null
        controller.playQueue(listOf(t1), 0)
        assertTrue(controller.statusMessage.value!!.contains("Sesión"))
        assertNull(controller.currentTrack.value)
        assertTrue(sink.playedUris.isEmpty())
    }

    @Test
    fun resolverFailure_failClosedWithMessage() {
        resolver.failure = TidalApiException("Tidal no respondió")
        controller.playQueue(listOf(t1), 0)
        assertEquals("Tidal no respondió", controller.statusMessage.value)
        assertNull(controller.currentTrack.value)
        assertTrue(sink.playedUris.isEmpty())
    }

    @Test
    fun encryptedTrack_reportsProtected() {
        resolver.failure = TidalEncryptedException("Esta pista está protegida y no se puede reproducir en IVANNA")
        controller.playQueue(listOf(t1), 0)
        assertTrue(controller.statusMessage.value!!.contains("protegida"))
        assertNull(controller.currentTrack.value)
    }

    @Test
    fun quality_invalidFallsBackToLossless() {
        controller.quality = "NOPE"
        assertEquals(TidalApiClient.DEFAULT_QUALITY, controller.quality)
        controller.quality = TidalApiClient.QUALITY_HI_RES
        assertEquals(TidalApiClient.QUALITY_HI_RES, controller.quality)
    }
}
