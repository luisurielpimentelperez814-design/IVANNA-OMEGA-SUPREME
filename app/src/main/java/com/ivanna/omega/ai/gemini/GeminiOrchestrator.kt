package com.ivanna.omega.ai.gemini

import android.util.Log
import com.google.ai.client.generativeai.type.content
import com.ivanna.omega.assistant.core.AdaptiveResponseEngine.ResponseProfile
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger

// MIGRACIÓN FIREBASE AI LOGIC (2026-09-04) — motivo: Google retira las API
// keys "AIza" tradicionales este mes y las nuevas "AQ." (Auth key) están
// confirmadas, con múltiples fuentes independientes recientes (incluido el
// propio foro de Google), rotas contra el endpoint REST simple sin importar
// header/SDK — no es arreglable del lado cliente. Firebase AI Logic evita
// el problema por completo: el proyecto ya tiene un patrón establecido
// (CloudSyncManager.kt) de inicializar Firebase manualmente vía
// FirebaseOptions.Builder — 3 constantes, sin el plugin
// com.google.gms.google-services ni google-services.json. Se reusa esa
// MISMA inicialización (CloudSyncManager.ensureFirebaseAppReady(), llamada
// desde IvannaGeminiAgent al construirse) en vez de duplicar otro camino de
// setup — es el mismo proyecto Firebase, AI Logic es solo otro producto
// sobre él, no otro proyecto.
//
// GenerativeModel de Firebase AI Logic (com.google.firebase.ai) es API-
// compatible con el SDK viejo en el único punto que usa este archivo:
// generateContent(prompt).text y generateContentStream(prompt) con .text
// por chunk — mismo shape, mismo nombre de método. Por eso el circuit
// breaker/registry/retry de abajo NO se toca: solo generateWith()/streamWith()
// deciden, en cada llamada, cuál de los dos construir y usar.
//
// PENDIENTE DEL LADO DEL USUARIO (no lo puedo hacer yo): rellenar las 3
// constantes FIREBASE_* en CloudSyncManager.kt con los valores reales de su
// proyecto Firebase (Project ID, App ID, Web API Key — los 3 se sacan de
// Configuración del proyecto → Tus apps, sin descargar ningún archivo).
// Mientras isConfigured sea false, firebaseAvailable() da false y se sigue
// usando el SDK legacy con la key que haya en SecureConfigurationManager
// (AIza restringida como mitigación, per conversación previa) — cero
// regresión para quien no haya migrado todavía.
import com.google.firebase.Firebase
import com.google.firebase.ai.GenerativeModel as FirebaseGenerativeModel
import com.google.firebase.ai.ai
import com.google.firebase.ai.type.GenerativeBackend
import com.google.firebase.ai.type.content as firebaseContent
import com.google.firebase.FirebaseApp
import com.google.ai.client.generativeai.GenerativeModel as LegacyGenerativeModel

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
        ModelEntry("gemini-2.5-pro", "Gemini 2.5 Pro", 1_000_000, 8192, true, true, true, ModelEntry.CostTier.PREMIUM, ModelEntry.LatencyProfile.MODERATE),
        ModelEntry("gemini-2.5-flash", "Gemini 2.5 Flash", 1_000_000, 8192, true, true, true, ModelEntry.CostTier.STANDARD, ModelEntry.LatencyProfile.FAST),
        ModelEntry("gemini-2.5-flash-lite", "Gemini 2.5 Flash-Lite", 1_000_000, 8192, true, false, true, ModelEntry.CostTier.ECONOMY, ModelEntry.LatencyProfile.VERY_FAST)
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

        val startTime = System.currentTimeMillis()
        val result = runCatching { generateWith(model, systemInstruction, prompt) }
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

            val fbStart = System.currentTimeMillis()
            val fbResult = runCatching { generateWith(fallback, systemInstruction, prompt) }
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
        val startTime = System.currentTimeMillis()
        try {
            var acc = ""
            streamWith(model, systemInstruction, prompt).collect { text ->
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

    /** true si el FirebaseApp por defecto ya fue inicializado (ver CloudSyncManager.ensureFirebaseAppReady()). */
    private fun firebaseAvailable(): Boolean =
        runCatching { FirebaseApp.getInstance(); true }.getOrDefault(false)

    private fun createLegacyModel(entry: ModelEntry, systemInstruction: String?): LegacyGenerativeModel {
        // FIX (CI rojo): el SDK generativeai:0.9.0 no expone GenerativeModel.Builder;
        // GenerativeModel se instancia por constructor (mismo patrón que
        // assistant/core/GeminiOrchestrator.createAdaptiveModel).
        return LegacyGenerativeModel(
            modelName = entry.name,
            apiKey = apiKeyProvider(),
            systemInstruction = systemInstruction?.let { content { text(it) } }
        )
    }

    private fun createFirebaseModel(entry: ModelEntry, systemInstruction: String?): FirebaseGenerativeModel {
        // Backend "Gemini Developer API" vía Firebase — mismo backend gratuito
        // que el SDK legacy usaba directo con key, ahora autenticado por
        // App Check en vez de una API key viajando por la red.
        return Firebase.ai(backend = GenerativeBackend.googleAI())
            .generativeModel(
                modelName = entry.name,
                systemInstruction = systemInstruction?.let { firebaseContent { text(it) } }
            )
    }

    /** Genera texto con el backend que esté realmente disponible ahora mismo. */
    private suspend fun generateWith(entry: ModelEntry, systemInstruction: String?, prompt: String): String =
        if (firebaseAvailable()) {
            createFirebaseModel(entry, systemInstruction).generateContent(prompt).text ?: ""
        } else {
            createLegacyModel(entry, systemInstruction).generateContent(prompt).text ?: ""
        }

    /** Streaming con el backend que esté realmente disponible ahora mismo. */
    private fun streamWith(entry: ModelEntry, systemInstruction: String?, prompt: String): Flow<String> =
        if (firebaseAvailable()) {
            createFirebaseModel(entry, systemInstruction).generateContentStream(prompt).map { it.text ?: "" }
        } else {
            createLegacyModel(entry, systemInstruction).generateContentStream(prompt).map { it.text ?: "" }
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
        if (!firebaseAvailable() && apiKeyProvider().isBlank()) return
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
                    generateWith(model, null, "OK")
                }
            }
            updateHealth(model.name, result.isSuccess, System.currentTimeMillis() - start)
        }
    }

    fun shutdown() { isRunning.set(false); scope.cancel() }
}
