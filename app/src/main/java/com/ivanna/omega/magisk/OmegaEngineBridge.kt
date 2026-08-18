package com.ivanna.omega.magisk
import android.net.LocalSocket
import android.net.LocalSocketAddress
import android.util.Log
import org.json.JSONObject
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
            socket.outputStream.write(payload.toString().toByteArray(Charsets.UTF_8))
            socket.outputStream.flush()
            val buffer = ByteArray(4096)
            val bytesRead = socket.inputStream.read(buffer)
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
    fun pushAdaptiveState(targetGain: Float, compAmount: Float, excRed: Float): Boolean = sendCommand(JSONObject().apply { put("action","SET_ADAPTIVE_STATE"); put("targetGain",targetGain); put("compAmount",compAmount); put("excRed",excRed); put("timestamp",System.currentTimeMillis()) })
    fun setRoom(rt60S: Float, wet: Float = 0.35f, roomIdx: Int = -1): Boolean = sendCommand(JSONObject().apply { put("action","SET_ROOM_RT60"); put("rt60",rt60S); put("wet",wet); put("idx",roomIdx) })
    fun disableRoom(): Boolean = setRoom(0f,0f)
    fun getRoomStatus(): JSONObject? = requestCommand(JSONObject().apply { put("action","GET_ROOM_STATUS") })
    fun requestTelemetry(): String { val r = requestCommand(JSONObject().apply { put("action","GET_STATUS") }); return if (r!=null) "Omega OK latency=${"%.1f".format(lastLatencyMs)}ms $r" else "Omega OFFLINE" }

    fun setIntensity(intensity: Float): Boolean = sendCommand(JSONObject().apply { put("action","SET_INTENSITY"); put("intensity", intensity) })
    fun setIntensity(intensity: Double): Boolean = sendCommand(JSONObject().apply { put("action","SET_INTENSITY"); put("intensity", intensity) })
    fun setIntensity(vararg args: Any): Boolean = sendCommand(JSONObject().apply { put("action","SET_INTENSITY"); put("args", args.map { it.toString() }) })

    fun pushSAFState(deltaEnergy: Float, metricNorm: Float, memory: Float, gain: Float): Boolean = sendCommand(JSONObject().apply { put("action","PUSH_SAF_STATE"); put("deltaEnergy", deltaEnergy); put("metricNorm", metricNorm); put("memory", memory); put("gain", gain) })
    fun pushSAFState(json: JSONObject): Boolean = sendCommand(JSONObject().apply { put("action","PUSH_SAF_STATE"); put("state", json) })
    fun pushSAFState(vararg args: Any): Boolean = sendCommand(JSONObject().apply { put("action","PUSH_SAF_STATE"); put("args", args.map { it.toString() }) })

    fun sendPerceptualState(compressor: Float, exciterRed: Float, highCut: Float, spatialWidth: Float, loudnessTarget: Float, harmonicGain: Float, antiDolby: Float): Boolean = sendCommand(JSONObject().apply { put("action","SEND_PERCEPTUAL_STATE"); put("compressor", compressor); put("exciterRed", exciterRed); put("highCut", highCut); put("spatialWidth", spatialWidth); put("loudnessTarget", loudnessTarget); put("harmonicGain", harmonicGain); put("antiDolby", antiDolby) })
    fun sendPerceptualState(json: JSONObject): Boolean = sendCommand(JSONObject().apply { put("action","SEND_PERCEPTUAL_STATE"); put("state", json) })
    fun sendPerceptualState(vararg args: Any): Boolean = sendCommand(JSONObject().apply { put("action","SEND_PERCEPTUAL_STATE"); put("args", args.map { it.toString() }) })

    fun setRouteProfile(json: JSONObject): Boolean = sendCommand(JSONObject().apply { put("action","SET_ROUTE_PROFILE"); put("profile", json) })
    fun setRouteProfile(a: Float, b: Float, c: Float): Boolean = sendCommand(JSONObject().apply { put("action","SET_ROUTE_PROFILE"); put("a", a); put("b", b); put("c", c) })
    fun setRouteProfile(a: Float, b: Float): Boolean = sendCommand(JSONObject().apply { put("action","SET_ROUTE_PROFILE"); put("a", a); put("b", b) })
    fun setRouteProfile(vararg args: Any): Boolean = sendCommand(JSONObject().apply { put("action","SET_ROUTE_PROFILE"); put("args", args.map { it.toString() }) })

    fun pushYamnetScores(speech: Float, music: Float, classId: Int, confidence: Float): Boolean = sendCommand(JSONObject().apply { put("action","PUSH_YAMNET_SCORES"); put("speech", speech); put("music", music); put("classId", classId); put("confidence", confidence) })
    fun pushYamnetScores(speech: Float, music: Float, classId: Float, confidence: Float): Boolean = pushYamnetScores(speech, music, classId.toInt(), confidence)
    fun pushYamnetScores(json: JSONObject): Boolean = sendCommand(JSONObject().apply { put("action","PUSH_YAMNET_SCORES"); put("scores", json) })
    fun pushYamnetScores(scores: FloatArray): Boolean = sendCommand(JSONObject().apply { put("action","PUSH_YAMNET_SCORES"); put("scores", scores.toList()) })
    fun pushYamnetScores(scores: List<Float>): Boolean = sendCommand(JSONObject().apply { put("action","PUSH_YAMNET_SCORES"); put("scores", scores) })
    fun pushYamnetScores(vararg args: Any): Boolean = sendCommand(JSONObject().apply { put("action","PUSH_YAMNET_SCORES"); put("args", args.map { it.toString() }) })

    fun setPinnaMetrics(json: JSONObject): Boolean = sendCommand(JSONObject().apply { put("action","SET_PINNA_METRICS"); put("metrics", json) })
    fun setPinnaMetrics(width: Float, height: Float, depth: Float): Boolean = sendCommand(JSONObject().apply { put("action","SET_PINNA_METRICS"); put("width", width); put("height", height); put("depth", depth) })
    fun setPinnaMetrics(vararg args: Any): Boolean = sendCommand(JSONObject().apply { put("action","SET_PINNA_METRICS"); put("args", args.map { it.toString() }) })

    fun disconnect() { isConnected = false; runCatching { persistentSocket?.close() }; persistentSocket = null }
    fun getStatus(): Boolean = isConnected
    fun getLastLatencyMs(): Float = lastLatencyMs
}
