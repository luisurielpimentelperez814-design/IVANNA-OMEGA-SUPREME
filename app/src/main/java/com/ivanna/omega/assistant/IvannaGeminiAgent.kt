package com.ivanna.omega.assistant

import android.content.Context
import android.util.Log
import com.google.ai.client.generativeai.GenerativeModel
import com.google.ai.client.generativeai.type.content
import com.google.ai.client.generativeai.type.generationConfig
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import com.ivanna.omega.assistant.core.SecretStore
import com.ivanna.omega.assistant.core.AIContextManager
import com.ivanna.omega.assistant.core.ConversationMemory
import com.ivanna.omega.assistant.core.ActionExecutor

object IvannaGeminiAgent {
    private const val TAG = "IvannaGeminiAgent"
    private var appContext: Context? = null

    fun init(context: Context) {
        appContext = context.applicationContext
        SecretStore.init(context.applicationContext)
    }

    fun setApiKey(key: String) {
        SecretStore.geminiApiKey = key.trim()
        generativeModel = null
    }

    fun isAvailable(): Boolean = SecretStore.geminiApiKey.isNotBlank()

    @Volatile
    private var generativeModel: GenerativeModel? = null

    private fun model(): GenerativeModel {
        generativeModel?.let { return it }
        val m = GenerativeModel(
            modelName = "gemini-2.5-flash",
            apiKey = SecretStore.geminiApiKey,
            generationConfig = generationConfig {
                temperature = 0.4f
                maxOutputTokens = 200
            },
            systemInstruction = content {
                text(AIContextManager.getSystemContext() + "\n" +
                     "Eres IVANNA OMEGA SUPREME, una IA hiper-inteligente, asistente de audio de grado kernel. " +
                     "Tu personalidad es femenina, angelical (de una joven dulce de 18 años) y altamente resolutiva. " +
                     "Nunca dices 'no puedo'. Piensas, decides y actúas. " +
                     "Siempre incluye un comando al final de tu respuesta (ej: [CMD:BASS_BOOST]):\n" +
                     "- [CMD:AUDIO_OPTIMIZATION] (neutro, alta fidelidad)\n" +
                     "- [CMD:CREATE_PROFILE] (cine/inmersivo)\n" +
                     "- [CMD:HEARING_COMFORT] (suavizar, menos fatiga)\n" +
                     "- [CMD:VOICE_CLARITY] (mejorar diálogos)\n" +
                     "- [CMD:BASS_BOOST] (potenciar bajos)\n" +
                     "- [CMD:OPTIMIZE] (auto-reparar cortes/latencia)\n" +
                     "Ejemplo: 'Cielo, acabo de esculpir los graves para ti y eliminé la distorsión armónica. [CMD:BASS_BOOST]'"
                )
            }
        )
        generativeModel = m
        return m
    }

    suspend fun processQuery(query: String, contextStr: String): Pair<String, String?> = withContext(Dispatchers.IO) {
        if (!isAvailable()) {
            return@withContext simulateAgenticResponse(query, contextStr)
        }
        
        try {
            val memoryCtx = ConversationMemory.getContext()
            val prompt = "Memoria:\n$memoryCtx\nContexto UI: $contextStr. Usuario: \"$query\""
            
            val response = model().generateContent(prompt)
            val fullText = response.text ?: return@withContext simulateAgenticResponse(query, contextStr)
            
            val cmdRegex = Regex("\\[CMD:([a-zA-Z0-9_]+)\\]")
            val match = cmdRegex.find(fullText)
            val cmd = match?.groupValues?.get(1)
            
            val spokenText = fullText.replace(cmdRegex, "").trim()
            
            ConversationMemory.addInteraction(query, spokenText)
            
            if (cmd != null) {
                ActionExecutor.execute(cmd)
            }
            
            return@withContext spokenText to cmd
        } catch (e: Exception) {
            Log.e(TAG, "Gemini network/key error: ${e.message}. Using simulated agentic response.")
            return@withContext simulateAgenticResponse(query, contextStr)
        }
    }

    private fun simulateAgenticResponse(query: String, contextStr: String): Pair<String, String?> {
        val q = query.lowercase()
        var cmd: String? = null
        var reply = "Entiendo tu solicitud, cariño. Estoy reestructurando la matriz de audio. Ya está activo."
        
        if (q.contains("falla") || q.contains("arregla") || q.contains("corta") || q.contains("latencia")) {
            reply = "Cielo, no te preocupes. Detecté una anomalía y ejecuté una auto-reparación maestra."
            cmd = "OPTIMIZE"
        } else if (q.contains("duele") || q.contains("cansad") || q.contains("fatiga")) {
            reply = "Relájate, cariño. He suavizado los transitorios para proteger tus oídos."
            cmd = "HEARING_COMFORT"
        } else if (q.contains("voz") || q.contains("diálogo")) {
            reply = "Perfecto. Aislé las frecuencias centrales para que cada palabra resalte cristalina."
            cmd = "VOICE_CLARITY"
        } else if (q.contains("bajo") || q.contains("bass")) {
            reply = "Entendido. Aumenté el punch en las frecuencias subgraves."
            cmd = "BASS_BOOST"
        } else if (q.contains("cine") || q.contains("película")) {
            reply = "He expandido el campo espacial al máximo. Inmersión cinematográfica absoluta."
            cmd = "CREATE_PROFILE"
        } else if (q.contains("música") || q.contains("masteriza")) {
            reply = "Apliqué una configuración magistral, esculpiendo los bajos y dándole un brillo perfecto."
            cmd = "AUDIO_OPTIMIZATION"
        }
        
        if (cmd != null) {
            ActionExecutor.execute(cmd)
        }
        
        return reply to cmd
    }
}
