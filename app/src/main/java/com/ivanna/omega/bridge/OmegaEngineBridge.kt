package com.ivanna.omega.bridge

import android.net.LocalSocket
import android.net.LocalSocketAddress
import com.ivanna.omega.ai.DSPDecision
import com.ivanna.omega.ai.PerceptualSnapshot
import com.ivanna.omega.ai.UserProfile
import org.json.JSONObject
import java.io.InputStream
import java.io.OutputStream
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean

/**
 * High-performance Unix Domain Socket Bridge communicating with `/dev/socket/ivanna_omega`.
 */
object OmegaEngineBridge {
    private const val SOCKET_NAME = "/dev/socket/ivanna_omega"
    private val executor = Executors.newSingleThreadExecutor()
    private val isConnected = AtomicBoolean(false)
    private var socket: LocalSocket? = null
    private var outputStream: OutputStream? = null

    fun connectAsync(onStatusChanged: (Boolean) -> Unit) {
        executor.execute {
            try {
                if (socket == null || !socket!!.isConnected) {
                    socket = LocalSocket().apply {
                        connect(LocalSocketAddress(SOCKET_NAME, LocalSocketAddress.Namespace.FILESYSTEM))
                    }
                    outputStream = socket!!.outputStream
                    isConnected.set(true)
                    onStatusChanged(true)
                }
            } catch (e: Exception) {
                isConnected.set(false)
                onStatusChanged(false)
            }
        }
    }

    fun sendPerceptualState(snapshot: PerceptualSnapshot, decision: DSPDecision, profile: UserProfile) {
        if (!isConnected.get()) return

        executor.execute {
            try {
                val payload = JSONObject().apply {
                    put("command", "SET_PERCEPTUAL_STATE")
                    put("snapshot", snapshot.toJson())
                    put("decision", decision.toJson())
                    put("profile", profile.toJson())
                }

                val bytes = (payload.toString() + "\n").toByteArray(Charsets.UTF_8)
                outputStream?.write(bytes)
                outputStream?.flush()
            } catch (e: Exception) {
                isConnected.set(false)
            }
        }
    }

    fun sendUserFeedback(manualAdjustDelta: Float) {
        if (!isConnected.get()) return

        executor.execute {
            try {
                val payload = JSONObject().apply {
                    put("command", "FEEDBACK_TUNING")
                    put("deltaDb", manualAdjustDelta.toDouble())
                }
                val bytes = (payload.toString() + "\n").toByteArray(Charsets.UTF_8)
                outputStream?.write(bytes)
                outputStream?.flush()
            } catch (e: Exception) {
                isConnected.set(false)
            }
        }
    }
}
