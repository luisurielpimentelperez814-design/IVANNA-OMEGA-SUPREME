package com.ivanna.omega.ai.gemini

import android.util.Log
import com.ivanna.omega.assistant.core.AdaptiveResponseEngine
import com.ivanna.omega.assistant.core.AdaptiveResponseEngine.ResponseProfile
import com.ivanna.omega.assistant.core.OmniRoutePrefs
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import org.json.JSONObject
import java.io.IOException
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

/**
 * OmniRouteOrchestrator — Cliente del gateway LLM gratuito OmniRoute.
 *
 * OmniRoute (https://github.com/ChrisCompton/omniroute, MIT) corre en la VM
 * de Muse y expone un endpoint compatible con OpenAI
 * (`{baseUrl}/v1/chat/completions`, `{baseUrl}/v1/models`). Enruta a 60+
 * proveedores con fallback automático por combo; el combo por defecto es
 * `ivanna-free` (ver OmniRoutePrefs).
 *
 * Misma interfaz que GeminiOrchestrator (`generateContent` /
 * `generateContentStream`), mismo circuit breaker (3 fallos = open, 30 s
 * cooldown), mismos timeouts (15 s request / 30 s stream). El agente
 * (IvannaGeminiAgent) ya hace los 3 reintentos con backoff — este
 * orquestador NO reintenta por su cuenta, igual que el de Gemini.
 *
 * - Gemini sigue siendo el DEFAULT; OmniRoute es opt-in (flag en ajustes).
 * - `imageData` se acepta por paridad de firma pero NO se envía al gateway
 *   (ruta de chat solo texto); la visión local no pasa por aquí.
 * - La API key viaja solo en el header `Authorization: Bearer`, nunca en
 *   la URL. Si no hay key configurada no se manda el header (el gateway
 *   por defecto acepta cualquier key; con REQUIRE_API_KEY=true exige la
 *   del dashboard y responde 401).
 * - Fail-closed: si el gateway falla o no responde, el agente devuelve
 *   Offline con la última respuesta en caché (ver IvannaGeminiAgent). El
 *   audio NUNCA depende del gateway.
 * - IP: NUNCA se envía código fuente propietario de IVANNA a ningún
 *   proveedor a través de este cliente — solo el chat del usuario y los
 *   diagnósticos (directiva de Esse, 2026-11-14). Los caminos de
 *   redacción de código del motor de evolución y de auto-reparación
 *   quedan APAGADOS.
 */
class OmniRouteOrchestrator(
    private val configProvider: () -> OmniRouteConfig,
    private val scope: CoroutineScope = CoroutineScope(SupervisorJob() + Dispatchers.IO),
    httpClient: OkHttpClient? = null
) {
    companion object {
        private const val TAG = "OmniRouteOrchestrator"
        private const val HEALTH_CHECK_INTERVAL_MS = 30_000L
        private const val INACTIVITY_THRESHOLD_MS = 180_000L   // 3 min
        private const val INACTIVITY_BACKOFF_MULTIPLIER = 4
        private const val HEALTH_CHECK_TIMEOUT_MS = 10_000L
        private const val MAX_CONSECUTIVE_FAILURES = 3
        private const val CIRCUIT_COOLDOWN_MS = 30_000L
        private const val REQUEST_TIMEOUT_MS = 15_000L
        private const val STREAM_TIMEOUT_MS = 30_000L
        private const val MAX_OUTPUT_TOKENS_CEILING = 8192

        private val JSON_MEDIA_TYPE = "application/json; charset=utf-8".toMediaType()

        // ── Funciones puras (testeables sin red) ─────────────────────────

        /** Construye el cuerpo JSON de /v1/chat/completions. */
        fun buildChatRequest(
            model: String,
            systemInstruction: String?,
            prompt: String,
            temperature: Float,
            maxTokens: Int,
            stream: Boolean
        ): String {
            val messages = org.json.JSONArray()
            if (!systemInstruction.isNullOrBlank()) {
                messages.put(JSONObject().put("role", "system").put("content", systemInstruction))
            }
            messages.put(JSONObject().put("role", "user").put("content", prompt))
            return JSONObject()
                .put("model", model)
                .put("messages", messages)
                .put("temperature", temperature.toDouble())
                .put("max_tokens", maxTokens)
                .put("stream", stream)
                .toString()
        }

        /** Extrae choices[0].message.content de una respuesta no-stream.
         *  Robusto a dos formatos: JSON directo (lo normal sin header
         *  Accept) y cuerpo SSE (algunos proveedores del gateway devuelven
         *  `data:` aunque no se pida stream) — en ese caso se concatenan
         *  los deltas. */
        fun parseChatContent(responseBody: String): String? {
            // 1. JSON directo.
            val direct = runCatching {
                JSONObject(responseBody)
                    .optJSONArray("choices")
                    ?.optJSONObject(0)
                    ?.optJSONObject("message")
                    ?.optString("content")
                    ?.takeIf { it.isNotEmpty() }
            }.getOrNull()
            if (direct != null) return direct
            // 2. Cuerpo SSE: concatenar los deltas de cada línea data:.
            if (!responseBody.contains("data:")) return null
            val acc = StringBuilder()
            for (line in responseBody.lines()) {
                parseSseDelta(line.trim())?.let { acc.append(it) }
            }
            return acc.toString().takeIf { it.isNotEmpty() }
        }

        /** true si la línea SSE es el terminador `data: [DONE]`. */
        fun isSseDone(line: String): Boolean =
            line.trim().removePrefix("data:").trim() == "[DONE]"

        /**
         * Extrae el delta de texto de una línea SSE (`data: {...}`).
         * Devuelve null para líneas vacías, comentarios (`: ...`), el
         * terminador [DONE] o chunks sin contenido — el llamador sigue
         * leyendo en esos casos.
         */
        fun parseSseDelta(line: String): String? {
            val trimmed = line.trim()
            if (!trimmed.startsWith("data:")) return null
            val payload = trimmed.removePrefix("data:").trim()
            if (payload.isEmpty() || payload == "[DONE]") return null
            return runCatching {
                JSONObject(payload)
                    .optJSONArray("choices")
                    ?.optJSONObject(0)
                    ?.optJSONObject("delta")
                    ?.optString("content")
                    ?.takeIf { it.isNotEmpty() }
            }.getOrNull()
        }

        data class ApiError(val http: Int, val type: String, val code: String, val message: String)

        /** Parsea el cuerpo de error formato OpenAI: {error:{message,type,code}}. */
        fun parseError(http: Int, body: String): ApiError {
            val err = runCatching { JSONObject(body).optJSONObject("error") }.getOrNull()
                ?: return ApiError(http, "", "", body.take(200))
            return ApiError(
                http = http,
                type = err.optString("type").orEmpty(),
                code = err.optString("code").orEmpty(),
                message = err.optString("message").take(300)
            )
        }

        /** Explicación accionable en español para un error del gateway. */
        fun explainError(error: ApiError): String = when (error.http) {
            401 -> "OmniRoute rechazó la API key (HTTP 401) — revisa la key en ajustes"
            429 -> "OmniRoute sin cuota / rate limit (HTTP 429) — espera y reintenta"
            400 -> "Petición rechazada por OmniRoute (HTTP 400): ${error.message.ifBlank { "sin detalle" }}"
            404 -> "Modelo o combo no encontrado en OmniRoute (HTTP 404) — revisa el modelo en ajustes"
            502, 503 -> "OmniRoute o el proveedor no responde (HTTP ${error.http}) — el gateway reintentará con otro proveedor"
            else -> "OmniRoute devolvió HTTP ${error.http}${if (error.message.isNotBlank()) ": ${error.message}" else ""}"
        }

        /**
         * Transición pura del circuit breaker (testeable sin red).
         * Éxito: cierra el circuito y resetea fallos. Fallo: acumula; a
         * partir de MAX_CONSECUTIVE_FAILURES abre el circuito.
         */
        fun transitionHealth(
            current: ModelHealth,
            success: Boolean,
            latencyMs: Long,
            nowMs: Long
        ): ModelHealth = if (success) {
            val total = current.totalRequests + 1
            val newAvg = if (current.totalRequests == 0) latencyMs
                else (current.averageLatencyMs * current.totalRequests + latencyMs) / total
            current.copy(
                isHealthy = true,
                circuitState = ModelHealth.CircuitState.CLOSED,
                lastSuccess = nowMs,
                consecutiveFailures = 0,
                averageLatencyMs = newAvg,
                totalRequests = total
            )
        } else {
            val failures = current.consecutiveFailures + 1
            val circuit = if (failures >= MAX_CONSECUTIVE_FAILURES) ModelHealth.CircuitState.OPEN
                else ModelHealth.CircuitState.CLOSED
            current.copy(
                isHealthy = failures < MAX_CONSECUTIVE_FAILURES,
                circuitState = circuit,
                lastFailure = nowMs,
                consecutiveFailures = failures,
                totalFailures = current.totalFailures + 1,
                totalRequests = current.totalRequests + 1
            )
        }
    }

    /** Configuración viva del gateway (se resuelve en cada llamada). */
    data class OmniRouteConfig(
        val baseUrl: String,
        val apiKey: String,
        val model: String,
        val enabled: Boolean
    )

    data class ModelHealth(
        val modelName: String,
        val isHealthy: Boolean = true,
        val circuitState: CircuitState = CircuitState.CLOSED,
        val lastSuccess: Long = 0,
        val lastFailure: Long = 0,
        val consecutiveFailures: Int = 0,
        val averageLatencyMs: Long = 0,
        val totalRequests: Int = 0,
        val totalFailures: Int = 0
    ) {
        enum class CircuitState { CLOSED, OPEN, HALF_OPEN }
    }

    data class OrchestratorState(
        val activeModel: String = "unknown",
        val isHealthy: Boolean = false,
        val lastLatencyMs: Long = 0,
        val fallbackCount: Int = 0,
        val totalRequests: Int = 0,
        val healthMap: Map<String, ModelHealth> = emptyMap()
    )

    private val client: OkHttpClient = httpClient ?: OkHttpClient.Builder()
        .connectTimeout(REQUEST_TIMEOUT_MS, TimeUnit.MILLISECONDS)
        .readTimeout(STREAM_TIMEOUT_MS, TimeUnit.MILLISECONDS)
        .writeTimeout(REQUEST_TIMEOUT_MS, TimeUnit.MILLISECONDS)
        .build()

    private val healthState = ConcurrentHashMap<String, ModelHealth>()
    private val isRunning = AtomicBoolean(true)
    private val _state = MutableStateFlow(OrchestratorState())
    val state: StateFlow<OrchestratorState> = _state.asStateFlow()

    @Volatile private var lastRealActivityAt = System.currentTimeMillis()

    /** Llamar en cada consulta real del usuario (igual que GeminiOrchestrator). */
    fun notifyRealActivity() { lastRealActivityAt = System.currentTimeMillis() }

    init {
        scope.launch {
            while (isRunning.get()) {
                runHealthChecks()
                val idle = System.currentTimeMillis() - lastRealActivityAt > INACTIVITY_THRESHOLD_MS
                delay(if (idle) HEALTH_CHECK_INTERVAL_MS * INACTIVITY_BACKOFF_MULTIPLIER else HEALTH_CHECK_INTERVAL_MS)
            }
        }
    }

    private fun resolveConfig(): OmniRouteConfig {
        val raw = configProvider()
        return raw.copy(
            baseUrl = raw.baseUrl.trim().trimEnd('/').ifBlank { OmniRoutePrefs.DEFAULT_BASE_URL },
            model = raw.model.trim().ifBlank { OmniRoutePrefs.DEFAULT_MODEL }
        )
    }

    private fun healthOf(model: String): ModelHealth =
        healthState.getOrPut(model) { ModelHealth(modelName = model) }

    suspend fun generateContent(
        prompt: String,
        responseProfile: ResponseProfile = ResponseProfile.NORMAL,
        systemInstruction: String? = null,
        imageData: List<ByteArray>? = null
    ): Result<String> = withContext(Dispatchers.IO) {
        // imageData se ignora por diseño: la ruta de chat del gateway es
        // solo texto (paridad de firma con GeminiOrchestrator, no soporte).
        if (imageData != null) Log.i(TAG, "imageData ignorado: OmniRoute chat es solo texto")
        val config = resolveConfig()
        val model = config.model

        if (healthOf(model).circuitState == ModelHealth.CircuitState.OPEN) {
            Log.w(TAG, "Circuit OPEN for $model, no se intenta la llamada")
            return@withContext Result.failure(Exception("Circuit OPEN for $model"))
        }

        val startTime = System.currentTimeMillis()
        val result = runCatching { postChatCompletion(config, model, systemInstruction, prompt, responseProfile, stream = false) }
        val latency = System.currentTimeMillis() - startTime
        updateHealth(model, result.isSuccess, latency)

        result.onSuccess {
            _state.update { s -> s.copy(activeModel = model, isHealthy = true, lastLatencyMs = latency, totalRequests = s.totalRequests + 1, healthMap = healthState.toMap()) }
        }.onFailure { error ->
            Log.w(TAG, "OmniRoute $model failed: ${error.message}")
        }
        result
    }

    suspend fun generateContentStream(
        prompt: String,
        responseProfile: ResponseProfile = ResponseProfile.NORMAL,
        systemInstruction: String? = null
    ): Flow<String> = flow {
        val config = resolveConfig()
        val model = config.model
        if (healthOf(model).circuitState == ModelHealth.CircuitState.OPEN) {
            throw Exception("Circuit OPEN for $model")
        }
        val startTime = System.currentTimeMillis()
        try {
            var acc = ""
            streamChatCompletion(config, model, systemInstruction, prompt, responseProfile).collect { delta ->
                acc += delta
                emit(acc)
            }
            updateHealth(model, true, System.currentTimeMillis() - startTime)
            _state.update { it.copy(activeModel = model, isHealthy = true, lastLatencyMs = System.currentTimeMillis() - startTime) }
        } catch (e: Exception) {
            updateHealth(model, false, System.currentTimeMillis() - startTime)
            throw e
        }
    }.flowOn(Dispatchers.IO)

    /** POST {baseUrl}/chat/completions sin streaming; devuelve el texto. */
    private fun postChatCompletion(
        config: OmniRouteConfig,
        model: String,
        systemInstruction: String?,
        prompt: String,
        profile: ResponseProfile,
        stream: Boolean
    ): String {
        val bodyJson = buildChatRequest(
            model = model,
            systemInstruction = systemInstruction,
            prompt = prompt,
            temperature = AdaptiveResponseEngine.getTemperature(profile),
            maxTokens = minOf(MAX_OUTPUT_TOKENS_CEILING, AdaptiveResponseEngine.getMaxTokens(profile)),
            stream = stream
        )
        val request = Request.Builder()
            .url("${config.baseUrl}/chat/completions")
            .post(bodyJson.toRequestBody(JSON_MEDIA_TYPE))
            .apply { if (config.apiKey.isNotBlank()) header("Authorization", "Bearer ${config.apiKey}") }
            .build()
        client.newCall(request).execute().use { response ->
            val rawBody = response.body?.string().orEmpty()
            if (!response.isSuccessful) {
                throw IOException(explainError(parseError(response.code, rawBody)))
            }
            return parseChatContent(rawBody)
                ?: throw IOException("OmniRoute respondió sin contenido (modelo $model)")
        }
    }

    /** POST {baseUrl}/chat/completions con stream SSE; emite deltas de texto. */
    private fun streamChatCompletion(
        config: OmniRouteConfig,
        model: String,
        systemInstruction: String?,
        prompt: String,
        profile: ResponseProfile
    ): Flow<String> = flow {
        val bodyJson = buildChatRequest(
            model = model,
            systemInstruction = systemInstruction,
            prompt = prompt,
            temperature = AdaptiveResponseEngine.getTemperature(profile),
            maxTokens = minOf(MAX_OUTPUT_TOKENS_CEILING, AdaptiveResponseEngine.getMaxTokens(profile)),
            stream = true
        )
        val request = Request.Builder()
            .url("${config.baseUrl}/chat/completions")
            .post(bodyJson.toRequestBody(JSON_MEDIA_TYPE))
            .header("Accept", "text/event-stream")
            .apply { if (config.apiKey.isNotBlank()) header("Authorization", "Bearer ${config.apiKey}") }
            .build()
        val response = client.newCall(request).execute()
        // Sin `use {}`: el cuerpo se lee con emit() en el contexto suspend
        // del flow, y `use` es un lambda no-suspend (emit ahí no compila).
        // Se cierran respuesta/reader en finally.
        if (!response.isSuccessful) {
            val rawBody = response.body?.string().orEmpty()
            runCatching { response.close() }
            throw IOException(explainError(parseError(response.code, rawBody)))
        }
        val buffered = response.body?.charStream()?.let { java.io.BufferedReader(it) }
            ?: run {
                runCatching { response.close() }
                throw IOException("OmniRoute stream sin cuerpo (modelo $model)")
            }
        try {
            while (true) {
                val line = buffered.readLine() ?: break
                val current = line.trim()
                if (current.isEmpty()) continue
                if (isSseDone(current)) break
                val delta = parseSseDelta(current) ?: continue
                emit(delta)
            }
        } finally {
            runCatching { buffered.close() }
            runCatching { response.close() }
        }
    }.flowOn(Dispatchers.IO)

    private fun updateHealth(modelName: String, success: Boolean, latencyMs: Long) {
        val current = healthOf(modelName)
        healthState[modelName] = transitionHealth(current, success, latencyMs, System.currentTimeMillis())
    }

    private suspend fun runHealthChecks() {
        val config = runCatching { resolveConfig() }.getOrNull() ?: return
        // Sin el flag activo no se gasta red en health checks: Gemini sigue
        // siendo el default y el gateway no se toca hasta que el usuario
        // enciende OmniRoute en ajustes.
        if (!config.enabled) return
        val model = config.model
        val health = healthOf(model)

        if (health.circuitState == ModelHealth.CircuitState.OPEN) {
            if (System.currentTimeMillis() - health.lastFailure > CIRCUIT_COOLDOWN_MS) {
                healthState[model] = health.copy(circuitState = ModelHealth.CircuitState.HALF_OPEN)
                Log.i(TAG, "Circuit HALF_OPEN for $model")
            } else {
                return
            }
        }

        // Health check ligero: GET /v1/models (no gasta tokens ni llama a
        // ningún LLM). 200 = el gateway responde.
        val start = System.currentTimeMillis()
        val result = runCatching {
            withTimeout(HEALTH_CHECK_TIMEOUT_MS) {
                val request = Request.Builder()
                    .url("${config.baseUrl}/models")
                    .get()
                    .apply { if (config.apiKey.isNotBlank()) header("Authorization", "Bearer ${config.apiKey}") }
                    .build()
                client.newCall(request).execute().use { response ->
                    if (!response.isSuccessful) throw IOException("HTTP ${response.code}")
                    response.body?.string()
                }
            }
        }
        updateHealth(model, result.isSuccess, System.currentTimeMillis() - start)
    }

    fun shutdown() { isRunning.set(false); scope.cancel() }
}
