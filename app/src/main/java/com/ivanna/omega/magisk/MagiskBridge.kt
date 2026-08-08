package com.ivanna.omega.magisk

import android.net.LocalSocket
import android.net.LocalSocketAddress
import android.util.Log
import java.io.File

/**
 * MagiskBridge v2.2 — Cableado real end-to-end con abstract socket
 *
 * FIX (socket no aparece encendido, pieza 3 de N):
 *   La versión anterior detectaba el daemon con
 *   File("/dev/socket/omega_daemon_socket").exists() — ese path es del
 *   filesystem y NUNCA existe porque el daemon (ivanna_daemon /
 *   OmegaDaemonV8) publica ahora en el abstract namespace de Linux
 *   ("@omega_daemon_socket"), donde no hay archivo. Por eso
 *   isDaemonRunning devolvía false y el panel Magisk siempre marcaba
 *   OFFLINE, aunque persist.ivanna.daemon_active=1.
 *
 *   Y sendCommand() usaba `nc -U <path>` con la ruta del filesystem,
 *   que además de fallar por el punto anterior necesitaba `su` para
 *   correr — negando el objetivo de "socket sin root".
 *
 * v2.2:
 *   - isOmegaSocketAvailable() ahora hace probe real con LocalSocket
 *     en Namespace.ABSTRACT contra "omega_daemon_socket", igual que
 *     hacen OmegaEngineBridge.probeSocket() y OmegaEngineBridge.sendCommand().
 *   - sendCommand() escribe directamente al LocalSocket sin invocar `su`
 *     ni `nc`, en abstract namespace. Fallback a /data/pf/pf.sock por
 *     filesystem si el abstract no responde.
 *   - Lectura de props sin root (SystemProperties reflection) se mantiene.
 *   - Cache de estados anti-thrashing se mantiene.
 */
object MagiskBridge {

    private const val TAG = "IVANNA-MagiskBridge"

    private const val SOCKET_OMEGA = "omega_daemon_socket"
    private const val SOCKET_LEGACY = "/data/pf/pf.sock"

    private const val PROP_ACTIVE = "persist.ivanna.magisk_active"
    private const val PROP_VERSION = "persist.ivanna.version"
    private const val PROP_DAEMON = "persist.ivanna.daemon_active"
    // PROP_CONCERT eliminado — estado gestionado en memoria con _concertModeActive

    private const val CACHE_TTL_MS = 2000L
    private const val SOCKET_READ_TIMEOUT_MS = 500

    private data class Cached(val value: String, val stamp: Long)

    private val propCache = HashMap<String, Cached>()

    private fun getPropCached(key: String): String {
        val now = System.currentTimeMillis()
        val cached = propCache[key]
        if (cached != null && now - cached.stamp < CACHE_TTL_MS) {
            return cached.value
        }
        val value = readSystemPropNoRoot(key)
        propCache[key] = Cached(value, now)
        return value
    }

    private fun readSystemPropNoRoot(key: String): String {
        return try {
            val cls = Class.forName("android.os.SystemProperties")
            val method = cls.getMethod("get", String::class.java, String::class.java)
            (method.invoke(null, key, "") as? String).orEmpty()
        } catch (t: Throwable) {
            Log.w(TAG, "SystemProperties error $key: ${t.message}")
            ""
        }
    }

    val isModuleActive: Boolean
        get() = getPropCached(PROP_ACTIVE) == "1"

    val moduleVersion: String
        get() = getPropCached(PROP_VERSION).ifEmpty { "unknown" }

    /**
     * Daemon vivo si:
     *   1) persist.ivanna.daemon_active=1 (setprop desde service.sh), O
     *   2) probe real al abstract socket responde.
     */
    val isDaemonRunning: Boolean
        get() {
            // FIX: antes la property de sistema ganaba y cortocircuitaba el probe.
            // persist.ivanna.daemon_active es *persistente*: si el daemon muere
            // (crash, kill, modulo desactivado) nadie la baja hasta el siguiente
            // arranque de service.sh, asi que la UI mostraba ONLINE con el socket
            // muerto y todos los comandos cayendo en "queued". El unico testigo
            // fiable de "puedo hablar con el daemon" es el connect() real; la
            // prop queda como pista secundaria cuando el probe falla por SELinux.
            val socketOk = try { isOmegaSocketAvailable() } catch (_: Throwable) { false }
            if (socketOk) return true
            return getPropCached(PROP_DAEMON) == "1" && propFallbackAllowed
        }

    /**
     * Solo confiamos en la prop cuando el probe nunca ha funcionado en esta
     * sesion (posible bloqueo SELinux de connectto). Si alguna vez conectamos y
     * ahora falla, el daemon esta realmente caido: no mentimos a la UI.
     */
    @Volatile private var everConnected = false
    private val propFallbackAllowed: Boolean get() = !everConnected

    // ── Envío de comandos (SIN root, LocalSocket directo) ──────────────────
    /**
     * Envía un comando de texto al daemon y devuelve la respuesta.
     * Prueba abstract namespace primero (@omega_daemon_socket), fallback
     * a /data/pf/pf.sock por filesystem si el abstract no responde.
     * Sin dependencia de `nc` ni de `su`.
     */
    fun sendCommand(command: String): String {
        // 1) Abstract namespace — donde publica ivanna_daemon / OmegaDaemonV8
        runCatching {
            LocalSocket().use { sock ->
                sock.soTimeout = SOCKET_READ_TIMEOUT_MS
                sock.connect(
                    LocalSocketAddress(SOCKET_OMEGA, LocalSocketAddress.Namespace.ABSTRACT)
                )
                sock.outputStream.apply {
                    write(command.toByteArray(Charsets.UTF_8))
                    write("\n".toByteArray(Charsets.UTF_8))
                    flush()
                }
                val buf = ByteArray(1024)
                val n = runCatching { sock.inputStream.read(buf) }.getOrDefault(-1)
                return if (n > 0) String(buf, 0, n, Charsets.UTF_8).trim() else "ACK"
            }
        }.onFailure { Log.d(TAG, "abstract socket send failed: ${it.message}") }

        // 2) Fallback filesystem legacy
        if (File(SOCKET_LEGACY).exists()) {
            runCatching {
                LocalSocket().use { sock ->
                    sock.soTimeout = SOCKET_READ_TIMEOUT_MS
                    sock.connect(
                        LocalSocketAddress(SOCKET_LEGACY, LocalSocketAddress.Namespace.FILESYSTEM)
                    )
                    sock.outputStream.apply {
                        write(command.toByteArray(Charsets.UTF_8))
                        write("\n".toByteArray(Charsets.UTF_8))
                        flush()
                    }
                    val buf = ByteArray(1024)
                    val n = runCatching { sock.inputStream.read(buf) }.getOrDefault(-1)
                    return if (n > 0) String(buf, 0, n, Charsets.UTF_8).trim() else "ACK"
                }
            }.onFailure { Log.d(TAG, "legacy socket send failed: ${it.message}") }
        }

        // 3) Nada respondió — FIX Bug-5: se eliminó exec/setprop que tardaba 3 s y siempre fallaba
        Log.w(TAG, "Daemon offline — command dropped: $command")
        return "queued"
    }

    // Helpers extension for use{} (LocalSocket no es Closeable en API antiguo)
    private inline fun <R> LocalSocket.use(block: (LocalSocket) -> R): R {
        try { return block(this) } finally { runCatching { close() } }
    }

    fun setPreset(name: String) = sendCommand("SET_PRESET:$name")
    fun getStatus() = sendCommand("STATUS")
    fun getTelemetry() = sendCommand("GET_TELEMETRY")
    fun reloadParams() = sendCommand("RELOAD_PARAMS")
    fun setBypass(v: Boolean) = sendCommand("SET_BYPASS:${if (v) 1 else 0}")

    fun setDrive(v: Float) = sendCommand("SET_PF_DRIVE:$v")
    fun setWet(v: Float) = sendCommand("SET_PF_WET:$v")
    fun setMix(v: Float) = sendCommand("SET_PF_MIX:$v")
    fun setAlpha(v: Float) = sendCommand("SET_PF_ALPHA:$v")
    fun setBeta(v: Float) = sendCommand("SET_PF_BETA:$v")
    fun setGamma(v: Float) = sendCommand("SET_PF_GAMMA:$v")
    fun setFreq(v: Float) = sendCommand("SET_PF_FREQ:$v")
    fun setResonance(v: Float) = sendCommand("SET_PF_RESONANCE:$v")
    fun setLow(v: Float) = sendCommand("SET_PF_LOW:$v")
    fun setMid(v: Float) = sendCommand("SET_PF_MID:$v")
    fun setHigh(v: Float) = sendCommand("SET_PF_HIGH:$v")
    fun setPresence(v: Float) = sendCommand("SET_PF_PRESENCE:$v")
    fun setMaster(v: Float) = sendCommand("SET_PF_MASTER:$v")

    // FIX Bug-5: estado en memoria — setSystemProp(PROP_CONCERT) siempre fallaba desde app
    @Volatile private var _concertModeActive = false

    fun setConcertMode(enabled: Boolean) {
        if (enabled) {
            setPreset("Spatial")
            sendCommand("SET_REVERB:0.7")
        } else {
            setPreset("Warm")
            sendCommand("SET_REVERB:0.0")
        }
        _concertModeActive = enabled
        Log.i(TAG, "ConcertMode=$enabled")
    }

    val isConcertModeActive: Boolean
        get() = _concertModeActive
    // exec(), ProcResult y setSystemProp eliminados — eran dead code que añadía 3 s de latencia

    /**
     * Probe real de conectividad al daemon.
     * Prioridad:
     *   1) Abstract namespace @omega_daemon_socket (daemon actual).
     *   2) Filesystem legacy /data/pf/pf.sock (compatibilidad hacia atrás).
     * NO comprueba /dev/socket/... (los abstract sockets no viven ahí).
     */
    private fun isOmegaSocketAvailable(): Boolean {
        val abstractOk = runCatching {
            LocalSocket().use { sock ->
                sock.soTimeout = SOCKET_READ_TIMEOUT_MS   // FIX Bug-4: sin esto puede bloquearse
                sock.connect(
                    LocalSocketAddress(SOCKET_OMEGA, LocalSocketAddress.Namespace.ABSTRACT)
                )
                true
            }
        }.getOrDefault(false)
        if (abstractOk) { everConnected = true; return true }

        return runCatching {
            if (!File(SOCKET_LEGACY).exists()) return@runCatching false
            LocalSocket().use { sock ->
                sock.soTimeout = SOCKET_READ_TIMEOUT_MS   // FIX Bug-4: idem legacy
                sock.connect(
                    LocalSocketAddress(SOCKET_LEGACY, LocalSocketAddress.Namespace.FILESYSTEM)
                )
                true
            }
        }.getOrDefault(false)
    }
}
