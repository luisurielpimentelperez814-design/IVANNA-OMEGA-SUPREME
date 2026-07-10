package com.ivanna.omega.magisk

import android.net.LocalSocket
import android.net.LocalSocketAddress
import android.util.Log
import java.io.InputStream
import java.io.OutputStream

/**
 * OmegaEngineBridge — Cliente Unix socket para el daemon C++.
 *
 * FIXES DE CONECTIVIDAD:
 *   1. reconnect(): si el socket se cierra inesperadamente (daemon reiniciado
 *      por Magisk), se intenta reconexión automática antes de cada send().
 *      Antes un socket cerrado producía IOException silenciosa → comandos perdidos.
 *   2. readResponse(): nuevo método para leer la respuesta del daemon (GET_TELEMETRY)
 *      con timeout (sin él la app se bloqueaba indefinidamente).
 *   3. isConnected: verifica también isClosed (LocalSocket puede reportar
 *      isConnected=true después de que el servidor cierra su extremo).
 */
class OmegaEngineBridge {

    private val TAG = "OmegaEngineBridge"
    private val SOCKET_NAME = "omega_daemon_socket"
    private val CONNECT_TIMEOUT_MS = 2000L

    private var socket: LocalSocket? = null
    private var out: OutputStream? = null
    private var input: InputStream? = null

    // ── Ciclo de vida ─────────────────────────────────────────────────────────

    fun connect(): Boolean {
        return try {
            val s = LocalSocket()
            s.connect(LocalSocketAddress(SOCKET_NAME, LocalSocketAddress.Namespace.ABSTRACT))
            socket = s
            out = s.outputStream
            input = s.inputStream
            Log.i(TAG, "Conectado a $SOCKET_NAME")
            true
        } catch (e: Exception) {
            Log.w(TAG, "connect() no disponible (daemon no activo): ${e.message}")
            false
        }
    }

    fun disconnect() {
        try { out?.close() } catch (_: Exception) {}
        try { input?.close() } catch (_: Exception) {}
        try { socket?.close() } catch (_: Exception) {}
        out = null; input = null; socket = null
        Log.i(TAG, "Desconectado")
    }

    // FIX: isConnected ahora también verifica isClosed
    val isConnected: Boolean
        get() = socket?.let { it.isConnected && !it.isClosed } == true

    // ── Envío de comandos ─────────────────────────────────────────────────────

    private fun send(cmd: String) {
        // FIX: reconexión automática si el socket murió
        if (!isConnected) {
            Log.d(TAG, "Socket cerrado — intentando reconexión")
            connect()
            if (!isConnected) return  // daemon no disponible, ignorar silenciosamente
        }
        try {
            out?.write("$cmd\n".toByteArray(Charsets.UTF_8))
            out?.flush()
        } catch (e: Exception) {
            Log.w(TAG, "send('$cmd') falló: ${e.message} — desconectando")
            disconnect()
        }
    }

    /**
     * FIX: lee la respuesta del daemon para comandos como GET_TELEMETRY.
     * Antes no había ninguna lectura → InputStream se llenaba y bloqueaba.
     */
    fun readResponse(timeoutMs: Long = 500L): String {
        if (!isConnected) return ""
        return try {
            val deadline = System.currentTimeMillis() + timeoutMs
            val sb = StringBuilder()
            while (System.currentTimeMillis() < deadline) {
                if ((input?.available() ?: 0) > 0) {
                    val b = input!!.read()
                    if (b == -1 || b == '\n'.code) break
                    sb.append(b.toChar())
                } else {
                    Thread.sleep(10)
                }
            }
            sb.toString()
        } catch (e: Exception) {
            Log.w(TAG, "readResponse error: ${e.message}")
            ""
        }
    }

    // ── Control básico ────────────────────────────────────────────────────────

    fun setProcessing(enabled: Boolean) = send("SET_PROCESSING:${if (enabled) 1 else 0}")
    fun setIntensity(v: Float)          = send("SET_INTENSITY:$v")
    fun setVocoderMix(v: Float)         = send("SET_VOCODER_MIX:$v")
    fun setBypass(enabled: Boolean)     = send("SET_BYPASS:${if (enabled) 1 else 0}")
    fun resetDefaults()                 = send("RESET_DEFAULTS")

    // ── AI adaptativa ─────────────────────────────────────────────────────────

    fun setAiEnabled(enabled: Boolean) =
        send("SET_AI_ENABLED:${if (enabled) 1 else 0}")

    fun setAiAutoAdapt(enabled: Boolean) =
        send("SET_AI_AUTO_ADAPT:${if (enabled) 1 else 0}")

    fun setAiSensitivity(v: Float) =
        send("SET_AI_SENSITIVITY:${v.coerceIn(0f, 1f)}")

    // ── PF Engine ─────────────────────────────────────────────────────────────

    fun setPFDrive(v: Float)      = send("SET_PF_DRIVE:$v")
    fun setPFWet(v: Float)        = send("SET_PF_WET:$v")
    fun setPFMix(v: Float)        = send("SET_PF_MIX:$v")
    fun setPFAlpha(v: Float)      = send("SET_PF_ALPHA:$v")
    fun setPFBeta(v: Float)       = send("SET_PF_BETA:$v")
    fun setPFGamma(v: Float)      = send("SET_PF_GAMMA:$v")
    fun setPFFreq(v: Float)       = send("SET_PF_FREQ:$v")
    fun setPFResonance(v: Float)  = send("SET_PF_RESONANCE:$v")
    fun setPFLow(v: Float)        = send("SET_PF_LOW:$v")
    fun setPFMid(v: Float)        = send("SET_PF_MID:$v")
    fun setPFHigh(v: Float)       = send("SET_PF_HIGH:$v")
    fun setPFPresence(v: Float)   = send("SET_PF_PRESENCE:$v")
    fun setPFMaster(v: Float)     = send("SET_PF_MASTER:$v")

    // FIX: DSPState.pushToNative() llama a setPFParams(...) como batch de
    // los 13 parámetros del PF Engine, pero solo existían los setters
    // individuales de arriba (uno por comando de socket) — rompía el build
    // (compileDebugKotlin: Unresolved reference). No hay comando de socket
    // batch en el daemon (omega_daemon.cpp solo parsea SET_PF_* uno por
    // uno), así que esto simplemente encadena los 13 setters ya probados.
    fun setPFParams(
        drive: Float, wet: Float, mix: Float,
        alpha: Float, beta: Float, gamma: Float,
        freq: Float, resonance: Float,
        low: Float, mid: Float, high: Float, presence: Float, master: Float
    ) {
        setPFDrive(drive)
        setPFWet(wet)
        setPFMix(mix)
        setPFAlpha(alpha)
        setPFBeta(beta)
        setPFGamma(gamma)
        setPFFreq(freq)
        setPFResonance(resonance)
        setPFLow(low)
        setPFMid(mid)
        setPFHigh(high)
        setPFPresence(presence)
        setPFMaster(master)
    }

    fun requestTelemetry(): String {
        send("GET_TELEMETRY")
        return readResponse()
    }
}
