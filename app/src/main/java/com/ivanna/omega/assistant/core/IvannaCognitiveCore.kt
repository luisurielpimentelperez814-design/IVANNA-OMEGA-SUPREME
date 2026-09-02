package com.ivanna.omega.assistant.core

import com.ivanna.omega.ai.gemini.IvannaGeminiAgent
import com.ivanna.omega.ai.memory.IvannaMemoryArchitecture
import com.ivanna.omega.assistant.IvannaDSPOrchestrator
import com.ivanna.omega.assistant.IvannaIntentMapper
import com.ivanna.omega.assistant.IvannaMusicalIntentEngine
import com.ivanna.omega.agent.IvannaAgentCore
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*

class IvannaCognitiveCore(
    private val memory: IvannaMemoryArchitecture,
    private val contextEngine: DynamicContextEngine,
    private val geminiAgent: IvannaGeminiAgent,
    private val intentMapper: IvannaIntentMapper,
    private val musicalEngine: IvannaMusicalIntentEngine,
    private val dspOrchestrator: IvannaDSPOrchestrator,
    private val agentCore: IvannaAgentCore,
    private val scope: CoroutineScope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
) {
    private val _cognitiveState = MutableStateFlow(CognitiveState.IDLE)
    val cognitiveState: StateFlow<CognitiveState> = _cognitiveState.asStateFlow()

    enum class CognitiveState { IDLE, PERCEIVING, REASONING, ACTING, REFLECTING }

    sealed class CognitiveResult {
        data class DirectAction(val actions: List<String>, val explanation: String) : CognitiveResult()
        data class GeminiResponse(val text: String, val commandsExecuted: List<String>, val modelUsed: String, val profileUsed: AdaptiveResponseEngine.ResponseProfile) : CognitiveResult()
        data class Error(val message: String) : CognitiveResult()
    }

    suspend fun processUserInput(input: String): CognitiveResult {
        _cognitiveState.value = CognitiveState.PERCEIVING
        val musicalIntent = musicalEngine.detectIntent(input)

        if (musicalIntent.isPureMusicalCommand && musicalIntent.confidence > 0.8f) {
            _cognitiveState.value = CognitiveState.ACTING
            val actions = intentMapper.mapToActions(musicalIntent)
            actions.forEach { action -> runCatching { dspOrchestrator.executeAction(action) } }
            _cognitiveState.value = CognitiveState.REFLECTING
            memory.recordInteraction("user", input)
            memory.recordInteraction("assistant", "Acción ejecutada: ${actions.joinToString(", ")}")
            return CognitiveResult.DirectAction(actions = actions.map { it.command }, explanation = musicalIntent.explanation)
        }

        _cognitiveState.value = CognitiveState.REASONING
        return when (val geminiResult = geminiAgent.processQuery(input)) {
            is IvannaGeminiAgent.AgentResponse.Success -> {
                _cognitiveState.value = CognitiveState.ACTING
                if (geminiResult.commands.isNotEmpty()) {
                    geminiResult.commands.forEach { cmd ->
                        val action = intentMapper.mapCommand(cmd)
                        action?.let { runCatching { dspOrchestrator.executeAction(it) } }
                    }
                }
                _cognitiveState.value = CognitiveState.REFLECTING
                scope.launch {
                    memory.learnFact(key = "query_pattern_${System.currentTimeMillis()}", value = input,
                        category = IvannaMemoryArchitecture.SemanticRecord.SemanticCategory.LEARNED_FACT)
                }
                CognitiveResult.GeminiResponse(text = geminiResult.text, commandsExecuted = geminiResult.commands,
                    modelUsed = geminiResult.modelUsed, profileUsed = geminiResult.profileUsed)
            }
            is IvannaGeminiAgent.AgentResponse.Error -> { _cognitiveState.value = CognitiveState.IDLE; CognitiveResult.Error(geminiResult.message) }
        }
    }

    fun explainLastDecision(): String {
        val lastState = _cognitiveState.value
        val agentLog = agentCore.getLastDecisionLog()
        return buildString {
            appendLine("Estado cognitivo: $lastState")
            agentLog?.let { appendLine("Decisión: ${it.decision}"); appendLine("Razón: ${it.reason}"); appendLine("Confianza: ${(it.confidence * 100).toInt()}%") }
                ?: appendLine("Sin registro de decisión reciente.")
        }
    }
}
