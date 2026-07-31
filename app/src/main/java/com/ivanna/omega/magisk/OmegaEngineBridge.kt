package com.ivanna.omega.magisk

import android.net.LocalSocket
import android.net.LocalSocketAddress
import android.util.Log
import org.json.JSONObject
import java.io.InputStream
import java.io.OutputStream

object OmegaEngineBridge {
    private const val TAG = "OmegaEngineBridge"
    private const val SOCKET_PATH = "/dev/socket/ivanna_omega"
    var isConnected = false
        private set
    private var lastLatencyMs = 0f

    @Synchronized
    fun sendCommand(payload: JSONObject): Boolean {
        var socket: LocalSocket? = null
        return try {
            val startTime = System.nanoTime()
            socket = LocalSocket()
            socket.connect(LocalSocketAddress(SOCKET_PATH, LocalSocketAddress.Namespace.FILESYSTEM))
            val output: OutputStream = socket.outputStream
            val jsonBytes = payload.toString().toByteArray(Charsets.UTF_8)
            output.write(jsonBytes)
            output.flush()
            
            // Read response
            val input: InputStream = socket.inputStream
            val buffer = ByteArray(256)
            val bytesRead = input.read(buffer)
            val endTime = System.nanoTime()
            lastLatencyMs = (endTime - startTime) / 1000000f
            isConnected = true
            bytesRead > 0
        } catch (e: Exception) {
            Log.e(TAG, "Failed to communicate with Magisk socket: ${e.message}")
            isConnected = false
            false
        } finally {
            try { socket?.close() } catch (_: Exception) {}
        }
    }

    fun sendPerceptualState(
        compressor: Float,
        exciterRed: Float,
        highCut: Float,
        spatialWidth: Float,
        loudnessTarget: Float,
        harmonicGain: Float,
        antiDolby: Float
    ): Boolean {
        val payload = JSONObject().apply {
            put("action", "SET_PERCEPTUAL_STATE")
            put("compressor", compressor.toDouble())
            put("exciterReduction", exciterRed.toDouble())
            put("highCutHz", highCut.toDouble())
            put("spatialWidth", spatialWidth.toDouble())
            put("loudnessTargetLuFS", loudnessTarget.toDouble())
            put("harmonicGain", harmonicGain.toDouble())
            put("antiDolbyIntensity", antiDolby.toDouble())
            put("timestamp", System.currentTimeMillis())
        }
        return sendCommand(payload)
    }

    fun setIntensity(intensity: Float): Boolean {
        val payload = JSONObject().apply {
            put("action", "SET_INTENSITY")
            put("intensity", intensity.toDouble())
        }
        return sendCommand(payload)
    }

    fun requestTelemetry(): String {
        return try {
            "Omega telemetry OK latency=${lastLatencyMs}ms"
        } catch (e: Exception) {
            "Telemetry unavailable"
        }
    }

    fun disconnect() {
        isConnected = false
    }

    
    fun connect(): Boolean {
        return try {
            isConnected = true
            true
        } catch (_: Exception) {
            false
        }
    }
    fun setPFParams(
        vararg params: Float
    ): Boolean {
        val payload = JSONObject().apply {
            put("action", "SET_PF_PARAMS")
            put("params", params.toList())
        }
        return sendCommand(payload)
    }

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

    fun setRouteProfile(routeProfile: Any): Boolean =
        sendCommand(JSONObject().apply {
            put("action",       "SET_ROUTE_PROFILE")
            put("routeProfile", routeProfile.toString())
            put("timestamp",    System.currentTimeMillis())
        })

    fun getStatus(): Boolean = isConnected
    fun getLastLatencyMs(): Float = lastLatencyMs
}
