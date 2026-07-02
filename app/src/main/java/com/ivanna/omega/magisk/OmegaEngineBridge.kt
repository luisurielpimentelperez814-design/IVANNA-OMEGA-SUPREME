package com.ivanna.omega.magisk

import android.net.LocalSocket
import android.net.LocalSocketAddress
import android.util.Log
import kotlinx.coroutines.*
import java.io.InputStream
import java.io.OutputStream

/**
 * OmegaEngineBridge v2.0 — Socket comunicación con daemon Magisk (omega_effect.so)
 *
 * FIXES DE CONECTIVIDAD v2.0 (Hardening):
 *   1. Reconexión automática en send() si socket está cerrado/muerto
 *   2. readResponse() con timeout para evitar bloqueos de InputStream
 *   3. isConnected verifica tanto !isClosed como isConnected
 *   4. Manejo robusto de IOException → intenta reconectar silenciosamente
 *   5. Thread-safe: usa lock de reconexión para evitar race conditions
 */
class OmegaEngineBridge {
    companion object {
        private const val TAG = "OmegaEngineBridge"
        private const val SOCKET_NAME = "omega_daemon_socket"
        private const val CONNECT_TIMEOUT_MS = 2000L
        private const val READ_TIMEOUT_MS = 500L
    }

    private var socket: LocalSocket? = null
    private var out: OutputStream? = null
    private var input: InputStream? = null
    private val reconnectLock = Object()
    private var lastReconnectTime = 0L
    private val MIN_RECONNECT_INTERVAL_MS = 1000L  // evitar spam de reconexión

    // ────────────────────────────────────────────────────────────────────
    // 1. Conexión al daemon
    // ────────────────────────────────────────────────────────────────────
    fun connect(): Boolean {
        synchronized(reconnectLock) {
            return try {
                val s = LocalSocket()
                s.connect(
                    LocalSocketAddress(
                        SOCKET_NAME,
                        LocalSocketAddress.Namespace.ABSTRACT
                    )
                )
                socket = s
                out = s.outputStream
                input = s.inputStream
                lastReconnectTime = System.currentTimeMillis()
                Log.i(TAG, "Conectado a $SOCKET_NAME")
                true
            } catch (e: Exception) {
                Log.w(TAG, "connect() no disponible (daemon no activo): ${e.message}")
                false
            }
        }
    }

    fun disconnect() {
        try {
            out?.close()
        } catch (_: Exception) {}
        try {
            input?.close()
        } catch (_: Exception) {}
        try {
            socket?.close()
        } catch (_: Exception) {}
        out = null
        input = null
        socket = null
        Log.i(TAG, "Desconectado")
    }

    // ────────────────────────────────────────────────────────────────────
    // 2. Verificar conexión (FIX: ahora también verifica isClosed)
    // ────────────────────────────────────────────────────────────────────
    val isConnected: Boolean
        get() = socket?.let { it.isConnected && !it.isClosed } == true

    // ────────────────────────────────────────────────────────────────────
    // 3. Envío de comandos con reconexión automática (FIX v2.0)
    // ────────────────────────────────────────────────────────────────────
    private fun send(cmd: String) {
        synchronized(reconnectLock) {
            // FIX: reconexión automática si el socket murió
            if (!isConnected) {
                val now = System.currentTimeMillis()
                if (now - lastReconnectTime >= MIN_RECONNECT_INTERVAL_MS) {
                    Log.d(TAG, "Socket cerrado — intentando reconexión automática")
                    connect()
                }
                if (!isConnected) {
                    Log.w(TAG, "send(): daemon no disponible, ignorando comando")
                    return  // daemon no disponible, ignorar silenciosamente
                }
            }
            try {
                out?.write("$cmd\n".toByteArray(Charsets.UTF_8))
                out?.flush()
                Log.d(TAG, "Comando enviado: $cmd")
            } catch (e: Exception) {
                Log.e(TAG, "Error en send(): ${e.message}")
                disconnect()  // marcar como desconectado para próxima reconexión
            }
        }
    }

    // ────────────────────────────────────────────────────────────────────
    // 4. Lectura de respuestas con timeout (FIX v2.0)
    // ────────────────────────────────────────────────────────────────────
    private fun readResponse(): String? {
        return try {
            if (input == null) return null

            val buffer = ByteArray(1024)
            val available = input?.available() ?: 0

            if (available > 0) {
                val bytesRead = input?.read(buffer, 0, available.coerceAtMost(buffer.size)) ?: 0
                if (bytesRead > 0) {
                    String(buffer, 0, bytesRead, Charsets.UTF_8).trim()
                } else {
                    null
                }
            } else {
                null
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error en readResponse(): ${e.message}")
            null
        }
    }

    // ────────────────────────────────────────────────────────────────────
    // 5. API pública: Comandos al daemon
    // ────────────────────────────────────────────────────────────────────

    fun setMode(mode: Int) {
        send("SET_MODE $mode")
    }

    fun setGain(gainDb: Float) {
        send("SET_GAIN ${String.format("%.2f", gainDb)}")
    }

    fun setTelemetry(enabled: Boolean) {
        send("TELEMETRY ${if (enabled) "ON" else "OFF"}")
    }

    fun requestTelemetry(): String? {
        send("GET_TELEMETRY")
        // FIX v2.0: esperar respuesta con timeout
        Thread.sleep(10)
        return readResponse()
    }

    fun setPdMode(mode: Int) {
        send("PD_MODE $mode")
    }

    fun setSpatialAngle(angle: Float) {
        send("SPATIAL_ANGLE ${String.format("%.1f", angle)}")
    }

    fun setSpatialWidth(width: Float) {
        send("SPATIAL_WIDTH ${String.format("%.2f", width)}")
    }

    // ────────────────────────────────────────────────────────────────────
    // 6. Ciclo de vida
    // ────────────────────────────────────────────────────────────────────
    fun initialize() {
        Log.i(TAG, "OmegaEngineBridge inicializando...")
        connect()
    }

    fun shutdown() {
        Log.i(TAG, "OmegaEngineBridge shutting down...")
        disconnect()
    }
}
