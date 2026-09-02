package com.ivanna.omega.assistant.core

import com.ivanna.omega.ai.memory.IvannaMemoryArchitecture

object MemoryRetrievalEngine {

    // Simulates an episodic/semantic fusion
    // For an OEM/enterprise setup, this would query a local Room DB or Vector DB
    // Currently, it builds a focused sliding window.
    fun fuseMemory(
        currentQuery: String,
        workingMemory: List<IvannaMemoryArchitecture.WorkingMemory.Interaction>
    ): String {
        if (workingMemory.isEmpty()) return "Sin memoria previa (inicio de sesión)."

        val builder = StringBuilder()
        val window = workingMemory.takeLast(6)

        for (m in window) {
            val prefix = if (m.role == "user") "Usuario" else "IVANNA"
            builder.append("$prefix: ${m.text}\n")
        }

        return builder.toString()
    }
}
