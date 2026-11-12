package com.ivanna.omega.tidal

import android.content.Context
import androidx.test.core.app.ApplicationProvider
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
 * TIDAL — tests de la caché acotada de streams (v2.5.421).
 * Sin red: la caché solo mueve archivos locales.
 */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [33])
class TidalStreamCacheTest {

    private lateinit var dir: File
    private lateinit var tmpDir: File
    private lateinit var cache: TidalStreamCache

    @Before
    fun setup() {
        val context: Context = ApplicationProvider.getApplicationContext()
        dir = File(context.cacheDir, "tidal-cache-test").apply {
            deleteRecursively(); mkdirs()
        }
        tmpDir = File(context.cacheDir, "tidal-cache-test-tmp").apply {
            deleteRecursively(); mkdirs()
        }
        cache = TidalStreamCache(dir, maxBytes = 200L, maxFiles = 3)
    }

    private fun tmpFile(name: String, size: Int): File =
        File(tmpDir, name).apply { writeBytes(ByteArray(size) { 1 }) }

    @Test
    fun putAndLookup_roundTrip() {
        val stored = cache.put(1L, "LOSSLESS", "flac", tmpFile("a.flac", 10))
        assertTrue(stored.exists())
        assertEquals(10, stored.length())
        assertEquals(stored, cache.lookup(1L, "LOSSLESS", "flac"))
        assertTrue(cache.isManaged(stored))
        assertTrue(!cache.isManaged(File(tmpDir, "a.flac")))
    }

    @Test
    fun lookup_missingReturnsNull() {
        assertNull(cache.lookup(99L, "LOSSLESS", "flac"))
    }

    @Test
    fun lookup_emptyFileReturnsNull() {
        val empty = File(dir, "t_5_lossless.flac").apply { createNewFile() }
        assertEquals(0L, empty.length())
        assertNull(cache.lookup(5L, "LOSSLESS", "flac"))
    }

    @Test
    fun eviction_oldestGoesFirstByBytes() {
        // maxBytes=200, cada archivo 80 B: al meter el 3º (240 B) sale el más viejo.
        for (id in 1L..3L) {
            cache.put(id, "LOSSLESS", "flac", tmpFile("b$id.flac", 80))
            Thread.sleep(10)
        }
        assertNull(cache.lookup(1L, "LOSSLESS", "flac"))
        assertTrue(cache.lookup(2L, "LOSSLESS", "flac") != null)
        assertTrue(cache.lookup(3L, "LOSSLESS", "flac") != null)
    }

    @Test
    fun eviction_maxFiles() {
        // maxFiles=3: al meter el 4º sale el más viejo aunque quepa en bytes.
        for (id in 1L..4L) {
            cache.put(id, "LOSSLESS", "flac", tmpFile("c$id.flac", 10))
            Thread.sleep(10)
        }
        assertNull(cache.lookup(1L, "LOSSLESS", "flac"))
        assertTrue(cache.lookup(4L, "LOSSLESS", "flac") != null)
    }

    @Test
    fun touch_protectsFromEviction() {
        cache.put(1L, "LOSSLESS", "flac", tmpFile("d1.flac", 80))
        Thread.sleep(10)
        cache.put(2L, "LOSSLESS", "flac", tmpFile("d2.flac", 80))
        Thread.sleep(10)
        // Tocar el 1 lo vuelve el más reciente: al meter el 3º sale el 2.
        cache.touch(cache.lookup(1L, "LOSSLESS", "flac")!!)
        Thread.sleep(10)
        cache.put(3L, "LOSSLESS", "flac", tmpFile("d3.flac", 80))
        assertTrue(cache.lookup(1L, "LOSSLESS", "flac") != null)
        assertNull(cache.lookup(2L, "LOSSLESS", "flac"))
        assertTrue(cache.lookup(3L, "LOSSLESS", "flac") != null)
    }
}
