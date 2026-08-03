package com.ivanna.omega.audio

import com.ivanna.omega.audio.objects.AudioScene
import android.net.LocalSocket
import android.net.LocalSocketAddress
import java.io.OutputStream
import java.nio.charset.StandardCharsets

/**
 * Bridge IPC de ultra-baja latencia para comunicación con el Daemon C++ de Magisk
 * vía UNIX Domain Socket (omega_daemon_socket).
 */
object OmegaEngineBridge {
    private const val SOCKET_PATH = "omega_daemon_socket"

    fun sendSpatialScene(scene: AudioScene): Boolean {
        return try {
            val socket = LocalSocket()
            socket.connect(LocalSocketAddress(SOCKET_PATH, LocalSocketAddress.Namespace.ABSTRACT))
            val output: OutputStream = socket.outputStream

            val sb = StringBuilder()
            sb.append("SET_SPATIAL_CONFIG ")
            sb.append("SAMPLE_RATE=").append(scene.sampleRate).append(" ")
            sb.append("OBJECTS=").append(scene.activeObjectCount).append("\n")

            output.write(sb.toString().toByteArray(StandardCharsets.UTF_8))
            output.flush()
            socket.close()
            true
        } catch (e: Exception) {
            false
        }
    }

    fun getPerformanceStats(): String {
        return try {
            val socket = LocalSocket()
            socket.connect(LocalSocketAddress(SOCKET_PATH, LocalSocketAddress.Namespace.ABSTRACT))
            val output = socket.outputStream
            val input = socket.inputStream

            output.write("GET_PERFORMANCE_STATS\n".toByteArray(StandardCharsets.UTF_8))
            output.flush()

            val buffer = ByteArray(256)
            val bytesRead = input.read(buffer)
            socket.close()
            if (bytesRead > 0) String(buffer, 0, bytesRead, StandardCharsets.UTF_8) else "OFFLINE"
        } catch (e: Exception) {
            "OFFLINE (Daemon no iniciado)"
        }
    }
}
