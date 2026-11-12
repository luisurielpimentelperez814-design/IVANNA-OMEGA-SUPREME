package com.ivanna.omega.tidal

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import java.io.File

/**
 * TIDAL — tests del resolvedor de streams (v2.5.416).
 * Fixtures: manifiesto BTS real (base64) y MPDs DASH reales en estructura
 * (SegmentTemplate + SegmentTimeline, como los entrega Tidal).
 * Sin red: los segmentos los sirve el HTTP falso.
 */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [33])
class TidalStreamResolverTest {

    companion object {
        // BTS: {"urls":["https://cdn.example/track.flac"],"codecs":"FLAC","mimeType":"audio/flac","encryptionType":"NONE"}
        const val BTS_PLAIN =
            "eyJ1cmxzIjpbImh0dHBzOi8vY2RuLmV4YW1wbGUvdHJhY2suZmxhYyJdLCJjb2RlY3MiOiJGTEFDIiwibWltZVR5cGUiOiJhdWRpby9mbGFjIiwiZW5jcnlwdGlvblR5cGUiOiJOT05FIn0="
        // Igual, con encryptionType PROTECTED.
        const val BTS_ENCRYPTED =
            "eyJ1cmxzIjpbImh0dHBzOi8vY2RuLmV4YW1wbGUvdHJhY2suZmxhYyJdLCJjb2RlY3MiOiJGTEFDIiwibWltZVR5cGUiOiJhdWRpby9mbGFjIiwiZW5jcnlwdGlvblR5cGUiOiJQUk9URUNURUQifQ=="
        // MPD con URLs absolutas: init + 3 segmentos (S d=44100 r=2).
        const val MPD_ABSOLUTE =
            "PD94bWwgdmVyc2lvbj0iMS4wIiBlbmNvZGluZz0iVVRGLTgiPz4KPE1QRCB4bWxucz0idXJuOm1wZWc6ZGFzaDpzY2hlbWE6bXBkOjIwMTEiIG1lZGlhUHJlc2VudGF0aW9uRHVyYXRpb249IlBUM00xMi4wMDBTIiBtaW5CdWZmZXJUaW1lPSJQVDEuNTAwUyIgdHlwZT0ic3RhdGljIj4KICA8UGVyaW9kIGR1cmF0aW9uPSJQVDNNMTIuMDAwUyI+CiAgICA8QWRhcHRhdGlvblNldCBjb250ZW50VHlwZT0iYXVkaW8iIG1pbWVUeXBlPSJhdWRpby9tcDQiIGNvZGVjcz0iZmxhYyIgbGFuZz0iZW4iPgogICAgICA8UmVwcmVzZW50YXRpb24gaWQ9IjAiIGJhbmR3aWR0aD0iODAwMDAwIiBjb2RlY3M9ImZsYWMiIGF1ZGlvU2FtcGxpbmdSYXRlPSI0NDEwMCI+CiAgICAgICAgPFNlZ21lbnRUZW1wbGF0ZSB0aW1lc2NhbGU9IjQ0MTAwIiBpbml0aWFsaXphdGlvbj0iaHR0cHM6Ly9jZG4uZXhhbXBsZS9pbml0Lm1wNCIgbWVkaWE9Imh0dHBzOi8vY2RuLmV4YW1wbGUvc2VnXyROdW1iZXIkLm00cyIgc3RhcnROdW1iZXI9IjAiPgogICAgICAgICAgPFNlZ21lbnRUaW1lbGluZT4KICAgICAgICAgICAgPFMgZD0iNDQxMDAiIHI9IjIiLz4KICAgICAgICAgIDwvU2VnbWVudFRpbWVsaW5lPgogICAgICAgIDwvU2VnbWVudFRlbXBsYXRlPgogICAgICA8L1JlcHJlc2VudGF0aW9uPgogIDwvQWRhcHRhdGlvblNldD4KICA8L1BlcmlvZD4KPC9NUEQ+"
        // MPD con URLs relativas + BaseURL en el MPD: init + 1 segmento.
        const val MPD_RELATIVE =
            "PD94bWwgdmVyc2lvbj0iMS4wIiBlbmNvZGluZz0iVVRGLTgiPz4KPE1QRCB4bWxucz0idXJuOm1wZWc6ZGFzaDpzY2hlbWE6bXBkOjIwMTEiIHR5cGU9InN0YXRpYyI+CiAgPEJhc2VVUkw+aHR0cHM6Ly9jZG4uZXhhbXBsZS9hdWRpby88L0Jhc2VVUkw+CiAgPFBlcmlvZD4KICAgIDxBZGFwdGF0aW9uU2V0IGNvbnRlbnRUeXBlPSJhdWRpbyIgbWltZVR5cGU9ImF1ZGlvL21wNCI+CiAgICAgIDxSZXByZXNlbnRhdGlvbiBpZD0iMCIgY29kZWNzPSJmbGFjIiBhdWRpb1NhbXBsaW5nUmF0ZT0iNDgwMDAiPgogICAgICAgIDxTZWdtZW50VGVtcGxhdGUgdGltZXNjYWxlPSI0ODAwMCIgaW5pdGlhbGl6YXRpb249ImluaXQubXA0IiBtZWRpYT0ic2VnXyROdW1iZXIkLm00cyIgc3RhcnROdW1iZXI9IjAiPgogICAgICAgICAgPFNlZ21lbnRUaW1lbGluZT48UyBkPSI0ODAwMCIgcj0iMCIvPjwvU2VnbWVudFRpbWVsaW5lPgogICAgICAgIDwvU2VnbWVudFRlbXBsYXRlPgogICAgICA8L1JlcHJlc2VudGF0aW9uPgogIDwvQWRhcHRhdGlvblNldD4KICA8L1BlcmlvZD4KPC9NUEQ+"
        // MPD con ContentProtection (cifrado).
        const val MPD_ENCRYPTED =
            "PD94bWwgdmVyc2lvbj0iMS4wIiBlbmNvZGluZz0iVVRGLTgiPz4KPE1QRCB4bWxucz0idXJuOm1wZWc6ZGFzaDpzY2hlbWE6bXBkOjIwMTEiIHR5cGU9InN0YXRpYyI+CiAgPFBlcmlvZD4KICAgIDxBZGFwdGF0aW9uU2V0IGNvbnRlbnRUeXBlPSJhdWRpbyIgbWltZVR5cGU9ImF1ZGlvL21wNCIgY29kZWNzPSJmbGFjIj4KICAgICAgPENvbnRlbnRQcm90ZWN0aW9uIHNjaGVtZUlkVXJpPSJ1cm46dXVpZDplZGVmOGJhOS03OWQ2LTRhY2UtYTNjOC0yN2RjZDUxZDIxZWQiLz4KICAgICAgPFJlcHJlc2VudGF0aW9uIGlkPSIwIiBjb2RlY3M9ImZsYWMiIGF1ZGlvU2FtcGxpbmdSYXRlPSI0NDEwMCI+CiAgICAgICAgPFNlZ21lbnRUZW1wbGF0ZSB0aW1lc2NhbGU9IjQ0MTAwIiBpbml0aWFsaXphdGlvbj0iaHR0cHM6Ly9jZG4uZXhhbXBsZS9pbml0Lm1wNCIgbWVkaWE9Imh0dHBzOi8vY2RuLmV4YW1wbGUvc2VnXyROdW1iZXIkLm00cyIgc3RhcnROdW1iZXI9IjAiPgogICAgICAgICAgPFNlZ21lbnRUaW1lbGluZT48UyBkPSI0NDUwMCIgcj0iMiIvPjwvU2VnbWVudFRpbWVsaW5lPgogICAgICAgIDwvU2VnbWVudFRlbXBsYXRlPgogICAgICA8L1JlcHJlc2VudGF0aW9uPgogIDwvQWRhcHRhdGlvblNldD4KICA8L1BlcmlvZD4KPC9NUEQ+"
    }

    private lateinit var cacheDir: File

    @Before
    fun setup() {
        val context: Context = ApplicationProvider.getApplicationContext()
        cacheDir = File(context.cacheDir, "tidal-resolver-test").apply { mkdirs() }
    }

    private fun info(mime: String, manifest: String, quality: String = "LOSSLESS") =
        TidalPlaybackInfo(
            trackId = 789L,
            audioQualityDelivered = quality,
            manifestMimeType = mime,
            manifestBase64 = manifest,
            bitDepth = 16,
            sampleRate = 44100
        )

    @Test
    fun bts_directUrl() {
        val resolver = TidalStreamResolver(FakeHttp())
        val resolved = resolver.resolve(info("application/vnd.tidal.bts", BTS_PLAIN), cacheDir)
        assertEquals("https://cdn.example/track.flac", resolved.playbackUri)
        assertEquals("FLAC 16-bit/44.1 kHz", resolved.qualityLabel)
        assertNull(resolved.downloadedFile)
    }

    @Test
    fun bts_encrypted_rejected() {
        val resolver = TidalStreamResolver(FakeHttp())
        try {
            resolver.resolve(info("application/vnd.tidal.bts", BTS_ENCRYPTED), cacheDir)
            fail("debió rechazar el manifiesto cifrado")
        } catch (e: TidalEncryptedException) {
            assertTrue(e.message!!.contains("protegida"))
        }
    }

    @Test
    fun dash_downloadsInitPlusSegmentsInOrder() {
        val http = FakeHttp(bytesHandler = { url ->
            when {
                url.endsWith("init.mp4") -> "INIT".toByteArray()
                url.endsWith("seg_0.m4s") -> "SEG0".toByteArray()
                url.endsWith("seg_1.m4s") -> "SEG1".toByteArray()
                url.endsWith("seg_2.m4s") -> "SEG2".toByteArray()
                else -> fail("URL inesperada: $url")
            }
        })
        val resolver = TidalStreamResolver(http)
        val resolved = resolver.resolve(
            info("application/dash+xml", MPD_ABSOLUTE, "HI_RES_LOSSLESS"), cacheDir
        )
        assertTrue(resolved.playbackUri.startsWith("file://"))
        assertEquals("FLAC 16-bit/44.1 kHz", resolved.qualityLabel)
        val file = resolved.downloadedFile!!
        assertTrue(file.exists())
        // init + 3 segmentos (S d=44100 r=2 → 3 segmentos), en orden.
        assertEquals("INITSEG0SEG1SEG2", String(file.readBytes(), Charsets.UTF_8))
        assertEquals(
            listOf(
                "https://cdn.example/init.mp4",
                "https://cdn.example/seg_0.m4s",
                "https://cdn.example/seg_1.m4s",
                "https://cdn.example/seg_2.m4s"
            ),
            http.requestedUrls
        )
        file.delete()
    }

    @Test
    fun dash_relativeUrls_resolvedAgainstBaseUrl() {
        val http = FakeHttp(bytesHandler = { url ->
            when {
                url == "https://cdn.example/audio/init.mp4" -> "INIT".toByteArray()
                url == "https://cdn.example/audio/seg_0.m4s" -> "SEG0".toByteArray()
                else -> fail("URL inesperada: $url")
            }
        })
        val resolver = TidalStreamResolver(http)
        val resolved = resolver.resolve(info("application/dash+xml", MPD_RELATIVE), cacheDir)
        val file = resolved.downloadedFile!!
        assertEquals("INITSEG0", String(file.readBytes(), Charsets.UTF_8))
        file.delete()
    }

    @Test
    fun dash_encrypted_rejected() {
        val resolver = TidalStreamResolver(FakeHttp())
        try {
            resolver.resolve(info("application/dash+xml", MPD_ENCRYPTED), cacheDir)
            fail("debió rechazar el MPD cifrado")
        } catch (e: TidalEncryptedException) {
            assertTrue(e.message!!.contains("protegida"))
        }
    }

    @Test
    fun unknownMimeType_rejected() {
        val resolver = TidalStreamResolver(FakeHttp())
        try {
            resolver.resolve(info("application/vnd.tidal.unknown", BTS_PLAIN), cacheDir)
            fail("debió rechazar el formato desconocido")
        } catch (e: TidalApiException) {
            assertTrue(e.message!!.contains("no reconocido"))
        }
    }

    @Test
    fun qualityLabel_degradedToAac() {
        val resolver = TidalStreamResolver(FakeHttp())
        val infoAac = TidalPlaybackInfo(
            trackId = 1L, audioQualityDelivered = "HIGH",
            manifestMimeType = "application/vnd.tidal.bts", manifestBase64 = BTS_PLAIN,
            bitDepth = null, sampleRate = null
        )
        assertEquals("AAC 320 kbps", resolver.qualityLabel(infoAac, "mp4a.40.2"))
        val infoLow = infoAac.copy(audioQualityDelivered = "LOW")
        assertEquals("AAC 96 kbps", resolver.qualityLabel(infoLow, "mp4a.40.5"))
        val infoFlacNoRate = infoAac.copy(audioQualityDelivered = "LOSSLESS")
        assertEquals("FLAC sin pérdida", resolver.qualityLabel(infoFlacNoRate, "flac"))
    }
}
