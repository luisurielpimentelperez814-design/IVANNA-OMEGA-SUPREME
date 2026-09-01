package com.ivanna.omega.magisk
import android.net.LocalSocket
import android.net.LocalSocketAddress
import android.util.Log
import org.json.JSONObject
import java.io.InputStream
import java.io.OutputStream
import java.net.InetSocketAddress
import java.net.Socket
import java.util.concurrent.atomic.AtomicBoolean

object OmegaEngineBridge {
    private const val TAG = "OmegaEngineBridge"
    private const val SOCKET_PRIMARY = "omega_daemon_socket"
    private const val TCP_FALLBACK_PORT = 12121   // mismo que --tcp-port en service.sh
    private const val CONNECT_TIMEOUT = 2000
    @Volatile var isConnected = false; private set
    @Volatile private var lastLatencyMs = 0f
    private val reconnecting = AtomicBoolean(false)
    // Canal persistente abstracto: funciona igual sobre LocalSocket (Unix
    // abstracto) o Socket (TCP loopback 127.0.0.1) — la app elige la primera
    // vía que conecte. Así el daemon sigue alcanzable aunque SELinux o la
    // ROM bloqueen el socket abstracto (el TCP fallback lo abre el daemon
    // con --tcp-port 12121, loopback solamente).
    private interface Channel {
        val output: OutputStream
        val input: InputStream
        fun close()
    }
    private class UnixChannel(val s: LocalSocket) : Channel {
        override val output get() = s.outputStream
        override val input  get() = s.inputStream
        override fun close() = s.close()
    }
    private class TcpChannel(val s: Socket) : Channel {
        override val output get() = s.outputStream
        override val input  get() = s.inputStream
        override fun close() = s.close()
    }
    @Volatile private var persistentChannel: Channel? = null

    fun connect(): Boolean {
        if (reconnecting.compareAndSet(false, true)) {
            try { return probeSocket() } finally { reconnecting.set(false) }
        }
        return isConnected
    }
    private fun probeSocket(): Boolean {
        // Sonda Unix primero; si falla (SELinux/ROM bloquea el abstracto),
        // probar el fallback TCP loopback que el daemon abre con --tcp-port.
        val unixOk = runCatching {
            val sock = LocalSocket()
            sock.connect(LocalSocketAddress(SOCKET_PRIMARY, LocalSocketAddress.Namespace.ABSTRACT))
            sock.soTimeout = CONNECT_TIMEOUT
            sock.close()
            true
        }.getOrDefault(false)
        if (unixOk) { isConnected = true; return true }
        val tcpOk = runCatching {
            val s = Socket()
            s.connect(InetSocketAddress("127.0.0.1", TCP_FALLBACK_PORT), CONNECT_TIMEOUT)
            s.soTimeout = CONNECT_TIMEOUT
            s.close()
            true
        }.getOrDefault(false)
        isConnected = tcpOk
        if (tcpOk) Log.i(TAG, "Daemon alcanzable via TCP loopback fallback (Unix socket bloqueado)")
        return tcpOk
    }
    @Synchronized
    private fun ensureSocket(): Channel? {
        if (persistentChannel != null && isConnected) return persistentChannel
        runCatching { persistentChannel?.close() }
        persistentChannel = null
        // Vía 1: Unix abstracto (rápida, sin overhead de red).
        val unix = runCatching {
            val sock = LocalSocket()
            sock.connect(LocalSocketAddress(SOCKET_PRIMARY, LocalSocketAddress.Namespace.ABSTRACT))
            sock.soTimeout = CONNECT_TIMEOUT
            UnixChannel(sock)
        }.getOrNull()
        if (unix != null) { persistentChannel = unix; isConnected = true; return unix }
        // Vía 2: TCP loopback 127.0.0.1:12121 (fallback del daemon).
        val tcp = runCatching {
            val s = Socket()
            s.connect(InetSocketAddress("127.0.0.1", TCP_FALLBACK_PORT), CONNECT_TIMEOUT)
            s.soTimeout = CONNECT_TIMEOUT
            s.tcpNoDelay = true
            TcpChannel(s)
        }.getOrNull()
        if (tcp != null) {
            persistentChannel = tcp; isConnected = true
            Log.i(TAG, "Conectado al daemon por TCP fallback (Unix no disponible)")
            return tcp
        }
        isConnected = false
        return null
    }
    @Synchronized
    fun sendCommand(payload: JSONObject): Boolean {
        return try {
            val t0 = System.nanoTime()
            val socket = ensureSocket()?: return false
            socket.output.write(payload.toString().toByteArray(Charsets.UTF_8))
            socket.output.flush()
            val buffer = ByteArray(4096)
            val bytesRead = socket.input.read(buffer)
            val t1 = System.nanoTime()
            lastLatencyMs = (t1 - t0) / 1_000_000f
            isConnected = bytesRead > 0
            isConnected
        } catch (e: Exception) {
            Log.w(TAG, "sendCommand error: ${e.message}")
            isConnected = false
            runCatching { persistentChannel?.close() }
            persistentChannel = null
            false
        }
    }
    @Synchronized
    fun requestCommand(payload: JSONObject): JSONObject? {
        return try {
            val t0 = System.nanoTime()
            val socket = ensureSocket()?: return null
            socket.output.write(payload.toString().toByteArray(Charsets.UTF_8))
            socket.output.flush()
            val buffer = ByteArray(4096)
            val bytesRead = socket.input.read(buffer)
            val t1 = System.nanoTime()
            lastLatencyMs = (t1 - t0) / 1_000_000f
            isConnected = bytesRead > 0
            if (isConnected) JSONObject(String(buffer, 0, bytesRead, Charsets.UTF_8)) else null
        } catch (e: Exception) {
            isConnected = false
            runCatching { persistentChannel?.close() }
            persistentChannel = null
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
    /**
     * Envía el vector latente q[7] real de Φ_SAF-Room^∞ (SaFRoomBridge.getParams())
     * al daemon — este es el único punto donde q[7] llega al bus (saf_q en
     * OmegaDspSnapshot), que omega_effect.cpp aplica vía setSafLatentParams().
     * Antes de este método, SpatialAudioPanel activaba el switch SAF pero nunca
     * enviaba el vector calculado; el DSP nunca recibía la calibración real.
     */
    fun pushSafLatentQ(q: FloatArray, gain: Float = 1.0f): Boolean {
        if (q.size < 7) return false
        return sendCommand(JSONObject().apply {
            put("action", "PUSH_SAF_STATE")
            put("q", org.json.JSONArray(q.take(7)))
            put("gain", gain)
        })
    }
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

    fun disconnect() { isConnected = false; runCatching { persistentChannel?.close() }; persistentChannel = null }
    fun getStatus(): Boolean = isConnected
    fun getLastLatencyMs(): Float = lastLatencyMs
}
