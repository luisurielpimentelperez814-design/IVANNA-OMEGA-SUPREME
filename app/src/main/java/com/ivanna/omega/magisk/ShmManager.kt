package com.ivanna.omega.magisk

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
        sharedMemory = null
        initialized.set(false)
    }
}
