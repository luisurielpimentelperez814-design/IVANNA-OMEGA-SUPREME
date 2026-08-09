package com.ivanna.omega.magisk

import android.net.LocalSocket
import android.net.LocalSocketAddress
import android.util.Log
import org.json.JSONObject
import java.io.InputStream
import java.io.OutputStream
import java.util.concurrent.atomic.AtomicBoolean

/**
 * OmegaEngineBridge — cliente LocalSocket hacia omega_daemon_socket.
 *
 * FIX CRÍTICO (socket siempre DESCONECTADO):
 *   El connect() original era un fake: ponía isConnected=true sin
 *   intentar ningún socket real. La primera sendCommand() fallaba
 *   y lo dejaba en false permanentemente.
 *
 *   connect() ahora hace un probe real (connect/close) al socket.
 *   isConnected es @Volatile y se actualiza en cada send/probe.
 *   El reconnecting flag evita probes concurrentes.
 */
object OmegaEngineBridge {
    private const val TAG = "OmegaEngineBridge"
    private const val SOCKET_PRIMARY  = "omega_daemon_socket"
    private const val SOCKET_CONTROL  = "omega_command_socket"
    private const val SOCKET_LEGACY   = "/data/pf/pf.sock"
    private const val CONNECT_TIMEOUT = 2000 // ms

    @Volatile var isConnected = false
        private set

    @Volatile private var lastLatencyMs = 0f

    // Evita probes concurrentes desde el reconnect loop y sendCommand
    private val reconnecting = AtomicBoolean(false)

    // ── Probe real al socket ─────────────────────────────────────────────
    /**
     * Intenta abrir y cerrar el socket abstract @omega_daemon_socket.
     * Devuelve true si el daemon responde; actualiza isConnected.
     */
    fun connect(): Boolean {
        if (reconnecting.compareAndSet(false, true)) {
            try {
                return probeSocket()
            } finally {
                reconnecting.set(false)
            }
        }
        return isConnected
    }

    private fun probeSocket(): Boolean {
        // FIX: soTimeout = CONNECT_TIMEOUT en probe — el CONNECT_TIMEOUT estaba
        // definido pero nunca usado. Sin timeout, connect() podía bloquearse varios
        // segundos en ciertos kernels cuando el backlog del daemon está lleno.
        val probedPrimary = runCatching {
            val sock = LocalSocket()
            sock.soTimeout = CONNECT_TIMEOUT          // FIX Bug-1: evita bloqueo en backlog
            sock.connect(
                LocalSocketAddress(SOCKET_PRIMARY, LocalSocketAddress.Namespace.ABSTRACT)
            )
            sock.close()
            true
        }.getOrDefault(false)

        if (probedPrimary) {
            if (!isConnected) Log.i(TAG, "✅ Socket @$SOCKET_PRIMARY conectado")
            isConnected = true
            return true
        }

        // Fallback socket legacy /data/pf/pf.sock
        val probedLegacy = runCatching {
            val sock = LocalSocket()
            sock.soTimeout = CONNECT_TIMEOUT          // FIX Bug-1: idem para legacy
            sock.connect(
                LocalSocketAddress(SOCKET_LEGACY, LocalSocketAddress.Namespace.FILESYSTEM)
            )
            sock.close()
            true
        }.getOrDefault(false)

        isConnected = probedLegacy
        if (probedLegacy) Log.i(TAG, "✅ Socket legacy $SOCKET_LEGACY conectado")
        else              Log.d(TAG, "⚪ Daemon no disponible (no-root o módulo no instalado)")
        return probedLegacy
    }

    // ── Envío de comandos ────────────────────────────────────────────────
    @Synchronized
    fun sendCommand(payload: JSONObject): Boolean {
        var socket: LocalSocket? = null
        return try {
            val t0 = System.nanoTime()
            socket = LocalSocket()
            socket.soTimeout = CONNECT_TIMEOUT        // FIX Bug-2: timeout en connect + read

            // Intentar primero socket abstracto, luego legacy
            val connected = runCatching {
                socket.connect(
                    LocalSocketAddress(SOCKET_PRIMARY, LocalSocketAddress.Namespace.ABSTRACT)
                )
                true
            }.getOrElse {
                runCatching {
                    socket.connect(
                        LocalSocketAddress(SOCKET_LEGACY, LocalSocketAddress.Namespace.FILESYSTEM)
                    )
                    true
                }.getOrDefault(false)
            }

            if (!connected) {
                isConnected = false
                return false
            }

            val output: OutputStream = socket.outputStream
            val jsonBytes = payload.toString().toByteArray(Charsets.UTF_8)
            output.write(jsonBytes)
            output.flush()

            val input: InputStream = socket.inputStream
            val buffer = ByteArray(1024)              // FIX Bug-2: buffer 256→1024
            val bytesRead = input.read(buffer)
            val t1 = System.nanoTime()
            lastLatencyMs = (t1 - t0) / 1_000_000f

            isConnected = bytesRead > 0
            isConnected
        } catch (e: Exception) {
            Log.w(TAG, "sendCommand error: ${e.message}")
            isConnected = false
            false
        } finally {
            runCatching { socket?.close() }
        }
    }

    // ── Comandos de alto nivel ───────────────────────────────────────────
    fun sendPerceptualState(
        compressor: Float, exciterRed: Float, highCut: Float,
        spatialWidth: Float, loudnessTarget: Float,
        harmonicGain: Float, antiDolby: Float
    ): Boolean = sendCommand(JSONObject().apply {
        put("action", "SET_PERCEPTUAL_STATE")
        put("compressor",           compressor.toDouble())
        put("exciterReduction",     exciterRed.toDouble())
        put("highCutHz",            highCut.toDouble())
        put("spatialWidth",         spatialWidth.toDouble())
        put("loudnessTargetLuFS",   loudnessTarget.toDouble())
        put("harmonicGain",         harmonicGain.toDouble())
        put("antiDolbyIntensity",   antiDolby.toDouble())
        put("timestamp",            System.currentTimeMillis())
    })

    fun setIntensity(intensity: Float): Boolean = sendCommand(JSONObject().apply {
        put("action",    "SET_INTENSITY")
        put("intensity", intensity.toDouble())
    })

    fun setPFParams(vararg params: Float): Boolean = sendCommand(JSONObject().apply {
        put("action", "SET_PF_PARAMS")
        put("params", params.toList())
    })

    fun pushAdaptiveState(targetGain: Float, compAmount: Float, excRed: Float): Boolean =
        sendCommand(JSONObject().apply {
            put("action",     "SET_ADAPTIVE_STATE")
            put("targetGain", targetGain.toDouble())
            put("compAmount", compAmount.toDouble())
            put("excRed",     excRed.toDouble())
            put("timestamp",  System.currentTimeMillis())
        })

    fun pushYamnetScores(speech: Float, music: Float, classId: Int, confidence: Float): Boolean =
        sendCommand(JSONObject().apply {
            put("action",     "SET_YAMNET_SCORES")
            put("speech",     speech.toDouble())
            put("music",      music.toDouble())
            put("classId",    classId)
            put("confidence", confidence.toDouble())
            put("timestamp",  System.currentTimeMillis())
        })

    fun setRouteProfile(bassBoostDb: Float, dialogBoostDb: Float, widenerMult: Float): Boolean =
        sendCommand(JSONObject().apply {
            put("action",         "SET_ROUTE_PROFILE")
            put("bassBoostDb",    bassBoostDb.toDouble())
            put("dialogBoostDb",  dialogBoostDb.toDouble())
            put("widenerMult",    widenerMult.toDouble())
            put("timestamp",      System.currentTimeMillis())
        })

    fun pushSAFState(deltaEnergy: Float, metricNorm: Float, memory: Float, gain: Float): Boolean =
        sendCommand(JSONObject().apply {
            put("action",      "SET_SAF_STATE")
            put("deltaEnergy", deltaEnergy.toDouble())
            put("metricNorm",  metricNorm.toDouble())
            put("memory",      memory.toDouble())
            put("gain",        gain.toDouble())
            put("timestamp",   System.currentTimeMillis())
        })


    fun setEqBands(gainsDb: FloatArray, listenPhon: Float = 60f, refPhon: Float = 80f): Boolean {
        val arr = org.json.JSONArray()
        gainsDb.forEach { arr.put(it.toDouble()) }
        return sendCommand(JSONObject().apply {
            put("action",     "SET_EQ_BANDS")
            put("gains",      arr)
            put("listenPhon", listenPhon.toDouble())
            put("refPhon",    refPhon.toDouble())
            put("timestamp",  System.currentTimeMillis())
        })
    }

    fun getCalibrationStatus(): JSONObject = JSONObject().apply {
        put("connected",   isConnected)
        put("calibrated",  com.ivanna.omega.audio.Iso226Calibrator.isCalibrated)
        put("listenPhon",  com.ivanna.omega.audio.Iso226Calibrator.listenPhon.toDouble())
        put("refPhon",     com.ivanna.omega.audio.Iso226Calibrator.refPhon.toDouble())
        put("latencyMs",   lastLatencyMs.toDouble())
    }

    fun requestTelemetry(): String {                   // FIX Bug-3: probe real antes de leer lastLatencyMs
        val ok = sendCommand(JSONObject().apply { put("action", "GET_STATUS") })
        return if (ok)
            "Omega telemetry OK latency=${"%.1f".format(lastLatencyMs)}ms | ISO226=${com.ivanna.omega.audio.Iso226Calibrator.describe()}"
        else
            "Omega telemetry OFFLINE | Daemon no responde"
    }

    fun disconnect() { isConnected = false }

    fun getStatus(): Boolean = isConnected
    fun getLastLatencyMs(): Float = lastLatencyMs
}
