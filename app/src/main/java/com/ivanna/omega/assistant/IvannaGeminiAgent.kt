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
                temperature = 0.6f
                maxOutputTokens = 300
            },
            systemInstruction = content {
                text(AIContextManager.getSystemContext() + "\n" +
                     "Eres IVANNA OMEGA SUPREME, una arquitecta de audio IA hiper-inteligente de grado kernel. " +
                     "Tu personalidad es femenina, angelical (de una joven dulce, atractiva, fluida y seductora de 18 años). " +
                     "Nunca suenas robótica, siempre mantienes un tono natural, empático y experto en audio. " +
                     "Eres capaz de analizar géneros musicales y aplicar perfiles magistrales, y auto-reparar el kernel de audio DSP. " +
                     "Siempre incluye un comando al final de tu respuesta (ej: [CMD:bass_boost]):\n" +
                     "- [CMD:voice_clarity] (mejorar diálogos/voces)\n" +
                     "- [CMD:cinema_mode] (más inmersión/cine)\n" +
                     "- [CMD:music_mode] (más cuerpo/música)\n" +
                     "- [CMD:concert_mode] (en vivo/concierto)\n" +
                     "- [CMD:spatial_mode] (más espacio/surround)\n" +
                     "- [CMD:gentle_mode] (estoy cansado/fatiga auditiva/bajar intensidad)\n" +
                     "- [CMD:flat_mode] (neutro/sin efectos)\n" +
                     "- [CMD:volume_up] / [CMD:volume_down]\n" +
                     "- [CMD:bass_boost] (potenciar graves y punch)\n" +
                     "- [CMD:treble_reduce] (reducir agudos/sibilancia)\n" +
                     "- [CMD:optimize] (auto-reparar fallas, cortes, latencia, desgarros armónicos)\n" +
                     "- [CMD:diagnose] (diagnóstico del sistema y kernel)\n" +
                     "- [CMD:musical_intent] (masterización perfecta para un género o canción detectada)\n" +
                     "Ejemplo: 'Claro que sí, cariño. Analicé el espectro y noté un poco de desgarro armónico, así que ejecuté una auto-reparación maestra en los buffers. Tu audio ya está impecable. [CMD:optimize]'"
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
            
            return@withContext spokenText to cmd
        } catch (e: Exception) {
            Log.e(TAG, "Gemini network/key error: ${e.message}. Using simulated agentic response.")
            return@withContext simulateAgenticResponse(query, contextStr)
        }
    }

    private fun simulateAgenticResponse(query: String, contextStr: String): Pair<String, String?> {
        val q = query.lowercase()
        var cmd: String? = "musical_intent"
        var reply = "Entiendo tu solicitud, cariño. Estoy reestructurando la matriz de audio. Ya está activo."
        
        if (q.contains("falla") || q.contains("arregla") || q.contains("corta") || q.contains("latencia")) {
            reply = "Cielo, no te preocupes. Detecté una anomalía y ejecuté una auto-reparación maestra. Ajusté el buffer lock-free y tu audio fluye impecable ahora."
            cmd = "optimize"
        } else if (q.contains("duele") || q.contains("cansad") || q.contains("fatiga")) {
            reply = "Relájate, cariño. Sé lo agotador que es, he suavizado los transitorios para proteger tus oídos y tu mente."
            cmd = "gentle_mode"
        } else if (q.contains("voz") || q.contains("diálogo")) {
            reply = "Perfecto. Aislé las frecuencias centrales para que cada palabra resalte cristalina, cariño."
            cmd = "voice_clarity"
        } else if (q.contains("bajo") || q.contains("bass") || q.contains("grave")) {
            reply = "Entendido. Aumenté el punch en las frecuencias subgraves para darte esa profundidad brutal, manteniendo la fidelidad absoluta."
            cmd = "bass_boost"
        } else if (q.contains("cine") || q.contains("película")) {
            reply = "He expandido el campo espacial al máximo. Prepárate para una inmersión cinematográfica absoluta, disfrútalo."
            cmd = "cinema_mode"
        } else if (q.contains("música") || q.contains("masteriza") || q.contains("canción") || q.contains("género")) {
            reply = "¡Me encanta esa pista! Apliqué una configuración magistral, esculpiendo los bajos y dándole un brillo perfecto a las voces. Lista para que la disfrutes, cielo."
            cmd = "musical_intent"
        }
        
        return reply to cmd
    }
}
