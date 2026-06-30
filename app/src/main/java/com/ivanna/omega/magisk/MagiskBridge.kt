package com.ivanna.omega.magisk

import android.util.Log
import java.io.BufferedReader
import java.io.InputStreamReader
import java.util.concurrent.TimeUnit

object MagiskBridge {
    private const val TAG = "MagiskBridge"
    private const val SOCKET_PATH = "/data/pf/pf.sock"
    private const val PROCESS_TIMEOUT_MS = 3000L
    
    fun isModuleInstalled(): Boolean {
        return try {
            val process = Runtime.getRuntime().exec(arrayOf("su", "-c", "test -e $SOCKET_PATH && echo yes"))
            val reader = BufferedReader(InputStreamReader(process.inputStream))
            val result = reader.readLine()?.trim() == "yes"
            // FIX: waitFor con timeout para evitar bloqueo del IO thread pool
            val exited = process.waitFor(PROCESS_TIMEOUT_MS, TimeUnit.MILLISECONDS)
            if (!exited) {
                process.destroyForcibly()
                Log.w(TAG, "Process timed out checking module")
            }
            result
        } catch (e: Exception) {
            Log.e(TAG, "Error checking module", e)
            false
        }
    }
    
    fun sendCommand(command: String): String {
        return try {
            val process = Runtime.getRuntime().exec(
                arrayOf("su", "-c", "echo -n '$command' | nc -U $SOCKET_PATH")
            )
            val reader = BufferedReader(InputStreamReader(process.inputStream))
            val result = reader.readText()
            // FIX: waitFor con timeout para evitar agotamiento del IO thread pool
            val exited = process.waitFor(PROCESS_TIMEOUT_MS, TimeUnit.MILLISECONDS)
            if (!exited) {
                process.destroyForcibly()
                Log.w(TAG, "Process timed out for command: $command")
            }
            Log.d(TAG, "Command: $command -> $result")
            result
        } catch (e: Exception) {
            Log.e(TAG, "Error sending command: $command", e)
            ""
        }
    }
    
    fun getStatus(): String = sendCommand("status")
    fun loadPreset(name: String) = sendCommand("load:$name")
    fun savePreset(name: String) = sendCommand("save:$name")
    
    fun setParameter(param: String, value: Float) {
        sendCommand("$param=$value")
    }
    
    fun setAmp(level: Int) = sendCommand("amp:$level")
    fun setDrive(value: Float) = setParameter("drive", value)
    fun setWet(value: Float) = setParameter("wet", value)
    
    // Parámetros avanzados
    fun setAlpha(value: Float) = setParameter("alpha", value)
    fun setBeta(value: Float) = setParameter("beta", value)
    fun setGamma(value: Float) = setParameter("gamma", value)
    fun setDelta(value: Float) = setParameter("delta", value)
    fun setSigma(value: Float) = setParameter("sigma", value)
    
    // EQ bands
    fun setEQBand(band: Int, gain: Float) {
        val bandNames = listOf("low", "mid", "high", "presence")
        if (band < bandNames.size) {
            setParameter(bandNames[band], gain)
        }
    }
    
    fun setSag(value: Float) = setParameter("sag", value)
}
