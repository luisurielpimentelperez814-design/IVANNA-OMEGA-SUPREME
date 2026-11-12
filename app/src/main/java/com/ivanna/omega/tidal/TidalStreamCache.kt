package com.ivanna.omega.tidal

import android.util.Log
import java.io.File

/**
 * TIDAL — caché acotada de streams ya descargados (v2.5.421).
 *
 * Un tema que ya se bajó (FLAC completo) no se vuelve a bajar: la
 * repetición es instantánea y funciona sin red (los últimos temas
 * escuchados). La caché vive en cacheDir/tidal_cache/ y se gestiona
 * sola: LRU por fecha de uso, tope 500 MB y 12 archivos. Nada se guarda
 * para siempre: es streaming con memoria, no una descarga para guardar.
 * Un tema cifrado nunca entra (el resolver lo rechaza antes).
 *
 * Clave: trackId + calidad ENTREGADA (un mismo tema en LOSSLESS y en
 * HI_RES son archivos distintos).
 */
class TidalStreamCache(
    private val baseDir: File,
    private val maxBytes: Long = 500L * 1024 * 1024,
    private val maxFiles: Int = 12
) {
    companion object {
        private const val TAG = "IVANNA.TidalCache"
    }

    fun fileFor(trackId: Long, quality: String, ext: String): File =
        File(baseDir, "t_${trackId}_${quality.lowercase()}.$ext")

    /** El archivo en caché si existe y no está vacío; null si no. */
    fun lookup(trackId: Long, quality: String, ext: String): File? {
        val f = fileFor(trackId, quality, ext)
        return if (f.isFile && f.length() > 0) f else null
    }

    /** true si el archivo vive dentro de esta caché (lo gestiona ella). */
    fun isManaged(file: File): Boolean =
        file.parentFile?.absolutePath == baseDir.absolutePath

    /**
     * Mueve un temporal recién descargado a la caché (misma clave) y
     * expulsa lo más viejo si se pasa del tope. Devuelve el archivo en
     * caché; si no se puede mover, devuelve el temporal tal cual.
     */
    fun put(trackId: Long, quality: String, ext: String, tmp: File): File {
        runCatching { baseDir.mkdirs() }
        val dest = fileFor(trackId, quality, ext)
        if (tmp.absolutePath != dest.absolutePath) {
            runCatching { dest.delete() }
            val moved = runCatching { tmp.renameTo(dest) }.getOrDefault(false)
            if (!moved) {
                // renameTo puede fallar entre filesystems: copiar y borrar.
                val copied = runCatching {
                    tmp.copyTo(dest, overwrite = true)
                    tmp.delete()
                }.isSuccess
                if (!copied) return tmp
            }
        }
        dest.setLastModified(System.currentTimeMillis())
        evictIfNeeded()
        return dest
    }

    /** Marca un archivo en caché como usado (lo protege de la expulsión). */
    fun touch(file: File) {
        runCatching { file.setLastModified(System.currentTimeMillis()) }
    }

    fun evictIfNeeded() {
        val files = baseDir.listFiles()?.filter { it.isFile }?.sortedBy { it.lastModified() }
            ?: return
        var total = files.sumOf { it.length() }
        var i = 0
        while ((total > maxBytes || files.size - i > maxFiles) && i < files.size) {
            total -= files[i].length()
            runCatching { files[i].delete() }
            Log.i(TAG, "Caché: expulsado ${files[i].name}")
            i++
        }
    }

    fun clear() {
        baseDir.listFiles()?.forEach { runCatching { it.delete() } }
    }

    fun sizeBytes(): Long = baseDir.listFiles()?.sumOf { it.length() } ?: 0L
}
