package com.ivanna.omega.magisk
import android.net.LocalSocket
import android.net.LocalSocketAddress
import android.util.Log
import org.json.JSONObject
import java.io.InputStream
import java.io.OutputStream
import java.util.concurrent.atomic.AtomicBoolean

object OmegaEngineBridge {
    private const val TAG = "OmegaEngineBridge"
    private const val SOCKET_PRIMARY = "omega_daemon_socket"
    private const val CONNECT_TIMEOUT = 2000
    @Volatile var isConnected = false; private set
    @Volatile private var lastLatencyMs = 0f
    private val reconnecting = AtomicBoolean(false)
    @Volatile private var persistentSocket: LocalSocket? = null

    fun connect(): Boolean {
        if (reconnecting.compareAndSet(false, true)) {
            try { return probeSocket() } finally { reconnecting.set(false) }
        }
        return isConnected
    }

    private fun probeSocket(): Boolean {
        val ok = runCatching {
            val sock = LocalSocket()
            sock.connect(LocalSocketAddress(SOCKET_PRIMARY, LocalSocketAddress.Namespace.ABSTRACT))
            sock.soTimeout = CONNECT_TIMEOUT
            sock.close()
            true
        }.getOrDefault(false)
        isConnected = ok
        return ok
    }

    @Synchronized
    private fun ensureSocket(): LocalSocket? {
        if (persistentSocket!= null && isConnected) return persistentSocket
        runCatching { persistentSocket?.close() }
        val sock = LocalSocket()
        val connected = runCatching {
            sock.connect(LocalSocketAddress(SOCKET_PRIMARY, LocalSocketAddress.Namespace.ABSTRACT))
            sock.soTimeout = CONNECT_TIMEOUT
            true
        }.getOrDefault(false)
        if (!connected) { isConnected = false; return null }
        persistentSocket = sock
        isConnected = true
        return sock
    }

    @Synchronized
    fun sendCommand(payload: JSONObject): Boolean {
        return try {
            val t0 = System.nanoTime()
            val socket = ensureSocket()?: return false
            val output = socket.outputStream
            output.write(payload.toString().toByteArray(Charsets.UTF_8))
            output.flush()
            val input = socket.inputStream
            val buffer = ByteArray(4096)
            val bytesRead = input.read(buffer)
            val t1 = System.nanoTime()
            lastLatencyMs = (t1 - t0) / 1_000_000f
            isConnected = bytesRead > 0
            isConnected
        } catch (e: Exception) {
            Log.w(TAG, "sendCommand error: ${e.message}")
            isConnected = false
            runCatching { persistentSocket?.close() }
            persistentSocket = null
            false
        }
    }

    @Synchronized
    fun requestCommand(payload: JSONObject): JSONObject? {
        return try {
            val t0 = System.nanoTime()
            val socket = ensureSocket()?: return null
            socket.outputStream.write(payload.toString().toByteArray(Charsets.UTF_8))
            socket.outputStream.flush()
            val buffer = ByteArray(4096)
            val bytesRead = socket.inputStream.read(buffer)
            val t1 = System.nanoTime()
            lastLatencyMs = (t1 - t0) / 1_000_000f
            isConnected = bytesRead > 0
            if (isConnected) JSONObject(String(buffer, 0, bytesRead, Charsets.UTF_8)) else null
        } catch (e: Exception) {
            isConnected = false
            runCatching { persistentSocket?.close() }
            persistentSocket = null
            null
        }
    }

    fun setPFParams(vararg params: Float): Boolean = sendCommand(JSONObject().apply { put("action","SET_PF_PARAMS"); put("params", params.toList()) })
    fun pushAdaptiveState(targetGain: Float, compAmount: Float, excRed: Float): Boolean = sendCommand(JSONObject().apply { put("action","SET_ADAPTIVE_STATE"); put("targetGain",targetGain.toDouble()); put("compAmount",compAmount.toDouble()); put("excRed",excRed.toDouble()); put("timestamp",System.currentTimeMillis()) })
    fun setRoom(rt60S: Float, wet: Float = 0.35f, roomIdx: Int = -1): Boolean = sendCommand(JSONObject().apply { put("action","SET_ROOM_RT60"); put("rt60",rt60S.toDouble()); put("wet",wet.toDouble()); put("idx",roomIdx) })
    fun disableRoom(): Boolean = setRoom(0f,0f)
    fun getRoomStatus(): JSONObject? = requestCommand(JSONObject().apply { put("action","GET_ROOM_STATUS") })
    fun requestTelemetry(): String {
        val resp = requestCommand(JSONObject().apply { put("action","GET_STATUS") })
        return if (resp!= null) "Omega OK latency=${"%.1f".format(lastLatencyMs)}ms" else "Omega OFFLINE"
    }
    fun disconnect() { isConnected = false; runCatching { persistentSocket?.close() }; persistentSocket = null }
    fun getStatus(): Boolean = isConnected
    fun getLastLatencyMs(): Float = lastLatencyMs
}
