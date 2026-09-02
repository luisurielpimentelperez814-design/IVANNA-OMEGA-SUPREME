package com.ivanna.omega.ai.gemini

import android.util.Log
import com.google.ai.client.generativeai.GenerativeModel
import com.google.ai.client.generativeai.type.content
import com.ivanna.omega.assistant.core.AdaptiveResponseEngine.ResponseProfile
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicBoolean

class GeminiOrchestrator(
    private val apiKeyProvider: () -> String,
    private val scope: CoroutineScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
) {
    companion object {
        private const val TAG = "GeminiOrchestrator"
        private const val HEALTH_CHECK_INTERVAL_MS = 30_000L
        private const val MAX_CONSECUTIVE_FAILURES = 3
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
        val lastSuccess: Long = 0,
        val lastFailure: Long = 0,
        val consecutiveFailures: Int = 0,
        val averageLatencyMs: Long = 0,
        val totalRequests: Int = 0,
        val totalFailures: Int = 0
    )

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
            while (isRunning.get()) { runHealthChecks(); delay(HEALTH_CHECK_INTERVAL_MS) }
        }
    }

    suspend fun generateContent(prompt: String, responseProfile: ResponseProfile = ResponseProfile.NORMAL, systemInstruction: String? = null, imageData: List<ByteArray>? = null): Result<String> = withContext(Dispatchers.IO) {
        val model = selectModel(responseProfile, imageData != null)
        val instance = createModel(model, systemInstruction)
        val startTime = System.currentTimeMillis()
        val result = runCatching { instance.generateContent(prompt).text ?: "" }
        val latency = System.currentTimeMillis() - startTime
        updateHealth(model.name, result.isSuccess, latency)

        result.onSuccess {
            _state.update { s -> s.copy(activeModel = model.name, isHealthy = true, lastLatencyMs = latency, totalRequests = s.totalRequests + 1, healthMap = healthState.toMap()) }
        }.onFailure { error ->
            Log.w(TAG, "Model ${model.name} failed: ${error.message}")
            val fallback = findFallback(model, responseProfile, imageData != null)
            if (fallback != null) {
                Log.i(TAG, "Falling back to ${fallback.name}")
                _state.update { it.copy(fallbackCount = it.fallbackCount + 1) }
                val fbInstance = createModel(fallback, systemInstruction)
                val fbResult = runCatching { fbInstance.generateContent(prompt).text ?: "" }
                updateHealth(fallback.name, fbResult.isSuccess, System.currentTimeMillis() - startTime)
                fbResult.onSuccess { _state.update { s -> s.copy(activeModel = fallback.name, isHealthy = true, lastLatencyMs = System.currentTimeMillis() - startTime, totalRequests = s.totalRequests + 1, healthMap = healthState.toMap()) } }
                return@withContext fbResult
            } else { _state.update { it.copy(isHealthy = false, healthMap = healthState.toMap()) } }
        }
        result
    }

    suspend fun generateContentStream(prompt: String, responseProfile: ResponseProfile = ResponseProfile.NORMAL, systemInstruction: String? = null): Flow<String> = flow {
        val model = selectModel(responseProfile, false)
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
        return capable.sortedWith(compareByDescending<ModelEntry> { healthState[it.name]?.isHealthy == true }.thenBy { healthState[it.name]?.averageLatencyMs ?: Long.MAX_VALUE }).firstOrNull() ?: registry.first()
    }

    private fun findFallback(failed: ModelEntry, profile: ResponseProfile, needsVision: Boolean): ModelEntry? {
        val idx = registry.indexOfFirst { it.name == failed.name }
        return registry.drop(idx + 1).filter { !needsVision || it.supportsVision }.firstOrNull { healthState[it.name]?.isHealthy != false }
    }

    private fun createModel(entry: ModelEntry, systemInstruction: String?): GenerativeModel {
        val builder = GenerativeModel.builder().modelName(entry.name).apiKey(apiKeyProvider())
        systemInstruction?.let { builder.systemInstruction(content { text(it) }) }
        return builder.build()
    }

    private fun updateHealth(modelName: String, success: Boolean, latencyMs: Long) {
        val current = healthState[modelName] ?: ModelHealth(modelName = modelName)
        val newHealth = if (success) {
            val total = current.totalRequests + 1
            val newAvg = if (current.totalRequests == 0) latencyMs else (current.averageLatencyMs * current.totalRequests + latencyMs) / total
            current.copy(isHealthy = true, lastSuccess = System.currentTimeMillis(), consecutiveFailures = 0, averageLatencyMs = newAvg, totalRequests = total)
        } else {
            val failures = current.consecutiveFailures + 1
            current.copy(isHealthy = failures < MAX_CONSECUTIVE_FAILURES, lastFailure = System.currentTimeMillis(), consecutiveFailures = failures, totalFailures = current.totalFailures + 1, totalRequests = current.totalRequests + 1)
        }
        healthState[modelName] = newHealth
    }

    private suspend fun runHealthChecks() {
        val key = apiKeyProvider()
        if (key.isBlank()) return
        registry.forEach { model ->
            val start = System.currentTimeMillis()
            val result = runCatching { createModel(model, null).generateContent("OK") }
            updateHealth(model.name, result.isSuccess, System.currentTimeMillis() - start)
        }
    }

    fun shutdown() { isRunning.set(false); scope.cancel() }
}
