package com.ivanna.omega.ai.memory

@Deprecated("Usar IvannaMemoryArchitecture directamente", ReplaceWith("IvannaMemoryArchitecture"))
class ConversationMemory(private val architecture: IvannaMemoryArchitecture) {
    fun addUserMessage(text: String) = architecture.recordInteraction("user", text)
    fun addAssistantMessage(text: String) = architecture.recordInteraction("assistant", text)
    suspend fun getContext(): String = architecture.buildContextForGemini("")
}
