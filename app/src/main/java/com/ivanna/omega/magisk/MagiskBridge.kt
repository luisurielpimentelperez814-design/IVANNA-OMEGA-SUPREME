package com.ivanna.omega.magisk

import android.util.Log
import java.io.BufferedReader
import java.io.InputStreamReader
import java.util.concurrent.TimeUnit

/**
 * MagiskBridge v2.0
 *
 * Canal entre la app y el módulo/daemon Magisk IVANNA.
 * Socket real:   /dev/socket/ivanna_omega  (daemon nativo)
 * Socket legacy: /data/pf/pf.sock          (compatibilidad)
 * Fallback:      setprop ivanna.pending_cmd (si daemon no está activo)
 */
object MagiskBridge {
    private const val TAG = "MagiskBridge"

    // Socket del daemon nativo (service.sh lo crea)
    private const val SOCKET_OMEGA   = "/dev/socket/ivanna_omega"
    // Socket legacy (compatibilidad con versiones anteriores)
    private const val SOCKET_LEGACY  = "/data/pf/pf.sock"
    // Propiedad seteada por post-fs-data.sh cuando el módulo está montado
    private const val PROP_ACTIVE    = "persist.ivanna.magisk_active"
    private const val PROP_VERSION   = "persist.ivanna.version"
    private const val PROP_CONCERT   = "ivanna.concert_mode"

    private const val TIMEOUT_MS     = 3000L

    // ── Detección del módulo ──────────────────────────────────────────────────

    /**
     * True si el módulo Magisk está instalado y el daemon activo.
     * Lee la propiedad de sistema seteada por post-fs-data.sh (no requiere su).
     */
    val isModuleActive: Boolean
        get() = getSystemProp(PROP_ACTIVE) == "1"

    val moduleVersion: String
        get() = getSystemProp(PROP_VERSION).ifEmpty { "unknown" }

    val isDaemonRunning: Boolean
        get() = try {
            val p = exec("test -S $SOCKET_OMEGA && echo yes")
            p.result.trim() == "yes"
        } catch (e: Exception) { false }

    // ── Comunicación con el daemon ────────────────────────────────────────────

    fun sendCommand(command: String): String {
        val socket = when {
            fileExists(SOCKET_OMEGA)  -> SOCKET_OMEGA
            fileExists(SOCKET_LEGACY) -> SOCKET_LEGACY
            else -> {
                // Fallback: setprop para que el daemon lo lea al conectar
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

    // ── Parámetros DSP (espeja OmegaEngineBridge via socket directo) ──────────

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
            setPreset("Spatial")
            sendCommand("SET_REVERB:0.7")
            setSystemProp(PROP_CONCERT, "1")
        } else {
            setPreset("Warm")
            sendCommand("SET_REVERB:0.0")
            setSystemProp(PROP_CONCERT, "0")
        }
        Log.i(TAG, "ConcertMode → $enabled")
    }

    val isConcertModeActive: Boolean
        get() = getSystemProp(PROP_CONCERT) == "1"

    // ── Helpers internos ──────────────────────────────────────────────────────

    private data class ProcResult(val result: String, val exitCode: Int)

    private fun exec(cmd: String): ProcResult {
        val process = Runtime.getRuntime().exec(arrayOf("su", "-c", cmd))
        val reader  = BufferedReader(InputStreamReader(process.inputStream))
        val output  = reader.readText().trim()
        val exited  = process.waitFor(TIMEOUT_MS, TimeUnit.MILLISECONDS)
        if (!exited) process.destroyForcibly()
        return ProcResult(output, if (exited) process.exitValue() else -1)
    }

    private fun fileExists(path: String): Boolean = try {
        exec("test -e $path && echo yes").result == "yes"
    } catch (e: Exception) { false }

    private fun getSystemProp(key: String): String = try {
        exec("getprop $key").result
    } catch (e: Exception) { "" }

    private fun setSystemProp(key: String, value: String) {
        try { exec("setprop $key $value") }
        catch (e: Exception) { Log.w(TAG, "setprop $key=$value falló") }
    }
}
