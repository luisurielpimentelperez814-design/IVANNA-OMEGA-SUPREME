package com.ivanna.omega.magisk

import com.ivanna.omega.saf.SaFRoomBridge

import android.content.Context
import android.net.LocalSocket
import android.net.LocalSocketAddress
import android.os.Build
import android.os.ParcelFileDescriptor
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
 * FIX 2026-08-10 (el SHM "nunca conectaba"): esta clase creaba su propia
 * region con SharedMemory.create() — una region PRIVADA del proceso app, sin
 * ninguna relacion con /data/adb/ivanna_omega/omega_shm del daemon. Los dos
 * lados escribian en memorias distintas, asi que readAndApplySafFrame() jamas
 * veia un frame valido. Ahora initialize() pide primero el fd real al daemon
 * (handshake SCM_RIGHTS de ivanna_daemon.cpp "Modo B") y solo cae a la region
 * local cuando no hay daemon (modo sin root).
 *
 * Tamano: 64 KiB, el mismo que declara daemon/core/shm_manager.h (SHM_SIZE).
 */
object ShmManager {
    private const val TAG = "IVANNA-SHM"
    private const val SHM_NAME = "ivanna_omega_hyperplane"

    // FIX: 4096 era una suposicion ("no hay contrato de tamano documentado").
    // Si lo hay: daemon/core/shm_manager.h declara `SHM_SIZE = 65536`
    // (64 KiB = 16 UnifiedControlFrames). Mapear 4 KiB contra una region de
    // 64 KiB dejaba 15/16 del hyperplane invisible para la app.
    private const val SHM_SIZE_BYTES = 65536

    private const val DAEMON_SOCKET = "omega_daemon_socket"
    private const val HANDSHAKE_TIMEOUT_MS = 1500

    private external fun nativeMlockBuffer(buffer: ByteBuffer): Int
    private external fun nativeMapSharedFd(fd: Int, size: Int): ByteBuffer?
    private external fun nativeUnmapSharedFd(buffer: ByteBuffer): Int

    private val loaded = NativeLibraryLoader.ensureLoaded()
    private val initialized = AtomicBoolean(false)

    @Volatile private var sharedMemory: SharedMemory? = null
    @Volatile private var mappedBuffer: ByteBuffer? = null
    @Volatile private var mappedFromDaemon = false

    /** Región mapeada, o null si initialize() no se llamó o falló. */
    val buffer: ByteBuffer? get() = mappedBuffer

    /** true si la memoria está mapeada y mlock() tuvo éxito. */
    val isReady: Boolean get() = initialized.get() && mappedBuffer != null

    /**
     * true cuando la region mapeada es LA DEL DAEMON (fd recibido por
     * SCM_RIGHTS). false = region local aislada (modo sin root): la app
     * funciona, pero no hay telemetria del daemon que leer.
     */
    val isSharedWithDaemon: Boolean get() = mappedFromDaemon

    fun initialize(ctx: Context) {
        if (!initialized.compareAndSet(false, true)) {
            Log.d(TAG, "initialize() ignorado: ya inicializado")
            return
        }
        if (!loaded) {
            Log.e(TAG, "libivanna_omega.so no cargada; SHM deshabilitada")
            initialized.set(false)
            return
        }

        // ── 1) Camino real: pedir el fd del omega_shm al daemon ───────────────
        // El daemon (ivanna_daemon.cpp, "Modo B") entrega el fd de
        // /data/adb/ivanna_omega/omega_shm por SCM_RIGHTS a todo cliente que
        // conecta y NO envia bytes durante 150 ms. Nadie en Kotlin lo pedia:
        // por eso el hyperplane estaba muerto aunque el daemon lo publicara.
        val daemonBuf = runCatching { mapFromDaemon() }.getOrNull()
        if (daemonBuf != null) {
            mappedBuffer = daemonBuf
            mappedFromDaemon = true
            Log.i(TAG, "SHM del daemon mapeada via SCM_RIGHTS (${daemonBuf.capacity()}B)")
            return
        }
        Log.w(TAG, "Daemon no entrego el fd de omega_shm — fallback a region local")

        // ── 2) Fallback sin root: region local propia ─────────────────────────
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O_MR1) {
            Log.w(TAG, "android.os.SharedMemory requiere API 27+; SHM deshabilitada")
            initialized.set(false)
            return
        }
        try {
            val shm = SharedMemory.create(SHM_NAME, SHM_SIZE_BYTES)
            val buf = shm.mapReadWrite()
            if (!buf.isDirect) {
                Log.e(TAG, "mapReadWrite() devolvió un buffer no-direct; abortando mlock")
                shm.close()
                initialized.set(false)
                return
            }
            val mlockResult = nativeMlockBuffer(buf)
            if (mlockResult != 0) {
                Log.w(TAG, "mlock() falló (ret=$mlockResult) — región puede paginarse")
            }
            sharedMemory = shm
            mappedBuffer = buf
            mappedFromDaemon = false
            Log.i(TAG, "SHM local '$SHM_NAME' mapeada (${SHM_SIZE_BYTES}B), mlock=${mlockResult == 0}")
        } catch (e: Exception) {
            Log.e(TAG, "Fallo creando/mapeando SharedMemory: ${e.message}", e)
            initialized.set(false)
        }
    }

    /**
     * Conecta al socket abstracto del daemon, guarda silencio para caer en el
     * handshake "Modo B", y mapea el fd recibido por SCM_RIGHTS.
     * Devuelve null si el daemon no esta, si SELinux niega connectto, o si el
     * mmap falla.
     */
    private fun mapFromDaemon(): ByteBuffer? {
        val sock = LocalSocket()
        try {
            sock.soTimeout = HANDSHAKE_TIMEOUT_MS
            sock.connect(
                LocalSocketAddress(DAEMON_SOCKET, LocalSocketAddress.Namespace.ABSTRACT)
            )
            // No escribir NADA: el daemon clasifica como cliente SHM (Modo B)
            // exactamente a quien calla durante 150 ms; si mandamos un byte
            // entra al parser de texto y nunca envia el fd.
            val one = ByteArray(1)
            val n = sock.inputStream.read(one)
            val fds = sock.ancillaryFileDescriptors
            if (n <= 0 || fds == null || fds.isEmpty() || fds[0] == null) {
                Log.w(TAG, "handshake SHM sin fd (n=$n, fds=${fds?.size ?: 0})")
                return null
            }
            // detachFd() transfiere la propiedad del fd al codigo nativo:
            // nativeMapSharedFd() hace close() tras el mmap.
            val pfd = ParcelFileDescriptor.dup(fds[0])
            val rawFd = pfd.detachFd()
            val buf = nativeMapSharedFd(rawFd, SHM_SIZE_BYTES)
            if (buf == null) {
                Log.e(TAG, "mmap del fd del daemon fallo (¿falta regla SELinux adb_data_file?)")
            }
            return buf
        } catch (t: Throwable) {
            Log.d(TAG, "mapFromDaemon: ${t.message}")
            return null
        } finally {
            runCatching { sock.close() }
        }
    }

    /** Libera la región. Llamar desde el ciclo de vida de la app (onDestroy). */
    fun release() {
        val buf = mappedBuffer
        mappedBuffer = null
        if (mappedFromDaemon && buf != null) {
            runCatching { nativeUnmapSharedFd(buf) }
        }
        mappedFromDaemon = false
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
        // Sin fd del daemon la region es local y siempre esta en ceros: leerla
        // solo gastaria ciclos a 10 Hz.
        if (!mappedFromDaemon) return false
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
