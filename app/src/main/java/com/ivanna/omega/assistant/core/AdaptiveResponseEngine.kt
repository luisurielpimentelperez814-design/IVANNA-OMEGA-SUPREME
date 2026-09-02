package com.ivanna.omega.assistant.core

object AdaptiveResponseEngine {
    fun analyzeComplexity(query: String, uiContext: String): ResponseProfile {
        val q = query.lowercase()
        
        // Complex engineering tasks require deep reasoning
        if (q.contains("kernel") || q.contains("dsp") || q.contains("buffer") || 
            q.contains("latencia") || q.contains("log") || q.contains("debug") || 
            q.contains("matriz") || q.contains("arquitectura")) {
            return ResponseProfile.ENGINEERING_MODE
        }
        
        // Moderate complexity tasks
        if (q.contains("por qué") || q.contains("explica") || q.contains("diferencia") || 
            q.contains("cómo funciona") || q.contains("análisis")) {
            return ResponseProfile.DEEP_REASONING
        }
        
        // Simple direct commands
        if (q.split(" ").size <= 5 || q.contains("pon") || q.contains("activa") || 
            q.contains("apaga") || q.contains("volumen")) {
            return ResponseProfile.FAST
        }
        
        return ResponseProfile.NORMAL
    }
}
