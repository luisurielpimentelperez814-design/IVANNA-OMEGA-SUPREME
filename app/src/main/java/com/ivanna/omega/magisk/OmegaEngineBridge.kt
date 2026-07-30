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
    private var isConnected = false
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

    fun getStatus(): Boolean = isConnected
    fun getLastLatencyMs(): Float = lastLatencyMs
}
