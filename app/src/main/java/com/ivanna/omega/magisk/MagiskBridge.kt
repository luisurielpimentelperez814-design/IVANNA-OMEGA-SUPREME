package com.ivanna.omega.magisk

import android.util.Log
import java.io.BufferedReader
import java.io.File
import java.io.InputStreamReader
import java.util.concurrent.TimeUnit

/**
 * MagiskBridge v2.1 (PATCH)
 *
 * Cambios clave respecto a v2.0:
 *  - isModuleActive lee /system prop SIN pasar por `su -c` (usa
 *    android.os.SystemProperties vía reflection — no requiere root).
 *  - isDaemonRunning ahora comprueba primero el prop `persist.ivanna.daemon_active`
 *    que setea service.sh, y solo cae a probe con `su -c test -e` cacheado 2s.
 *  - Cache de 2 segundos en getters bloqueantes → panel Magisk no dispara
 *    diálogo de superusuario en cada tick.
 *  - `exec()` se reduce al mínimo (solo lo que requiere root real).
 */
object MagiskBridge {
    private const val TAG = "MagiskBridge"

    private const val SOCKET_OMEGA   = "/dev/socket/ivanna_omega"
    private const val SOCKET_LEGACY  = "/data/pf/pf.sock"
    private const val PROP_ACTIVE    = "persist.ivanna.magisk_active"
    private const val PROP_VERSION   = "persist.ivanna.version"
    private const val PROP_DAEMON    = "persist.ivanna.daemon_active"
    private const val PROP_CONCERT   = "ivanna.concert_mode"

    private const val TIMEOUT_MS     = 3000L
    private const val CACHE_TTL_MS   = 2000L

    // ── Cache ligero para evitar `su` en cada frame de Compose ────────────────
    private data class Cached(val value: String, val stamp: Long)
    private val propCache = HashMap<String, Cached>()

    private fun getPropCached(key: String): String {
        val now = System.currentTimeMillis()
        val c = propCache[key]
        if (c != null && now - c.stamp < CACHE_TTL_MS) return c.value
        val v = readSystemPropNoRoot(key)
        propCache[key] = Cached(v, now)
        return v
    }

    /** Lee un system property sin root usando reflection sobre SystemProperties. */
    private fun readSystemPropNoRoot(key: String): String = try {
        val cls = Class.forName("android.os.SystemProperties")
        val m = cls.getMethod("get", String::class.java, String::class.java)
        (m.invoke(null, key, "") as? String).orEmpty()
    } catch (t: Throwable) {
        Log.w(TAG, "SystemProperties.get($key) falló: ${t.message}")
        ""
    }

    // ── Detección del módulo ──────────────────────────────────────────────────

    val isModuleActive: Boolean
        get() = getPropCached(PROP_ACTIVE) == "1"

    val moduleVersion: String
        get() = getPropCached(PROP_VERSION).ifEmpty { "unknown" }

    /**
     * True si el daemon nativo está vivo. Primero mira el prop que setea
     * service.sh (rápido, sin root). Si el prop no está seteado pero el
     * socket existe, también cuenta como vivo.
     */
    val isDaemonRunning: Boolean
        get() {
            if (getPropCached(PROP_DAEMON) == "1") return true
            return try { File(SOCKET_OMEGA).exists() } catch (_: Throwable) { false }
        }

    // ── Comunicación con el daemon ────────────────────────────────────────────

    fun sendCommand(command: String): String {
        val socket = when {
            File(SOCKET_OMEGA).exists()  -> SOCKET_OMEGA
            File(SOCKET_LEGACY).exists() -> SOCKET_LEGACY
            else -> {
                setSystemProp("ivanna.pending_cmd", command)
                Log.w(TAG, "Daemon no conectado — cmd encolado via setprop: $command")
                return "queued"
            }
        }
        return try {
            val p = exec("echo -n '$command' | nc -U $socket")
            Log.d(TAG, "CMD=$command SOCKET=$socket RESP=${p.result}")
            p.result
        } catch (e: Exception) {
            Log.e(TAG, "Error enviando $command", e)
            ""
        }
    }

    // ── Presets ───────────────────────────────────────────────────────────────

    fun setPreset(name: String)         = sendCommand("SET_PRESET:$name")
    fun getStatus(): String             = sendCommand("STATUS")
    fun getTelemetry(): String          = sendCommand("GET_TELEMETRY")
    fun reloadParams()                  = sendCommand("RELOAD_PARAMS")
    fun setBypass(bypass: Boolean)      = sendCommand("SET_BYPASS:${if (bypass) 1 else 0}")

    fun setDrive(v: Float)     = sendCommand("SET_PF_DRIVE:$v")
    fun setWet(v: Float)       = sendCommand("SET_PF_WET:$v")
    fun setMix(v: Float)       = sendCommand("SET_PF_MIX:$v")
    fun setAlpha(v: Float)     = sendCommand("SET_PF_ALPHA:$v")
    fun setBeta(v: Float)      = sendCommand("SET_PF_BETA:$v")
    fun setGamma(v: Float)     = sendCommand("SET_PF_GAMMA:$v")
    fun setFreq(v: Float)      = sendCommand("SET_PF_FREQ:$v")
    fun setResonance(v: Float) = sendCommand("SET_PF_RESONANCE:$v")
    fun setLow(v: Float)       = sendCommand("SET_PF_LOW:$v")
    fun setMid(v: Float)       = sendCommand("SET_PF_MID:$v")
    fun setHigh(v: Float)      = sendCommand("SET_PF_HIGH:$v")
    fun setPresence(v: Float)  = sendCommand("SET_PF_PRESENCE:$v")
    fun setMaster(v: Float)    = sendCommand("SET_PF_MASTER:$v")

    // ── Modo Concierto ────────────────────────────────────────────────────────

    fun setConcertMode(enabled: Boolean) {
        if (enabled) {
            setPreset("Spatial"); sendCommand("SET_REVERB:0.7"); setSystemProp(PROP_CONCERT, "1")
        } else {
            setPreset("Warm");    sendCommand("SET_REVERB:0.0"); setSystemProp(PROP_CONCERT, "0")
        }
        Log.i(TAG, "ConcertMode → $enabled")
    }

    val isConcertModeActive: Boolean
        get() = getPropCached(PROP_CONCERT) == "1"

    // ── Helpers internos ──────────────────────────────────────────────────────

    private data class ProcResult(val result: String, val exitCode: Int)

    /**
     * Solo se usa cuando de verdad hace falta root (mandar comandos por
     * `nc -U` al socket que vive en /dev/socket con perms root). Los reads
     * de estado usan readSystemPropNoRoot() y File.exists() sin `su`.
     */
    private fun exec(cmd: String): ProcResult {
        val process = Runtime.getRuntime().exec(arrayOf("su", "-c", cmd))
        val reader  = BufferedReader(InputStreamReader(process.inputStream))
        val output  = reader.readText().trim()
        val exited  = process.waitFor(TIMEOUT_MS, TimeUnit.MILLISECONDS)
        if (!exited) process.destroyForcibly()
        return ProcResult(output, if (exited) process.exitValue() else -1)
    }

    private fun setSystemProp(key: String, value: String) {
        try { exec("setprop $key $value") }
        catch (e: Exception) { Log.w(TAG, "setprop $key=$value falló") }
    }
}
