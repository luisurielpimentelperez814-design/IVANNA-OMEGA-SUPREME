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
import kotlinx.coroutines.withContext
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

    // v2.5.443 — Puente de diagnóstico en Floot (verificado 2026-11-14):
    // https://ivanna-diagnostic-bridge.floot.app — el prefijo /_api es
    // obligatorio. El contrato es EL MISMO que el puente de Render (mismo
    // body, mismas respuestas): GET /_api/healthz, POST /_api/v1/diagnostics
    // (Bearer si hay token), GET /_api/v1/diagnostics/latest,
    // GET /_api/v1/diagnostics/recent, POST /_api/v1/diagnostics/analyze.
    // El análisis en Floot es por REGLAS en español (no LLM; Pollinations
    // no responde a tiempo desde Floot).
    const val DEFAULT_ENDPOINT: String =
        "https://ivanna-diagnostic-bridge.floot.app/_api/v1/diagnostics"
    // URL vieja de Render (el puente vivía en el plan free de Render).
    // Si un teléfono la tiene guardada, se migra sola a Floot (ver
    // resolveEndpoint): Esse no tiene que re-escribir nada.
    const val LEGACY_RENDER_ENDPOINT: String =
        "https://ivanna-diagnostic-bridge.onrender.com/v1/diagnostics"

    /**
     * Resuelve el endpoint efectivo. Pura (testeable en host).
     *   - vacío/nulo → el default de Floot.
     *   - la URL vieja de Render → el default de Floot (migración automática).
     *   - cualquier otra → la guardada (sin barra final).
     */
    fun resolveEndpoint(stored: String?): String {
        val normalized = stored?.trim()?.trimEnd('/') ?: ""
        if (normalized.isEmpty()) return DEFAULT_ENDPOINT
        if (normalized == LEGACY_RENDER_ENDPOINT) return DEFAULT_ENDPOINT
        return normalized
    }
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
        val prefs = context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        val effectiveToken = bearerToken.ifBlank { prefs.getString(KEY_TOKEN, "") ?: "" }
        // v2.5.443 — si se guarda la URL vieja de Render, se guarda Floot
        // en su lugar (migración; el contrato es el mismo).
        val normalized = resolveEndpoint(endpoint.trim().trimEnd('/'))
        val valid = runCatching {
            val url = URL(normalized)
            url.protocol.equals("https", ignoreCase = true) &&
                url.host.isNotBlank() && url.userInfo == null &&
                normalized.endsWith("/v1/diagnostics")
        }.getOrDefault(false)
        if (enabled && (!valid || effectiveToken.length < 32)) return false
        prefs.edit()
            .putBoolean(KEY_ENABLED, enabled)
            .putString(KEY_ENDPOINT, normalized)
            .putString(KEY_TOKEN, effectiveToken)
            .apply()
        return true
    }

    fun isEnabled(context: Context): Boolean =
        context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getBoolean(KEY_ENABLED, false)

    /** Endpoint efectivo (resuelto: default Floot si no hay nada o si era Render). */
    fun configuredEndpoint(context: Context): String =
        resolveEndpoint(
            context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
                .getString(KEY_ENDPOINT, null)
        )

    fun disable(context: Context) {
        context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
            .putBoolean(KEY_ENABLED, false).remove(KEY_TOKEN).apply()
    }

    /** Requests an on-demand AI analysis of the latest uploaded snapshot. Recommendations only. */
    fun requestAnalysis(context: Context, onResult: (String) -> Unit) {
        val prefs = context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        if (!prefs.getBoolean(KEY_ENABLED, false)) {
            onResult("Activa primero el envío remoto de diagnósticos.")
            return
        }
        val endpoint = resolveEndpoint(prefs.getString(KEY_ENDPOINT, null))
        val token = prefs.getString(KEY_TOKEN, null) ?: run {
            onResult("Falta configurar el token de diagnóstico.")
            return
        }
        val worker = scope ?: run {
            onResult("El conector local todavía no está iniciado.")
            return
        }
        worker.launch {
            val result = runCatching {
                val analysisUrl = endpoint.trimEnd('/') + "/analyze"
                val connection = (URL(analysisUrl).openConnection() as HttpURLConnection)
                try {
                    connection.requestMethod = "POST"
                    connection.connectTimeout = 5_000
                    connection.readTimeout = 15_000
                    connection.doOutput = true
                    connection.setRequestProperty("Authorization", "Bearer $token")
                    connection.setRequestProperty("Content-Type", "application/json")
                    connection.outputStream.use { it.write(byteArrayOf()) }
                    val status = connection.responseCode
                    val stream = if (status in 200..299) connection.inputStream else connection.errorStream
                    val text = stream?.bufferedReader(Charsets.UTF_8)?.use { it.readText() }.orEmpty()
                    val json = runCatching { JSONObject(text) }.getOrNull()
                    if (status in 200..299) json?.optString("analysis")?.takeIf { it.isNotBlank() }
                        ?: "El servicio no devolvió un análisis legible."
                    else when (json?.optString("error")) {
                        "ai_not_configured" -> "El servidor no tiene disponible el análisis por reglas."
                        "no_snapshot_yet" -> "Todavía no hay una telemetría recibida; espera un ciclo de envío."
                        else -> "No se pudo obtener el análisis (HTTP $status)."
                    }
                } finally {
                    connection.disconnect()
                }
            }.getOrElse { "No se pudo conectar al servicio de análisis (${it.javaClass.simpleName})." }
            withContext(Dispatchers.Main) { onResult(result) }
        }
    }

    private fun uploadIfConfigured() {
        val prefs = appContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        if (!prefs.getBoolean(KEY_ENABLED, false)) return
        val endpoint = resolveEndpoint(prefs.getString(KEY_ENDPOINT, null))
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
