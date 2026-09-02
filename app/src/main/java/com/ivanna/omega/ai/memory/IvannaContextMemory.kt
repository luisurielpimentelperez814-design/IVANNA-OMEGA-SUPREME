package com.ivanna.omega.ai.memory

@Deprecated("Usar IvannaMemoryArchitecture directamente", ReplaceWith("IvannaMemoryArchitecture"))
class IvannaContextMemory(private val architecture: IvannaMemoryArchitecture) {
    suspend fun getContext(): String = architecture.buildContextForGemini("")
}
