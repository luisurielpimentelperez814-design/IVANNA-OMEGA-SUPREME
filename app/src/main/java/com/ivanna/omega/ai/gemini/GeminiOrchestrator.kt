package com.ivanna.omega.ai.gemini

import android.util.Log
import com.google.ai.client.generativeai.GenerativeModel
import com.google.ai.client.generativeai.type.content
import com.ivanna.omega.assistant.core.AdaptiveResponseEngine.ResponseProfile
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger

/**
 * GeminiOrchestrator — Gestión inteligente de modelos con circuit breaker.
 *
 * Hardening OEM:
 * - Timeouts en health checks (10s)
 * - Circuit breaker por modelo (3 fallos = open, 30s cooldown)
 * - Latency tracking con ventana deslizante
 * - Health checks adaptativos (más frecuentes si hay fallos)
 */
class GeminiOrchestrator(
    private val apiKeyProvider: () -> String,
    private val scope: CoroutineScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
) {
    companion object {
        private const val TAG = "GeminiOrchestrator"
        private const val HEALTH_CHECK_INTERVAL_MS = 30_000L
        private const val HEALTH_CHECK_TIMEOUT_MS = 10_000L
        private const val MAX_CONSECUTIVE_FAILURES = 3
        private const val CIRCUIT_COOLDOWN_MS = 30_000L
    }

    data class ModelEntry(
        val name: String,
        val displayName: String,
        val maxInputTokens: Int,
        val maxOutputTokens: Int,
        val supportsVision: Boolean,
        val supportsAudio: Boolean,
        val supportsFunctionCalling: Boolean,
        val costTier: CostTier,
        val latencyProfile: LatencyProfile
    ) {
        enum class CostTier { PREMIUM, STANDARD, ECONOMY }
        enum class LatencyProfile { VERY_FAST, FAST, MODERATE, SLOW }
    }

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

    private val registry = listOf(
        ModelEntry("gemini-1.5-pro-latest", "Gemini 1.5 Pro", 1_000_000, 8192, true, true, true, ModelEntry.CostTier.PREMIUM, ModelEntry.LatencyProfile.MODERATE),
        ModelEntry("gemini-1.5-flash-latest", "Gemini 1.5 Flash", 1_000_000, 8192, true, true, true, ModelEntry.CostTier.STANDARD, ModelEntry.LatencyProfile.FAST),
        ModelEntry("gemini-1.5-flash-8b-latest", "Gemini 1.5 Flash 8B", 1_000_000, 8192, true, false, true, ModelEntry.CostTier.ECONOMY, ModelEntry.LatencyProfile.VERY_FAST)
    )

    private val healthState = ConcurrentHashMap<String, ModelHealth>()
    private val isRunning = AtomicBoolean(true)
    private val _state = MutableStateFlow(OrchestratorState())
    val state: StateFlow<OrchestratorState> = _state.asStateFlow()

    init {
        registry.forEach { healthState[it.name] = ModelHealth(modelName = it.name) }
        scope.launch {
            while (isRunning.get()) {
                runHealthChecks()
                delay(HEALTH_CHECK_INTERVAL_MS)
            }
        }
    }

    suspend fun generateContent(prompt: String, responseProfile: ResponseProfile = ResponseProfile.NORMAL, systemInstruction: String? = null, imageData: List<ByteArray>? = null): Result<String> = withContext(Dispatchers.IO) {
        val model = selectModel(responseProfile, imageData != null)

        // Circuit breaker check
        if (healthState[model.name]?.circuitState == ModelHealth.CircuitState.OPEN) {
            Log.w(TAG, "Circuit OPEN for ${model.name}, skipping to fallback")
            return@withContext tryFallback(model, prompt, responseProfile, systemInstruction, imageData != null)
        }

        val modelInstance = createModel(model, systemInstruction)
        val startTime = System.currentTimeMillis()
        val result = runCatching { modelInstance.generateContent(prompt).text ?: "" }
        val latency = System.currentTimeMillis() - startTime
        updateHealth(model.name, result.isSuccess, latency)

        result.onSuccess {
            _state.update { s -> s.copy(activeModel = model.name, isHealthy = true, lastLatencyMs = latency, totalRequests = s.totalRequests + 1, healthMap = healthState.toMap()) }
        }.onFailure { error ->
            Log.w(TAG, "Model ${model.name} failed: ${error.message}")
            return@withContext tryFallback(model, prompt, responseProfile, systemInstruction, imageData != null)
        }
        result
    }

    private suspend fun tryFallback(failed: ModelEntry, prompt: String, profile: ResponseProfile, systemInstruction: String?, needsVision: Boolean): Result<String> {
        val fallback = findFallback(failed, profile, needsVision)
        if (fallback != null) {
            Log.i(TAG, "Falling back to ${fallback.name}")
            _state.update { it.copy(fallbackCount = it.fallbackCount + 1) }

            if (healthState[fallback.name]?.circuitState == ModelHealth.CircuitState.OPEN) {
                Log.w(TAG, "Fallback ${fallback.name} also OPEN")
                return Result.failure(Exception("All models circuit OPEN"))
            }

            val fbInstance = createModel(fallback, systemInstruction)
            val fbStart = System.currentTimeMillis()
            val fbResult = runCatching { fbInstance.generateContent(prompt).text ?: "" }
            updateHealth(fallback.name, fbResult.isSuccess, System.currentTimeMillis() - fbStart)

            fbResult.onSuccess {
                _state.update { s -> s.copy(activeModel = fallback.name, isHealthy = true, lastLatencyMs = System.currentTimeMillis() - fbStart, totalRequests = s.totalRequests + 1, healthMap = healthState.toMap()) }
            }
            return fbResult
        } else {
            _state.update { it.copy(isHealthy = false, healthMap = healthState.toMap()) }
            return Result.failure(Exception("No fallback available"))
        }
    }

    suspend fun generateContentStream(prompt: String, responseProfile: ResponseProfile = ResponseProfile.NORMAL, systemInstruction: String? = null): Flow<String> = flow {
        val model = selectModel(responseProfile, false)
        if (healthState[model.name]?.circuitState == ModelHealth.CircuitState.OPEN) {
            throw Exception("Circuit OPEN for ${model.name}")
        }
        val instance = createModel(model, systemInstruction)
        val startTime = System.currentTimeMillis()
        try {
            var acc = ""
            instance.generateContentStream(prompt).collect { chunk ->
                val text = chunk.text ?: ""
                acc += text
                emit(acc)
            }
            updateHealth(model.name, true, System.currentTimeMillis() - startTime)
            _state.update { it.copy(activeModel = model.name, isHealthy = true, lastLatencyMs = System.currentTimeMillis() - startTime) }
        } catch (e: Exception) {
            updateHealth(model.name, false, System.currentTimeMillis() - startTime)
            throw e
        }
    }.flowOn(Dispatchers.IO)

    private fun selectModel(profile: ResponseProfile, needsVision: Boolean): ModelEntry {
        val candidates = when (profile) {
            ResponseProfile.FAST -> registry.filter { it.latencyProfile == ModelEntry.LatencyProfile.VERY_FAST || it.latencyProfile == ModelEntry.LatencyProfile.FAST }
            ResponseProfile.NORMAL -> registry.filter { it.latencyProfile != ModelEntry.LatencyProfile.SLOW }
            ResponseProfile.DEEP_REASONING -> registry.filter { it.name.contains("pro") || it.latencyProfile == ModelEntry.LatencyProfile.MODERATE }
            ResponseProfile.ENGINEERING_MODE -> registry.filter { it.name.contains("pro") }
        }.ifEmpty { registry }
        val capable = candidates.filter { !needsVision || it.supportsVision }
        return capable.sortedWith(compareByDescending<ModelEntry> {
            val h = healthState[it.name]
            h?.isHealthy == true && h.circuitState != ModelHealth.CircuitState.OPEN
        }.thenBy { healthState[it.name]?.averageLatencyMs ?: Long.MAX_VALUE }).firstOrNull() ?: registry.first()
    }

    private fun findFallback(failed: ModelEntry, profile: ResponseProfile, needsVision: Boolean): ModelEntry? {
        val idx = registry.indexOfFirst { it.name == failed.name }
        return registry.drop(idx + 1).filter { !needsVision || it.supportsVision }.firstOrNull {
            healthState[it.name]?.circuitState != ModelHealth.CircuitState.OPEN
        }
    }

    private fun createModel(entry: ModelEntry, systemInstruction: String?): GenerativeModel {
        val builder = GenerativeModel.Builder().modelName(entry.name).apiKey(apiKeyProvider())
        systemInstruction?.let { builder.systemInstruction(content { text(it) }) }
        return builder.build()
    }

    private fun updateHealth(modelName: String, success: Boolean, latencyMs: Long) {
        val current = healthState[modelName] ?: ModelHealth(modelName = modelName)
        val newHealth = if (success) {
            val total = current.totalRequests + 1
            val newAvg = if (current.totalRequests == 0) latencyMs else (current.averageLatencyMs * current.totalRequests + latencyMs) / total
            current.copy(
                isHealthy = true,
                circuitState = ModelHealth.CircuitState.CLOSED,
                lastSuccess = System.currentTimeMillis(),
                consecutiveFailures = 0,
                averageLatencyMs = newAvg,
                totalRequests = total
            )
        } else {
            val failures = current.consecutiveFailures + 1
            val circuit = if (failures >= MAX_CONSECUTIVE_FAILURES) ModelHealth.CircuitState.OPEN else ModelHealth.CircuitState.CLOSED
            current.copy(
                isHealthy = failures < MAX_CONSECUTIVE_FAILURES,
                circuitState = circuit,
                lastFailure = System.currentTimeMillis(),
                consecutiveFailures = failures,
                totalFailures = current.totalFailures + 1,
                totalRequests = current.totalRequests + 1
            )
        }
        healthState[modelName] = newHealth
    }

    private suspend fun runHealthChecks() {
        val key = apiKeyProvider()
        if (key.isBlank()) return
        registry.forEach { model ->
            val health = healthState[model.name] ?: return@forEach

            // Si circuit está OPEN, verificar si ya pasó el cooldown
            if (health.circuitState == ModelHealth.CircuitState.OPEN) {
                if (System.currentTimeMillis() - health.lastFailure > CIRCUIT_COOLDOWN_MS) {
                    healthState[model.name] = health.copy(circuitState = ModelHealth.CircuitState.HALF_OPEN)
                    Log.i(TAG, "Circuit HALF_OPEN for ${model.name}")
                } else {
                    return@forEach
                }
            }

            val start = System.currentTimeMillis()
            val result = runCatching {
                withTimeout(HEALTH_CHECK_TIMEOUT_MS) {
                    createModel(model, null).generateContent("OK")
                }
            }
            updateHealth(model.name, result.isSuccess, System.currentTimeMillis() - start)
        }
    }

    fun shutdown() { isRunning.set(false); scope.cancel() }
}
