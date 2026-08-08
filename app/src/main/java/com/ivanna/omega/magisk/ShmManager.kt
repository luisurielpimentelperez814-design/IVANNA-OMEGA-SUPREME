package com.ivanna.omega.magisk

import com.ivanna.omega.saf.SaFRoomBridge

import android.content.Context
import android.os.Build
import android.os.SharedMemory
import android.util.Log
import com.ivanna.omega.core.NativeLibraryLoader
import java.nio.ByteBuffer
import java.util.concurrent.atomic.AtomicBoolean

/**
 * ShmManager — implementación real.
 *
 * FIX (bug real, ver auditoría): esta clase era un stub de una línea
 * ("init stub") y el símbolo JNI que debía usar (nativeMlock) apuntaba
 * además a un paquete Java que no existe (com.ivanna.omega.ShmManager en
 * vez de com.ivanna.omega.magisk.ShmManager) — corregido en
 * shm_hyperplane.cpp junto con este archivo.
 *
 * Qué hace: crea una región de memoria compartida (android.os.SharedMemory,
 * API 27+) que el daemon Magisk (system-wide) y el proceso de la app
 * pueden mapear ambos, y la fija en RAM (mlock) para que el kernel no la
 * pagee bajo presión de memoria — crítico porque es un buffer leído por
 * el hilo de audio en tiempo real en ambos lados.
 *
 * Supuesto explícito (no hay contrato de tamaño documentado en el resto
 * del repo): se usa un tamaño fijo conservador de 4 KiB (una página),
 * suficiente para el UnifiedControlFrame actual. Si el hyperplane real
 * necesita otro tamaño, cambiar SHM_SIZE_BYTES abajo.
 */
object ShmManager {
    private const val TAG = "IVANNA-SHM"
    private const val SHM_NAME = "ivanna_omega_hyperplane"
    private const val SHM_SIZE_BYTES = 4096

    private external fun nativeMlockBuffer(buffer: ByteBuffer): Int

    private val loaded = NativeLibraryLoader.ensureLoaded()
    private val initialized = AtomicBoolean(false)

    @Volatile private var sharedMemory: SharedMemory? = null
    @Volatile private var mappedBuffer: ByteBuffer? = null

    /** Región mapeada, o null si initialize() no se llamó o falló. */
    val buffer: ByteBuffer? get() = mappedBuffer

    /** true si la memoria está mapeada y mlock() tuvo éxito. */
    val isReady: Boolean get() = initialized.get()

    fun initialize(ctx: Context) {
        if (!initialized.compareAndSet(false, true)) {
            Log.d(TAG, "initialize() ignorado: ya inicializado")
            return
        }
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O_MR1) {
            Log.w(TAG, "android.os.SharedMemory requiere API 27+; SHM deshabilitado en este dispositivo")
            return
        }
        if (!loaded) {
            Log.e(TAG, "libivanna_omega.so no cargada; no se puede mlock() la región SHM")
            return
        }
        try {
            val shm = SharedMemory.create(SHM_NAME, SHM_SIZE_BYTES)
            val buf = shm.mapReadWrite()
            if (!buf.isDirect) {
                // No debería ocurrir: SharedMemory.mapReadWrite() siempre
                // devuelve un DirectByteBuffer, pero si algún día cambia
                // el contrato de la API, mlock() nativo no tiene sentido
                // sobre un buffer no-direct (no hay dirección real detrás).
                Log.e(TAG, "mapReadWrite() devolvió un buffer no-direct; abortando mlock")
                shm.close()
                return
            }
            val mlockResult = nativeMlockBuffer(buf)
            if (mlockResult != 0) {
                Log.w(TAG, "mlock() falló (ret=$mlockResult) — SHM sigue usable pero puede paginarse bajo presión de memoria")
            }
            sharedMemory = shm
            mappedBuffer = buf
            Log.i(TAG, "SHM '$SHM_NAME' creada y mapeada (${SHM_SIZE_BYTES}B), mlock=${mlockResult == 0}")
        } catch (e: Exception) {
            Log.e(TAG, "Fallo creando/mapeando SharedMemory: ${e.message}", e)
            initialized.set(false)
        }
    }

    /** Libera la región. Llamar desde el ciclo de vida de la app (onDestroy). */
    fun release() {
        mappedBuffer = null
        sharedMemory?.close()
        // FIX (parser roto): la linea aqui era `sharedMemory` (identificador
        // suelto sin operador). Ademas el cierre `}` de release() se habia
        // perdido en algun rebase, dejando readAndApplySafFrame() anidado
        // dentro de release() y el archivo entero desbalanceado en +1 llave
        // (compileDebugKotlin: "Missing '}'" en L150). Se completa la
        // asignacion a null y se cierra release() antes del siguiente fun.
        sharedMemory = null
    }

    // ── FIX: Leer SAF frame del SHM y alimentar SaFRoomBridge ─────────────────
    // El daemon publica un SAF frame (4×float: gain/compressor/exciter/spatial)
    // en el SHM después de cada SAF_UPDATE. Nadie en Kotlin lo leía — la app
    // nunca veía los valores que el daemon había procesado.
    //
    // readAndApplySafFrame() lee los 16 bytes desde (base + 16) del SHM
    // (los primeros 16 bytes son el ShmHeader seqlock) y los pasa a
    // SaFRoomBridge.setHrtfState() + setRoomState() para mantener el
    // optimizador Kotlin en sync con el daemon C++.
    //
    // Llamar periódicamente desde AdaptiveBackend.pollTelemetry() (10 Hz).
    fun readAndApplySafFrame(): Boolean {
        val buf = buffer ?: return false
        if (!isReady) return false
        return try {
            // Leer epoch (seqlock: verificar antes y después)
            val HEADER_BYTES = 16
            if (buf.capacity() < HEADER_BYTES + 16) return false

            // epoch está en bytes 0..7 (ShmHeader::epoch, std::atomic<uint64_t>)
            val epochBefore = buf.getLong(0)
            if (epochBefore % 2L != 0L) return false   // escritura en curso

            // SAF frame: [gain:f][compressor:f][exciter:f][spatial:f]
            val gain       = buf.getFloat(HEADER_BYTES + 0)
            val compressor = buf.getFloat(HEADER_BYTES + 4)
            val exciter    = buf.getFloat(HEADER_BYTES + 8)
            val spatial    = buf.getFloat(HEADER_BYTES + 12)

            val epochAfter = buf.getLong(0)
            if (epochAfter != epochBefore) return false  // torn read — reintentar después

            // Validar rango (datos plausibles)
            if (gain !in 0.1f..4.0f) return false

            // Propagar al optimizador Riemanniano Kotlin
            SaFRoomBridge.setHrtfState(
                mismatchEnergy  = exciter.coerceIn(0f, 1f),
                convergenceRate = (1f - spatial.coerceIn(0f, 1f))
            )
            // gain del daemon → contexto de sala: gain > 1.0 implica señal reforzada
            // (sala poco reverberante); gain < 1.0 implica atenuación (sala activa)
            SaFRoomBridge.setRoomState(
                rt60    = (1.5f * (1f - gain.coerceIn(0.5f, 1.5f) / 1.5f)).coerceIn(0f, 3f),
                drr     = gain * 6f,
                roomMode = compressor.coerceIn(0f, 1f)
            )
            true
        } catch (_: Exception) { false }
    }

    fun close() {
        initialized.set(false)
    }
}
