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
 *   ivanna_daemon publica en el abstract namespace de Linux
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
 *     ni `nc`, en abstract namespace.
 *   - Lectura de props sin root (SystemProperties reflection) se mantiene.
 *   - Cache de estados anti-thrashing se mantiene.
 */
object MagiskBridge {

    private const val TAG = "IVANNA-MagiskBridge"

    private const val SOCKET_OMEGA = "omega_daemon_socket"

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

    // ── Diagnóstico root del socket (última respuesta visible, no genérica) ─
    /**
     * Diagnóstico real del socket con root. El mensaje "Socket no disponible
     * (daemon offline o módulo no instalado)" no distinguía nada. Esto
     * separa las 3 causas reales:
     *   1. socket ausente en /proc/net/unix → el daemon NUNCA bindeó
     *      (binario faltante/crash al arrancar) → revisar daemon.log.
     *   2. socket presente pero connect() falla → SELinux denegando
     *      connectto (untrusted_app → dominio del daemon) → sepolicy.
     *   3. sin root → la app no puede ni leer /proc/net/unix con verdad.
     * Devuelve una línea de diagnóstico legible para el panel.
     */
    fun diagnoseSocket(): String {
        // Primero el probe sin root (camino feliz)
        if (isOmegaSocketAvailable()) return "OK: socket conecta sin root"

        // FIX (falso diagnóstico): antes se declaraba "nunca bindeó" mirando
        // SOLO /proc/net/unix — pero el daemon también abre un fallback TCP en
        // 127.0.0.1:12121 (--tcp-port en service.sh) precisamente para ROMs
        // donde SELinux bloquea el abstracto. Si el TCP responde, el daemon
        // está vivo: el problema es SELinux sobre el abstracto, no el binario.
        val tcpOk = runCatching {
            java.net.Socket().use { s ->
                s.connect(java.net.InetSocketAddress("127.0.0.1", 12121), 1500)
                true
            }
        }.getOrDefault(false)
        if (tcpOk) {
            everConnected = true
            return "OK parcial: daemon vivo vía TCP 127.0.0.1:12121 — el abstracto está bloqueado por SELinux. Aplica sepolicy.rule y reinicia."
        }

        // Probe root: leer /proc/net/unix buscando el abstract socket.
        // Los abstract sockets aparecen como "@omega_daemon_socket" (el @ es
        // el byte NUL visualizado). grep -a porque el archivo tiene NULs.
        val listed = runCatching {
            val p = ProcessBuilder("su", "-c",
                "grep -a omega_daemon_socket /proc/net/unix; echo EXIT:\$?; tail -5 /data/adb/ivanna_omega/daemon.log 2>/dev/null")
                .redirectErrorStream(true).start()
            val out = p.inputStream.bufferedReader().readText()
            p.waitFor()
            out
        }.getOrElse { return "FALLO: probe root imposible (${it.javaClass.simpleName}) — ¿su concedido?" }

        return when {
            listed.contains("omega_daemon_socket") ->
                "FALLO: socket existe (daemon bindeó) pero connect() es rechazado → SELinux denial (connectto). Aplica sepolicy v2.0 y reinicia."
            listed.contains("bind(") ->
                "FALLO: el daemon arrancó pero bind() falló — últimas líneas del log:\n${listed.substringAfter("EXIT:1").take(200)}"
            listed.contains("EXIT:1") || listed.isBlank() ->
                "FALLO: socket ausente en /proc/net/unix → el daemon nunca bindeó (crash al arrancar o binario faltante). Revisa /data/adb/ivanna_omega/daemon.log y reinstala el módulo v2.3.0."
            else ->
                "FALLO: diagnóstico root sin salida útil: ${listed.take(120)}"
        }
    }

    // ── Envío de comandos (SIN root, LocalSocket directo) ──────────────────
    /**
     * Envía un comando de texto al daemon (abstract @omega_daemon_socket) y devuelve la respuesta.
     * Retorna "" si el daemon no está disponible. Sin dependencia de `nc` ni de `su`.
     */
    fun sendCommand(command: String): String {
        // Abstract namespace — donde publica ivanna_daemon
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

        // Daemon offline: retorna "" (sentinel) para que callers usen isEmpty().
        Log.w(TAG, "Daemon offline — command NOT delivered: $command")
        return ""
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
     * Probe real de conectividad al daemon vía abstract namespace @omega_daemon_socket.
     * NO necesita fallback a filesystem — el PF Engine legacy fue removido.
     * soTimeout garantiza que connect() no se bloquee si el backlog está al límite.
     */
    private fun isOmegaSocketAvailable(): Boolean {
        val abstractOk = runCatching {
            LocalSocket().use { sock ->
                sock.soTimeout = SOCKET_READ_TIMEOUT_MS
                sock.connect(
                    LocalSocketAddress(SOCKET_OMEGA, LocalSocketAddress.Namespace.ABSTRACT)
                )
                true
            }
        }.getOrDefault(false)
        if (abstractOk) { everConnected = true; return true }
        // FIX: el daemon publica también TCP loopback 127.0.0.1:12121
        // (--tcp-port en service.sh). Sin este segundo probe, isDaemonRunning
        // devolvía false en ROMs donde SELinux bloquea el abstracto aunque el
        // daemon estuviera perfectamente vivo por TCP.
        val tcpOk = runCatching {
            java.net.Socket().use { s ->
                s.soTimeout = SOCKET_READ_TIMEOUT_MS
                s.connect(java.net.InetSocketAddress("127.0.0.1", 12121), SOCKET_READ_TIMEOUT_MS)
                true
            }
        }.getOrDefault(false)
        if (tcpOk) everConnected = true
        return tcpOk
    }
}
