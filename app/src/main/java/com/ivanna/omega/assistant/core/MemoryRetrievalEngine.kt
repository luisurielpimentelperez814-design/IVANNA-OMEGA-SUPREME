package com.ivanna.omega.assistant.core

import com.ivanna.omega.assistant.core.IvannaCognitiveCore.Interaction

object MemoryRetrievalEngine {

    // Simulates an episodic/semantic fusion
    // For an OEM/enterprise setup, this would query a local Room DB or Vector DB
    // Currently, it builds a focused sliding window.
    fun fuseMemory(currentQuery: String, workingMemory: List<Interaction>): String {
        if (workingMemory.isEmpty()) return "Sin memoria previa (inicio de sesión)."
        
        val builder = java.lang.StringBuilder()
        val window = workingMemory.takeLast(6) // Only pass the last 3 turns to avoid context bloat
        
        for (m in window) {
            val prefix = if (m.role == "user") "Usuario" else "IVANNA"
            builder.append("$prefix: ${m.text}\n")
        }
        
        return builder.toString()
    }
}
