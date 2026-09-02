package com.ivanna.omega.ai.gemini

import android.content.Context
import android.util.Log
import com.ivanna.omega.BuildConfig
import com.ivanna.omega.ai.memory.IvannaMemoryArchitecture
import com.ivanna.omega.assistant.core.AdaptiveResponseEngine
import com.ivanna.omega.assistant.core.DynamicContextEngine
import com.ivanna.omega.assistant.core.SecureConfigurationManager
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*

class IvannaGeminiAgent(
    context: Context,
    private val memory: IvannaMemoryArchitecture,
    private val contextEngine: DynamicContextEngine,
    private val scope: CoroutineScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
) {
    companion object {
        private const val TAG = "IvannaGeminiAgent"
        private const val MAX_RETRIES = 2
    }

    private val adaptiveEngine = AdaptiveResponseEngine()
    private val secureConfig = SecureConfigurationManager

    private val orchestrator by lazy {
        GeminiOrchestrator(apiKeyProvider = { resolveApiKey(context) }, scope = scope)
    }

    private val _agentState = MutableStateFlow(AgentState.IDLE)
    val agentState: StateFlow<AgentState> = _agentState.asStateFlow()

    enum class AgentState { IDLE, THINKING, RESPONDING, ERROR }

    sealed class AgentResponse {
        data class Success(val text: String, val commands: List<String>, val profileUsed: AdaptiveResponseEngine.ResponseProfile, val modelUsed: String) : AgentResponse()
        data class Error(val message: String) : AgentResponse()
    }

    suspend fun processQuery(userQuery: String, onPartialResponse: ((String) -> Unit)? = null): AgentResponse = withContext(Dispatchers.IO) {
        _agentState.value = AgentState.THINKING
        try {
            val profile = adaptiveEngine.analyzeComplexity(query = userQuery, uiContext = contextEngine.getCurrentScreenContext())
            val richContext = buildSystemPrompt(userQuery)
            val result = generateWithRetry(richContext, profile, onPartialResponse)

            result.fold(
                onSuccess = { responseText ->
                    _agentState.value = AgentState.RESPONDING
                    val commands = extractDSPCommands(responseText)
                    val cleanText = cleanResponse(responseText)
                    memory.recordInteraction("user", userQuery)
                    memory.recordInteraction("assistant", cleanText)
                    scope.launch { memory.persistSession(sessionId = generateSessionId(), userQuery = userQuery, ivannaResponse = cleanText, actions = commands) }
                    _agentState.value = AgentState.IDLE
                    AgentResponse.Success(text = cleanText, commands = commands, profileUsed = profile, modelUsed = orchestrator.state.value.activeModel)
                },
                onFailure = { error ->
                    _agentState.value = AgentState.ERROR
                    Log.e(TAG, "Gemini generation failed", error)
                    AgentResponse.Error(error.message ?: "Error desconocido")
                }
            )
        } catch (e: Exception) {
            _agentState.value = AgentState.ERROR
            AgentResponse.Error(e.message ?: "Error crítico")
        }
    }

    private suspend fun generateWithRetry(prompt: String, profile: AdaptiveResponseEngine.ResponseProfile, onPartial: ((String) -> Unit)?): Result<String> {
        repeat(MAX_RETRIES) { attempt ->
            val result = if (onPartial != null) {
                val sb = StringBuilder()
                try {
                    orchestrator.generateContentStream(prompt, profile).collect { partial ->
                        sb.clear(); sb.append(partial); onPartial(partial)
                    }
                    Result.success(sb.toString())
                } catch (e: Exception) { Result.failure(e) }
            } else {
                orchestrator.generateContent(prompt, profile)
            }
            result.onSuccess { return Result.success(it) }
                .onFailure { error ->
                    Log.w(TAG, "Attempt ${attempt + 1} failed: ${error.message}")
                    if (attempt < MAX_RETRIES - 1) delay(1000L * (attempt + 1))
                }
        }
        return Result.failure(Exception("Max retries exceeded"))
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
            appendLine("=== PROTOCOLO DE COMANDOS ===")
            appendLine("Cuando el usuario pida un ajuste de audio, emite: [CMD:nombre_comando]")
            appendLine("Comandos: voice_clarity, cinema_mode, music_mode, concert_mode, spatial_mode,")
            appendLine("gentle_mode, flat_mode, volume_up, volume_down, bass_boost, treble_reduce, auto_optimize")
            appendLine()
            appendLine("=== CONSULTA ===")
            appendLine(userQuery)
        }
    }

    private fun extractDSPCommands(response: String): List<String> =
        "\[CMD:([a-z_]+)\]".toRegex().findAll(response).map { it.groupValues[1] }.toList()

    private fun cleanResponse(response: String): String = response.replace("\[CMD:[a-z_]+\]".toRegex(), "").trim()

    private fun resolveApiKey(context: Context): String {
        if (!secureConfig.state.value.isInitialized) secureConfig.initialize(context)
        return secureConfig.getApiKey().takeIf { it.isNotBlank() } ?: BuildConfig.GEMINI_API_KEY ?: ""
    }

    private fun generateSessionId(): String = "sess_${System.currentTimeMillis()}"
    fun shutdown() { orchestrator.shutdown(); scope.cancel() }
}
