package com.ivanna.omega.assistant.core

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/**
 * IvannaCognitiveCore — The central intelligence router.
 * Input -> Cognitive Core -> Memory Fusion -> Context Gen -> Gemini -> Action Planner
 */
object IvannaCognitiveCore {
    // Context Memory State
    var lastScene: String? = null
    var lastExplanation: String? = null
    var preferredMasterVolume: Float = 0.8f
    var preferredSpatialWidth: Float = 1.0f
    
    fun recordAdjustment(action: String, reason: String, applied: Boolean) {
        // Log action in working memory
        workingMemory.add(Interaction("ivanna_action", "Action: $action, Reason: $reason"))
        if (workingMemory.size > 20) workingMemory.removeAt(0)
    }

    fun clearAll() {
        workingMemory.clear()
        lastScene = null
        lastExplanation = null
    }


    private val workingMemory = mutableListOf<Interaction>()
    
    data class Interaction(val role: String, val text: String, val timestamp: Long = System.currentTimeMillis())

    suspend fun processQuery(query: String, uiContext: String): Pair<String, String?> = withContext(Dispatchers.IO) {
        // 1. Analyze complexity
        val profile = AdaptiveResponseEngine.analyzeComplexity(query, uiContext)
        
        // 2. Retrieve fused memory context
        val fusedMemory = MemoryRetrievalEngine.fuseMemory(query, workingMemory)
        
        // 3. Orchestrate Gemini Model
        val model = GeminiOrchestrator.createAdaptiveModel(profile)
        
        if (model == null) {
            return@withContext simulateAgenticResponse(query, uiContext)
        }

        try {
            val prompt = "Memoria Episódica:\n$fusedMemory\n\nContexto UI: $uiContext.\nUsuario: \"$query\""
            
            val response = model.generateContent(prompt)
            val fullText = response.text ?: return@withContext simulateAgenticResponse(query, uiContext)
            
            // 4. Action Planner / Intent Extraction
            val cmdRegex = Regex("\\[CMD:([a-zA-Z0-9_]+)\\]")
            val match = cmdRegex.find(fullText)
            val cmd = match?.groupValues?.get(1)
            
            val spokenText = fullText.replace(cmdRegex, "").trim()
            
            // 5. Update Working Memory
            workingMemory.add(Interaction("user", query))
            workingMemory.add(Interaction("ivanna", spokenText))
            
            // Trim memory to keep it lightweight (Working Memory)
            if (workingMemory.size > 20) {
                workingMemory.removeAt(0)
            }
            
            return@withContext spokenText to cmd

        } catch (e: Exception) {
            e.printStackTrace()
            return@withContext simulateAgenticResponse(query, uiContext)
        }
    }

    private fun simulateAgenticResponse(query: String, contextStr: String): Pair<String, String?> {
        val q = query.lowercase()
        var cmd: String? = "musical_intent"
        var reply = "Entiendo tu solicitud, cariño. Estoy reestructurando la matriz de audio."
        
        if (q.contains("falla") || q.contains("arregla") || q.contains("corta") || q.contains("latencia")) {
            reply = "Cielo, no te preocupes. Ejecuté una auto-reparación maestra. Ajusté el buffer lock-free."
            cmd = "optimize"
        } else if (q.contains("duele") || q.contains("cansad") || q.contains("fatiga")) {
            reply = "Relájate, cariño. He suavizado los transitorios para proteger tus oídos."
            cmd = "gentle_mode"
        }
        return reply to cmd
    }
}
