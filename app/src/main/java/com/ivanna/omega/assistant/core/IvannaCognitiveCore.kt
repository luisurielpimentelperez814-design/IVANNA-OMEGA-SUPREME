package com.ivanna.omega.assistant.core

import com.ivanna.omega.ai.gemini.IvannaGeminiAgent
import com.ivanna.omega.ai.memory.IvannaMemoryArchitecture
import com.ivanna.omega.assistant.IvannaDSPOrchestrator
import com.ivanna.omega.agent.IvannaAgentCore
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*

/**
 * IvannaCognitiveCore (assistant.core) — adaptador entre el nuevo
 * IvannaGeminiAgent (clase instanciable) y el ViewModel/Orquestrador.
 *
 * Corregido para usar solo APIs que realmente existen:
 *  - IvannaGeminiAgent.processQuery() → AgentResponse.Success | Error | Offline
 *  - IvannaDSPOrchestrator.executeCommand(cmd: String): Boolean
 *  - IvannaAgentCore.recentDecisions() (no getLastDecisionLog)
 *  - IvannaMemoryArchitecture.recordInteraction / learnFact
 */
class IvannaCognitiveCore(
    private val memory: IvannaMemoryArchitecture,
    private val contextEngine: DynamicContextEngine,
    private val geminiAgent: IvannaGeminiAgent,
    private val dspOrchestrator: IvannaDSPOrchestrator,
    private val agentCore: IvannaAgentCore,
    private val scope: CoroutineScope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
) {
    private val _cognitiveState = MutableStateFlow(CognitiveState.IDLE)
    val cognitiveState: StateFlow<CognitiveState> = _cognitiveState.asStateFlow()

    enum class CognitiveState { IDLE, PERCEIVING, REASONING, ACTING, REFLECTING }

    sealed class CognitiveResult {
        data class DirectAction(
            val actions: List<String>,
            val explanation: String
        ) : CognitiveResult()

        data class GeminiResponse(
            val text: String,
            val commandsExecuted: List<String>,
            val modelUsed: String,
            val profileUsed: AdaptiveResponseEngine.ResponseProfile
        ) : CognitiveResult()

        data class Offline(val cachedText: String?) : CognitiveResult()
        data class Error(val message: String) : CognitiveResult()
    }

    suspend fun processUserInput(input: String): CognitiveResult {
        _cognitiveState.value = CognitiveState.PERCEIVING

        _cognitiveState.value = CognitiveState.REASONING
        return when (val geminiResult = geminiAgent.processQuery(input)) {
            is IvannaGeminiAgent.AgentResponse.Success -> {
                _cognitiveState.value = CognitiveState.ACTING
                // Ejecutar comandos DSP si los hay
                val executed = mutableListOf<String>()
                geminiResult.commands.forEach { cmd ->
                    val ok = runCatching { dspOrchestrator.executeCommand(cmd) }.getOrDefault(false)
                    if (ok) executed.add(cmd)
                }
                _cognitiveState.value = CognitiveState.REFLECTING
                // Registrar en memoria a largo plazo
                scope.launch {
                    memory.recordInteraction("user", input)
                    memory.recordInteraction("assistant", geminiResult.text)
                    if (input.length > 20) {
                        runCatching {
                            memory.learnFact(
                                key = "query_${System.currentTimeMillis()}",
                                value = input,
                                category = IvannaMemoryArchitecture.SemanticRecord.SemanticCategory.LEARNED_FACT
                            )
                        }
                    }
                }
                CognitiveResult.GeminiResponse(
                    text = geminiResult.text,
                    commandsExecuted = executed,
                    modelUsed = geminiResult.modelUsed,
                    profileUsed = geminiResult.profileUsed
                )
            }
            is IvannaGeminiAgent.AgentResponse.Offline -> {
                _cognitiveState.value = CognitiveState.IDLE
                CognitiveResult.Offline(geminiResult.cachedResponse)
            }
            is IvannaGeminiAgent.AgentResponse.Error -> {
                _cognitiveState.value = CognitiveState.IDLE
                CognitiveResult.Error(geminiResult.message)
            }
        }
    }

    fun explainLastDecision(): String {
        val lastState = _cognitiveState.value
        // IvannaAgentCore expone recentDecisions(), no getLastDecisionLog()
        val lastRecord = agentCore.recentDecisions().lastOrNull()
        return buildString {
            appendLine("Estado cognitivo: $lastState")
            if (lastRecord != null) {
                appendLine("Decisión: ${lastRecord.action}")
                appendLine("Razón: ${lastRecord.reason}")
                appendLine("Timestamp: ${lastRecord.timestampMs}")
            } else {
                appendLine("Sin registro de decisión reciente.")
            }
        }
    }
}
