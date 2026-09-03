package com.ivanna.omega.ai.gemini

import android.content.Context
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.util.Log
import com.ivanna.omega.BuildConfig
import com.ivanna.omega.ai.memory.IvannaMemoryArchitecture
import com.ivanna.omega.assistant.core.AdaptiveResponseEngine
import com.ivanna.omega.assistant.core.DynamicContextEngine
import com.ivanna.omega.assistant.core.SecureConfigurationManager
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*

/**
 * IvannaGeminiAgent — Agente conversacional con hardening OEM.
 *
 * Capacidades de producción:
 * - Timeouts seguros en todas las operaciones de red
 * - Cancelación proactiva de requests
 * - Detección de estado offline con degradación graceful
 * - Validación estricta de comandos DSP antes de ejecución
 * - Backoff exponencial con jitter
 * - Métricas de latencia y tasa de éxito
 */
class IvannaGeminiAgent(
    context: Context,
    private val memory: IvannaMemoryArchitecture,
    private val contextEngine: DynamicContextEngine,
    private val scope: CoroutineScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
) {
    companion object {
        private const val TAG = "IvannaGeminiAgent"
        private const val MAX_RETRIES = 3
        private const val REQUEST_TIMEOUT_MS = 15_000L
        private const val STREAM_TIMEOUT_MS = 30_000L
        private const val HEALTH_CHECK_TIMEOUT_MS = 10_000L

        // Comandos DSP validados — whitelist estricta
        val VALID_DSP_COMMANDS = setOf(
            "voice_clarity", "cinema_mode", "music_mode", "concert_mode",
            "spatial_mode", "gentle_mode", "flat_mode", "volume_up",
            "volume_down", "bass_boost", "treble_reduce", "auto_optimize",
            "studio_reference", "bass_boost_preset", "vocal_clarity_preset",
            "live_room_preset", "cinematic_preset", "electronic_preset",
            "acoustic_preset", "rock_preset", "podcast_preset"
        )
    }

    private val adaptiveEngine = AdaptiveResponseEngine()
    private val secureConfig = SecureConfigurationManager
    private val connectivityManager = context.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager

    private val orchestrator by lazy {
        GeminiOrchestrator(
            apiKeyProvider = { resolveApiKey(context) },
            scope = scope
        )
    }

    private val _agentState = MutableStateFlow(AgentState.IDLE)
    val agentState: StateFlow<AgentState> = _agentState.asStateFlow()

    // Métricas OEM
    private val _metrics = MutableStateFlow(AgentMetrics())
    val metrics: StateFlow<AgentMetrics> = _metrics.asStateFlow()

    enum class AgentState { IDLE, THINKING, RESPONDING, ERROR, OFFLINE }

    data class AgentMetrics(
        val totalRequests: Int = 0,
        val successfulRequests: Int = 0,
        val failedRequests: Int = 0,
        val averageLatencyMs: Long = 0,
        val lastLatencyMs: Long = 0,
        val offlineFallbacks: Int = 0
    )

    sealed class AgentResponse {
        data class Success(
            val text: String,
            val commands: List<String>,
            val profileUsed: AdaptiveResponseEngine.ResponseProfile,
            val modelUsed: String
        ) : AgentResponse()

        data class Error(val message: String) : AgentResponse()
        data class Offline(val cachedResponse: String?) : AgentResponse()
    }

    /** Verifica si el dispositivo tiene conectividad de red. */
    private fun isNetworkAvailable(): Boolean {
        val network = connectivityManager.activeNetwork ?: return false
        val capabilities = connectivityManager.getNetworkCapabilities(network) ?: return false
        return capabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
    }

    /**
     * Procesa query del usuario con hardening completo.
     */
    suspend fun processQuery(
        userQuery: String,
        onPartialResponse: ((String) -> Unit)? = null
    ): AgentResponse = withContext(Dispatchers.IO) {
        _metrics.update { it.copy(totalRequests = it.totalRequests + 1) }

        // 1. Verificar conectividad
        if (!isNetworkAvailable()) {
            _agentState.value = AgentState.OFFLINE
            _metrics.update { it.copy(offlineFallbacks = it.offlineFallbacks + 1) }
            return@withContext AgentResponse.Offline(
                cachedResponse = memory.getUserPreference("last_response_$userQuery")
            )
        }

        _agentState.value = AgentState.THINKING
        val startTime = System.currentTimeMillis()

        try {
            withTimeout(REQUEST_TIMEOUT_MS) {
                val profile = adaptiveEngine.analyzeComplexity(
                    query = userQuery,
                    uiContext = contextEngine.getCurrentScreenContext()
                )

                val richContext = buildSystemPrompt(userQuery)
                val result = generateWithRetry(richContext, profile, onPartialResponse)

                val latency = System.currentTimeMillis() - startTime
                updateMetrics(success = result.isSuccess, latency = latency)

                result.fold(
                    onSuccess = { responseText ->
                        _agentState.value = AgentState.RESPONDING

                        // Validación estricta de comandos DSP
                        val rawCommands = extractDSPCommands(responseText)
                        val validatedCommands = rawCommands.filter { it in VALID_DSP_COMMANDS }

                        if (rawCommands.size != validatedCommands.size) {
                            val invalid = rawCommands - validatedCommands.toSet()
                            Log.w(TAG, "Comandos DSP inválidos rechazados: $invalid")
                        }

                        val cleanText = cleanResponse(responseText)

                        // Persistir interacción
                        memory.recordInteraction("user", userQuery)
                        memory.recordInteraction("assistant", cleanText)

                        scope.launch {
                            memory.persistSession(
                                sessionId = generateSessionId(),
                                userQuery = userQuery,
                                ivannaResponse = cleanText,
                                actions = validatedCommands
                            )
                        }

                        _agentState.value = AgentState.IDLE
                        AgentResponse.Success(
                            text = cleanText,
                            commands = validatedCommands,
                            profileUsed = profile,
                            modelUsed = orchestrator.state.value.activeModel
                        )
                    },
                    onFailure = { error ->
                        _agentState.value = AgentState.ERROR
                        Log.e(TAG, "Gemini generation failed", error)
                        AgentResponse.Error(error.message ?: "Error desconocido")
                    }
                )
            }
        } catch (e: TimeoutCancellationException) {
            _agentState.value = AgentState.ERROR
            updateMetrics(success = false, latency = System.currentTimeMillis() - startTime)
            Log.e(TAG, "Request timeout after ${REQUEST_TIMEOUT_MS}ms")
            AgentResponse.Error("La solicitud excedió el tiempo límite. Intenta de nuevo.")
        } catch (e: Exception) {
            _agentState.value = AgentState.ERROR
            updateMetrics(success = false, latency = System.currentTimeMillis() - startTime)
            AgentResponse.Error(e.message ?: "Error crítico")
        }
    }

    private suspend fun generateWithRetry(
        prompt: String,
        profile: AdaptiveResponseEngine.ResponseProfile,
        onPartial: ((String) -> Unit)?
    ): Result<String> {
        repeat(MAX_RETRIES) { attempt ->
            val result = if (onPartial != null) {
                val sb = StringBuilder()
                try {
                    withTimeout(STREAM_TIMEOUT_MS) {
                        orchestrator.generateContentStream(prompt, profile).collect { partial ->
                            sb.clear()
                            sb.append(partial)
                            onPartial(partial)
                        }
                    }
                    Result.success(sb.toString())
                } catch (e: TimeoutCancellationException) {
                    Result.failure(e)
                } catch (e: CancellationException) {
                    throw e
                } catch (e: Exception) {
                    Result.failure(e)
                }
            } else {
                try {
                    withTimeout(REQUEST_TIMEOUT_MS) {
                        orchestrator.generateContent(prompt, profile)
                    }
                } catch (e: TimeoutCancellationException) {
                    Result.failure(e)
                }
            }

            result.onSuccess { return Result.success(it) }
                .onFailure { error ->
                    Log.w(TAG, "Attempt ${attempt + 1}/$MAX_RETRIES failed: ${error.message}")
                    if (attempt < MAX_RETRIES - 1) {
                        // Backoff exponencial con jitter: 1s, 2s, 4s
                        val delayMs = (1000L * (1 shl attempt)) + (0..500).random()
                        delay(delayMs)
                    }
                }
        }
        return Result.failure(Exception("Max retries ($MAX_RETRIES) exceeded"))
    }

    private suspend fun buildSystemPrompt(userQuery: String): String {
        val memoryContext = memory.buildContextForGemini(userQuery)
        val deviceContext = contextEngine.buildFullContext()

        return buildString {
            appendLine("Eres IVANNA OMEGA SUPREME, asistente de inteligencia acústica OEM.")
            appendLine("Controlas el sistema de audio del dispositivo en tiempo real.")
            appendLine()
            appendLine("=== CONTEXTO DEL DISPOSITIVO ===")
            appendLine(deviceContext)
            appendLine()
            if (memoryContext.isNotBlank()) {
                appendLine("=== MEMORIA RELEVANTE ===")
                appendLine(memoryContext)
                appendLine()
            }
            appendLine("=== PROTOCOLO DE COMANDOS (WHITELIST) ===")
            appendLine("Cuando el usuario pida un ajuste de audio, emite EXACTAMENTE: [CMD:nombre_comando]")
            appendLine("Comandos válidos:")
            VALID_DSP_COMMANDS.forEach { appendLine("  - $it") }
            appendLine()
            appendLine("REGLAS DE SEGURIDAD:")
            appendLine("- NUNCA emitas un comando que no esté en la lista anterior.")
            appendLine("- NUNCA pidas información personal del usuario.")
            appendLine("- NUNCA ejecutes comandos del sistema operativo.")
            appendLine()
            appendLine("=== CONSULTA ===")
            appendLine(userQuery)
        }
    }

    private fun extractDSPCommands(response: String): List<String> =
        """\[CMD:([a-z_]+)\]""".toRegex().findAll(response).map { it.groupValues[1] }.toList()

    private fun cleanResponse(response: String): String = response.replace("""\[CMD:[a-z_]+\]""".toRegex(), "").trim()

    private fun resolveApiKey(context: Context): String {
        if (!secureConfig.state.value.isInitialized) secureConfig.initialize(context)
        return secureConfig.getApiKey().takeIf { it.isNotBlank() } ?: BuildConfig.GEMINI_API_KEY ?: ""
    }

    private fun generateSessionId(): String = "sess_${System.currentTimeMillis()}"

    private fun updateMetrics(success: Boolean, latency: Long) {
        _metrics.update { current ->
            val total = current.totalRequests.coerceAtLeast(1)
            val newAvg = if (total == 1) latency else (current.averageLatencyMs * (total - 1) + latency) / total
            current.copy(
                successfulRequests = if (success) current.successfulRequests + 1 else current.successfulRequests,
                failedRequests = if (!success) current.failedRequests + 1 else current.failedRequests,
                averageLatencyMs = newAvg,
                lastLatencyMs = latency
            )
        }
    }

    fun shutdown() {
        scope.cancel()
        orchestrator.shutdown()
        // FIX (leak real): memory es una dependencia inyectada por constructor
        // que este agente es el único dueño funcional de aquí — antes solo se
        // cancelaba scope/orchestrator y la IvannaMemoryArchitecture inyectada
        // (con su PROPIO CoroutineScope + EncryptedFile) quedaba viva para
        // siempre. Impacto real:
        //   1. NetworkStatusPanel crea un probeAgent desechable en cada tap de
        //      "Conectar" con `IvannaMemoryArchitecture(ctx)` nueva — sin este
        //      fix, cada tap dejaba un CoroutineScope de memoria huérfano
        //      corriendo para siempre (mismo patrón de leak ya documentado
        //      para geminiAgent en IVANNAApplication, pero sin cerrar aquí).
        //   2. IvannaAssistantViewModel: la memoria real de la sesión nunca
        //      se persistía/cancelaba al salir de la pantalla (onCleared()).
        // shutdown() ya guarda a disco antes de cancelar su scope (ver
        // IvannaMemoryArchitecture.shutdown()), así que esto también cierra
        // en falso el caso de "memoria de sesión no se guarda al cerrar".
        memory.shutdown()
    }
}
