package com.ivanna.omega.core

import android.content.Context
import android.util.Log
import com.ivanna.omega.agent.IvannaAgentCore
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.util.concurrent.atomic.AtomicLong

/**
 * Privacy-minimized remote diagnostics. Starts locally at app boot but sends nothing
 * unless the owner explicitly enables it and configures an HTTPS endpoint + bearer token.
 * No audio, app names, hardware IDs, or repair commands are sent.
 */
object LiveDiagnosticConnector {
    private const val TAG = "IvannaDiagConnector"
    private const val PREFS = "ivanna_live_diagnostics"
    private const val KEY_ENABLED = "enabled"
    private const val KEY_ENDPOINT = "endpoint"
    private const val KEY_TOKEN = "token"
    private const val PERIOD_MS = 15_000L
    private val sequence = AtomicLong(0)
    @Volatile private var started = false
    private var scope: CoroutineScope? = null
    private lateinit var appContext: Context

    fun start(context: Context) {
        if (started) return
        started = true
        appContext = context.applicationContext
        scope = CoroutineScope(SupervisorJob() + Dispatchers.IO).also { s ->
            s.launch {
                while (isActive) {
                    runCatching { uploadIfConfigured() }
                        .onFailure { Log.w(TAG, "Diagnostic upload skipped: ${it.javaClass.simpleName}") }
                    delay(PERIOD_MS)
                }
            }
        }
        Log.i(TAG, "Local diagnostic connector initialized; remote upload is opt-in")
    }

    /**
     * Call only from an explicit user-controlled settings action.
     * Endpoint must be HTTPS; token remains in private app preferences and is never logged.
     */
    fun configure(context: Context, enabled: Boolean, endpoint: String, bearerToken: String): Boolean {
        val normalized = endpoint.trim().trimEnd('/')
        val valid = runCatching {
            val url = URL(normalized)
            url.protocol.equals("https", ignoreCase = true) &&
                url.host.isNotBlank() && url.userInfo == null &&
                normalized.endsWith("/v1/diagnostics")
        }.getOrDefault(false)
        if (enabled && (!valid || bearerToken.length < 32)) return false
        context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
            .putBoolean(KEY_ENABLED, enabled)
            .putString(KEY_ENDPOINT, normalized)
            .putString(KEY_TOKEN, bearerToken)
            .apply()
        return true
    }

    fun disable(context: Context) {
        context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
            .putBoolean(KEY_ENABLED, false).remove(KEY_TOKEN).apply()
    }

    private fun uploadIfConfigured() {
        val prefs = appContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        if (!prefs.getBoolean(KEY_ENABLED, false)) return
        val endpoint = prefs.getString(KEY_ENDPOINT, null) ?: return
        val token = prefs.getString(KEY_TOKEN, null) ?: return
        if (!endpoint.startsWith("https://") || token.length < 32) return

        val metrics = OEMTelemetry.metrics.value
        val agent = IvannaAgentCore.state.value
        val health = agent.health
        val body = JSONObject().apply {
            put("schemaVersion", 1)
            put("sequence", sequence.incrementAndGet())
            put("uptimeMs", android.os.SystemClock.elapsedRealtime())
            put("metrics", JSONObject().apply {
                put("dspProcessingUs", metrics.dspProcessingUs)
                put("frameDrops", metrics.frameDrops)
                put("underruns", metrics.underruns)
                put("thermalThrottling", metrics.thermalThrottling)
                put("memoryPressure", when (metrics.memoryPressure.lowercase()) {
                    "normal", "moderate", "high" -> metrics.memoryPressure.lowercase()
                    else -> "unknown"
                })
            })
            put("agent", JSONObject().apply {
                put("running", agent.running)
                put("daemonConnected", health.daemonConnected)
                put("adaptiveEngineRunning", health.adaptiveEngineRunning)
                put("clipping", health.clipping)
                put("latencyDegraded", health.latencyDegraded)
                put("clipCount", health.clipCount)
                put("roundTripLatencyUs", health.roundTripLatencyUs)
            })
        }

        val connection = (URL(endpoint).openConnection() as HttpURLConnection)
        try {
            connection.requestMethod = "POST"
            connection.connectTimeout = 4_000
            connection.readTimeout = 4_000
            connection.doOutput = true
            connection.setRequestProperty("Authorization", "Bearer $token")
            connection.setRequestProperty("Content-Type", "application/json")
            connection.setRequestProperty("Cache-Control", "no-store")
            connection.outputStream.use { it.write(body.toString().toByteArray(Charsets.UTF_8)) }
            val status = connection.responseCode
            if (status in 200..299) Log.d(TAG, "Diagnostic snapshot accepted (HTTP $status)")
            else Log.w(TAG, "Diagnostic endpoint rejected snapshot (HTTP $status)")
        } finally {
            connection.disconnect()
        }
    }

    fun shutdown() {
        scope?.cancel()
        scope = null
        started = false
    }
}
