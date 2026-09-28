package com.ivanna.omega.saf

import android.content.Context
import android.util.Log
import java.io.File
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.MessageDigest

/**
 * SaFCalibrationPrefs — persistencia magistral del vector latente q[7]
 * (calibración personal HRTF por Φ_SAF^∞).
 *
 * PROBLEMA QUE RESUELVE (auditoría del propietario, 2026-09-10):
 *   La persistencia previa vivía SOLO en el camino nativo
 *   (`SaFBridge.nativeSaFSaveState/LoadState`). Consecuencias reales:
 *     1. No hay verificación de integridad: un `saf_calibration_state.txt`
 *        truncado (kill durante flush) o pisado por otro proceso se cargaba
 *        con datos parciales sin aviso, corrompiendo q_t.
 *     2. No hay atomicidad: la escritura era directa, sin rename atómico
 *        — un crash entre `open()` y `close()` dejaba el archivo a medias.
 *     3. La UI no tenía forma de saber si la calibración existía sin llamar
 *        al JNI y verificar iter>0 (frágil: iter=0 puede ser "calibrado a
 *        sujeto medio" o "sin calibrar", indistinguibles).
 *     4. No había reconciliación con `SpatialAudioPrefs` (que sí es la
 *        SSOT de `hrtfSubject`), así que la calibración vivía dentro del
 *        proceso y no llegaba al DSP system-wide en cold-start.
 *
 * DISEÑO — formato binario "IVSF" v2:
 *   ┌─────────────────────────────────────────────────────────────────────┐
 *   │ offset  bytes  campo                                                │
 *   ├─────────────────────────────────────────────────────────────────────┤
 *   │   0     4      magic  = 'I','V','S','F'                             │
 *   │   4     4      version = 2 (int32 LE)                               │
 *   │   8     8      timestamp_ms (int64 LE) — cuándo se calibró          │
 *   │  16     4      iteration (int32 LE) — pasos Φ_SAF^∞ acumulados      │
 *   │  20     4      converged (int32 LE: 0/1)                            │
 *   │  24    28      q[7] float32 LE                                      │
 *   │  52    32      sha256 sobre bytes [0..52) — sella todo lo anterior  │
 *   └─────────────────────────────────────────────────────────────────────┘
 *   Total: 84 bytes por archivo. Coste ridículo, protección real.
 *
 *   El checksum SHA-256 detecta:
 *     - truncado (bytes finales cortados)
 *     - bit-flip (celda de flash degradada, disco sucio)
 *     - manipulación externa (otro proceso escribiendo encima)
 *
 * ATOMICIDAD:
 *   save() escribe primero a `saf_calibration_v2.bin.tmp`, hace fsync (vía
 *   FileDescriptor.sync) y luego renombra a `saf_calibration_v2.bin`. Si el
 *   proceso muere durante el fsync, el archivo definitivo sigue siendo el
 *   anterior (íntegro). Si muere después del rename, ya está commiteado.
 *
 * SSOT:
 *   Este archivo es la fuente de verdad Kotlin del vector q[7]. El JNI
 *   nativo (`nativeSaFSaveState`) sigue existiendo como backend, y se
 *   sincroniza en ambas direcciones desde `SaFEngine`. Si el nativo y el
 *   archivo Kotlin divergen (p. ej. porque otra sesión escribió solo en
 *   uno), gana el que tenga timestamp más reciente.
 */
object SaFCalibrationPrefs {

    private const val TAG = "SaFCalibrationPrefs"
    private const val FILE_NAME = "saf_calibration_v2.bin"
    private const val TMP_SUFFIX = ".tmp"

    // Layout constants (ver docstring). PAYLOAD_SIZE = todo lo que va bajo hash.
    private const val PAYLOAD_SIZE = 52
    private const val TOTAL_SIZE = 84
    private const val HASH_SIZE = 32
    private val MAGIC = byteArrayOf('I'.code.toByte(), 'V'.code.toByte(),
                                    'S'.code.toByte(), 'F'.code.toByte())
    private const val VERSION = 2

    // ── DTO ──────────────────────────────────────────────────────────────
    data class Snapshot(
        val q: FloatArray,        // 7 elementos exactos
        val iteration: Int,
        val converged: Boolean,
        val timestampMs: Long
    ) {
        override fun equals(other: Any?): Boolean {
            if (this === other) return true
            if (other !is Snapshot) return false
            return q.contentEquals(other.q) &&
                   iteration == other.iteration &&
                   converged == other.converged &&
                   timestampMs == other.timestampMs
        }
        override fun hashCode(): Int {
            var h = q.contentHashCode()
            h = 31 * h + iteration
            h = 31 * h + converged.hashCode()
            h = 31 * h + timestampMs.hashCode()
            return h
        }

        companion object {
            fun defaults(): Snapshot = Snapshot(
                q = FloatArray(7),
                iteration = 0,
                converged = false,
                timestampMs = 0L
            )
        }
    }

    // ── API pública ──────────────────────────────────────────────────────

    fun hasCalibration(context: Context): Boolean =
        fileFor(context).let { it.exists() && it.length() == TOTAL_SIZE.toLong() }

    /**
     * Carga la calibración persistida. Si el archivo no existe, está
     * truncado, tiene magic/versión inválidos o su SHA-256 no cuadra,
     * devuelve `Snapshot.defaults()` con log — nunca lanza.
     */
    fun load(context: Context): Snapshot {
        val f = fileFor(context)
        if (!f.exists()) return Snapshot.defaults()
        if (f.length() != TOTAL_SIZE.toLong()) {
            Log.w(TAG, "load: tamaño inesperado ${f.length()} (esperado $TOTAL_SIZE) — descartando")
            return Snapshot.defaults()
        }
        return try {
            val all = f.readBytes()
            val payload = all.copyOfRange(0, PAYLOAD_SIZE)
            val storedHash = all.copyOfRange(PAYLOAD_SIZE, TOTAL_SIZE)
            val computed = sha256(payload)
            if (!computed.contentEquals(storedHash)) {
                Log.w(TAG, "load: SHA-256 no coincide — archivo corrupto, cargando defaults")
                return Snapshot.defaults()
            }
            val bb = ByteBuffer.wrap(payload).order(ByteOrder.LITTLE_ENDIAN)
            val m0 = bb.get(); val m1 = bb.get(); val m2 = bb.get(); val m3 = bb.get()
            if (m0 != MAGIC[0] || m1 != MAGIC[1] || m2 != MAGIC[2] || m3 != MAGIC[3]) {
                Log.w(TAG, "load: magic inválido — no es un IVSF file")
                return Snapshot.defaults()
            }
            val version = bb.int
            if (version != VERSION) {
                Log.w(TAG, "load: version $version distinta de $VERSION — no migro, uso defaults")
                return Snapshot.defaults()
            }
            val ts = bb.long
            val iter = bb.int
            val convFlag = bb.int
            val q = FloatArray(7) { bb.float }
            // Sanidad: q finito y en [-1..1] (el modelo Φ_SAF acota los PC
            // a ±3σ ≈ ±0.16 en el peor caso; cualquier valor fuera de
            // [-1,1] es basura, no una calibración real).
            for (i in q.indices) {
                if (!q[i].isFinite() || q[i] < -1f || q[i] > 1f) {
                    Log.w(TAG, "load: q[$i]=${q[i]} fuera de rango — descartando")
                    return Snapshot.defaults()
                }
            }
            Snapshot(q, iter, convFlag != 0, ts)
        } catch (t: Throwable) {
            Log.w(TAG, "load: ${t.message}")
            Snapshot.defaults()
        }
    }

    /**
     * Guarda de forma atómica (write-then-rename con fsync). Devuelve true
     * si se completó el rename final. Si algo falla antes, no toca el
     * archivo bueno anterior.
     */
    fun save(context: Context, snap: Snapshot): Boolean {
        require(snap.q.size == 7) { "q debe tener 7 elementos, no ${snap.q.size}" }
        val payload = ByteArray(PAYLOAD_SIZE)
        val bb = ByteBuffer.wrap(payload).order(ByteOrder.LITTLE_ENDIAN)
        bb.put(MAGIC)
        bb.putInt(VERSION)
        bb.putLong(snap.timestampMs)
        bb.putInt(snap.iteration)
        bb.putInt(if (snap.converged) 1 else 0)
        for (v in snap.q) {
            val safe = when {
                !v.isFinite() -> 0f
                v < -1f -> -1f
                v > 1f -> 1f
                else -> v
            }
            bb.putFloat(safe)
        }
        val hash = sha256(payload)
        val full = ByteArray(TOTAL_SIZE)
        System.arraycopy(payload, 0, full, 0, PAYLOAD_SIZE)
        System.arraycopy(hash, 0, full, PAYLOAD_SIZE, HASH_SIZE)

        val target = fileFor(context)
        val tmp = File(target.parentFile, target.name + TMP_SUFFIX)
        return try {
            target.parentFile?.mkdirs()
            RandomAccessFile(tmp, "rw").use { raf ->
                raf.setLength(0)
                raf.write(full)
                // Fuerza flush a disco antes del rename — sin esto, un
                // corte de energía puede dejar tmp aún en buffers de VFS
                // y perder el commit.
                runCatching { raf.fd.sync() }
                    .onFailure { Log.w(TAG, "fd.sync no soportado: ${it.message}") }
            }
            // Rename atómico en el mismo sistema de archivos.
            val ok = tmp.renameTo(target)
            if (!ok) {
                // Fallback: algunos ROM devuelven false si target existe.
                target.delete()
                val ok2 = tmp.renameTo(target)
                if (!ok2) Log.w(TAG, "renameTo fallback también devolvió false")
                ok2
            } else true
        } catch (t: Throwable) {
            Log.w(TAG, "save: ${t.message}")
            runCatching { tmp.delete() }
            false
        }
    }

    /**
     * Ruta absoluta del archivo, para que `SaFEngine` la pase al JNI
     * (que guarda su propia representación textual). Ambos coexisten:
     * el archivo binario Kotlin es la SSOT versionada; el archivo TXT
     * nativo es el backend del optimizador.
     */
    fun filePath(context: Context): String = fileFor(context).absolutePath

    fun clear(context: Context) {
        runCatching { fileFor(context).delete() }
        runCatching { File(fileFor(context).parentFile, FILE_NAME + TMP_SUFFIX).delete() }
    }

    // ── Helpers internos ─────────────────────────────────────────────────

    private fun fileFor(context: Context): File {
        val dir = File(context.applicationContext.filesDir, "saf")
        if (!dir.exists()) dir.mkdirs()
        return File(dir, FILE_NAME)
    }

    private fun sha256(bytes: ByteArray): ByteArray =
        MessageDigest.getInstance("SHA-256").digest(bytes)
}
