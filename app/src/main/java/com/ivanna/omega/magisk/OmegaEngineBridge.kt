package com.ivannafusion

import android.net.LocalSocket
import android.net.LocalSocketAddress
import android.util.Log
import java.io.OutputStream

/**
 * IVANNA-OMEGA-SUPREME — OmegaEngineBridge
 *
 * Cliente del socket Unix abstracto "omega_daemon_socket" que expone el
 * daemon C++ (omega_daemon.cpp) al módulo Magisk y al audioserver.
 *
 * La APK usa OmegaDaemon (JNI en-proceso) para control directo.
 * OmegaEngineBridge es el canal de texto para procesos externos
 * (el módulo Magisk / omega_effect.so) que no pueden llamar JNI.
 *
 * Protocolo: comandos de texto terminados en '\n', ej:
 *   SET_AI_ENABLED:1\n
 *   SET_PF_DRIVE:0.65\n
 *   GET_TELEMETRY\n  → responde JSON en la misma conexión
 */
class OmegaEngineBridge {

    private val TAG = "OmegaEngineBridge"
    private val SOCKET_NAME = "omega_daemon_socket"

    private var socket: LocalSocket? = null
    private var out: OutputStream? = null

    // ── Ciclo de vida ─────────────────────────────────────────────────────────

    fun connect(): Boolean {
        return try {
            val s = LocalSocket()
            s.connect(LocalSocketAddress(SOCKET_NAME, LocalSocketAddress.Namespace.ABSTRACT))
            socket = s
            out = s.outputStream
            Log.i(TAG, "Conectado a $SOCKET_NAME")
            true
        } catch (e: Exception) {
            Log.e(TAG, "connect() falló: ${e.message}")
            false
        }
    }

    fun disconnect() {
        try {
            out?.close()
            socket?.close()
        } catch (_: Exception) {}
        out = null
        socket = null
        Log.i(TAG, "Desconectado")
    }

    val isConnected: Boolean get() = socket?.isConnected == true

    // ── Envío de comandos ─────────────────────────────────────────────────────

    private fun send(cmd: String) {
        try {
            out?.write("$cmd\n".toByteArray(Charsets.UTF_8))
            out?.flush()
        } catch (e: Exception) {
            Log.w(TAG, "send('$cmd') falló: ${e.message}")
        }
    }

    // ── Control básico ────────────────────────────────────────────────────────

    fun setProcessing(enabled: Boolean) = send("SET_PROCESSING:${if (enabled) 1 else 0}")
    fun setIntensity(v: Float)          = send("SET_INTENSITY:$v")
    fun setVocoderMix(v: Float)         = send("SET_VOCODER_MIX:$v")
    fun setBypass(enabled: Boolean)     = send("SET_BYPASS:${if (enabled) 1 else 0}")
    fun resetDefaults()                 = send("RESET_DEFAULTS")

    // ── AI adaptativa ─────────────────────────────────────────────────────────

    /** Activa/desactiva el AGC en el hot path del efecto AudioFlinger. */
    fun setAiEnabled(enabled: Boolean) =
        send("SET_AI_ENABLED:${if (enabled) 1 else 0}")

    /**
     * Activa el auto-adapt: el daemon ajusta `intensity` y `bypass`
     * automáticamente según temperatura y latencia.
     */
    fun setAiAutoAdapt(enabled: Boolean) =
        send("SET_AI_AUTO_ADAPT:${if (enabled) 1 else 0}")

    /**
     * Sensibilidad del AGC y auto-adapt.
     * 0 = time-constant lento / ajuste suave.
     * 1 = time-constant rápido / ajuste agresivo.
     */
    fun setAiSensitivity(v: Float) =
        send("SET_AI_SENSITIVITY:${v.coerceIn(0f, 1f)}")

    // ── PF Engine — comandos de socket ────────────────────────────────────────

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

    fun requestTelemetry()        = send("GET_TELEMETRY")
}
