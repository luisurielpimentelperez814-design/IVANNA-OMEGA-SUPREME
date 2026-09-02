package com.ivanna.omega.assistant.core

import com.google.ai.client.generativeai.GenerativeModel
import com.google.ai.client.generativeai.type.content
import com.google.ai.client.generativeai.type.generationConfig
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import android.util.Log

enum class ResponseProfile {
    FAST,
    NORMAL,
    DEEP_REASONING,
    ENGINEERING_MODE
}

object GeminiOrchestrator {
    private const val TAG = "GeminiOrchestrator"

    private fun getBestModelName(profile: ResponseProfile): String {
        return when(profile) {
            ResponseProfile.FAST             -> "gemini-2.0-flash"
            ResponseProfile.NORMAL           -> "gemini-2.0-flash"
            ResponseProfile.DEEP_REASONING   -> "gemini-1.5-pro"
            ResponseProfile.ENGINEERING_MODE -> "gemini-1.5-pro"
        }
    }

    suspend fun createAdaptiveModel(profile: ResponseProfile): GenerativeModel? {
        val apiKey = SecureConfigurationManager.getApiKey()
        if (apiKey.isBlank()) return null

        // Usar AIContextManager (object estático) en lugar de DynamicContextEngine (instancia)
        val dynamicContext = AIContextManager.getFullSystemPrompt()
        val targetModel = getBestModelName(profile)

        return GenerativeModel(
            modelName = targetModel,
            apiKey = apiKey,
            generationConfig = generationConfig {
                temperature = when(profile) {
                    ResponseProfile.FAST             -> 0.3f
                    ResponseProfile.NORMAL           -> 0.6f
                    ResponseProfile.DEEP_REASONING   -> 0.4f
                    ResponseProfile.ENGINEERING_MODE -> 0.2f
                }
                maxOutputTokens = when(profile) {
                    ResponseProfile.FAST             -> 300
                    ResponseProfile.NORMAL           -> 800
                    ResponseProfile.DEEP_REASONING   -> 1200
                    ResponseProfile.ENGINEERING_MODE -> 1500
                }
            },
            systemInstruction = content {
                text(dynamicContext)
            }
        )
    }
}
