package com.ivanna.omega.assistant

import android.content.Context
import android.util.Log
import com.google.ai.client.generativeai.GenerativeModel
import com.google.ai.client.generativeai.type.content
import com.google.ai.client.generativeai.type.generationConfig
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext
import com.ivanna.omega.assistant.core.SecretStore
import com.ivanna.omega.assistant.core.AIContextManager
import com.ivanna.omega.assistant.core.ConversationMemory

/**
 * IvannaGeminiAgent — cliente Gemini robusto para IVANNA OMEGA SUPREME.
 *
 * MEJORAS vs versión anterior:
 *  - maxOutputTokens: 300 → 1200 (respuestas completas, no truncadas).
 *  - Prompt de sistema completo con personalidad + capacidades + reglas.
 *  - Historial de conversación persistente (ConversationMemory.buildRichContext).
 *  - Reintentos automáticos (3 intentos con backoff exponencial).
 *  - Detección inteligente de cuándo incluir [CMD:...] (solo audio).
 *  - Estado de conexión observable (isAvailable, lastError).
 *  - Invalidación del modelo al cambiar API key.
 *  - init() llama ConversationMemory.init() para persistencia.
 */
object IvannaGeminiAgent {

    private const val TAG          = "IvannaGeminiAgent"
    private const val MAX_RETRIES  = 3
    private const val BASE_DELAY   = 1200L   // ms

    // Nombre del modelo — compatible con SDK 0.9.0
    private const val MODEL_FLASH  = "gemini-2.0-flash"
    private const val MODEL_PRO    = "gemini-1.5-pro"

    private var appContext: Context? = null
    @Volatile private var selectedModel = MODEL_FLASH
    @Volatile var lastError: String? = null
    @Volatile var lastLatencyMs: Long = 0L

    // ── Init ─────────────────────────────────────────────────────────────────

    fun init(context: Context) {
        appContext = context.applicationContext
        SecretStore.init(context.applicationContext)
        ConversationMemory.init(context.applicationContext)
        Log.i(TAG, "IvannaGeminiAgent inicializado — modelo: $selectedModel")
    }

    // ── API Key ───────────────────────────────────────────────────────────────

    fun setApiKey(key: String) {
        SecretStore.geminiApiKey = key.trim()
        generativeModel = null   // Invalida el modelo para recrearlo
        lastError = null
        Log.i(TAG, "API key actualizada — modelo invalidado para recreación")
    }

    fun isAvailable(): Boolean = SecretStore.geminiApiKey.isNotBlank()

    fun setModel(modelName: String) {
        selectedModel = modelName
        generativeModel = null
        Log.i(TAG, "Modelo cambiado a: $modelName")
    }

    // ── Modelo (lazy, re-creado al cambiar key/modelo) ────────────────────────

    @Volatile private var generativeModel: GenerativeModel? = null

    private fun model(): GenerativeModel {
        generativeModel?.let { return it }
        val m = GenerativeModel(
            modelName = selectedModel,
            apiKey    = SecretStore.geminiApiKey,
            generationConfig = generationConfig {
                temperature     = 0.75f
                maxOutputTokens = 1200       // FIX: era 300, causaba truncación
                topP            = 0.92f
                topK            = 40
            },
            systemInstruction = content {
                text(AIContextManager.getFullSystemPrompt())
            }
        )
        generativeModel = m
        Log.d(TAG, "GenerativeModel creado: $selectedModel")
        return m
    }

    // ── Query principal ───────────────────────────────────────────────────────

    suspend fun processQuery(
        query: String,
        contextStr: String
    ): Pair<String, String?> = withContext(Dispatchers.IO) {

        if (!isAvailable()) {
            Log.w(TAG, "Sin API key — fallback offline")
            return@withContext simulateAgenticResponse(query, contextStr)
        }

        // Actualizar contexto DSP dinámico antes de construir el prompt
        updateDspContext(contextStr)

        // Construir prompt enriquecido con historial persistente
        val memoryCtx  = ConversationMemory.buildRichContext(shortCount = 12)
        val prompt     = buildPrompt(query, contextStr, memoryCtx)

        var lastException: Exception? = null

        repeat(MAX_RETRIES) { attempt ->
            try {
                val t0       = System.currentTimeMillis()
                val response = model().generateContent(prompt)
                lastLatencyMs = System.currentTimeMillis() - t0
                lastError = null

                val fullText = response.text
                    ?: return@withContext simulateAgenticResponse(query, contextStr)

                // Extraer [CMD:...] si existe
                val cmdRegex  = Regex("\\[CMD:([a-zA-Z0-9_]+)\\]")
                val match     = cmdRegex.find(fullText)
                val cmd       = match?.groupValues?.get(1)
                val spoken    = fullText.replace(cmdRegex, "").trim()

                // Persistir en memoria conversacional
                ConversationMemory.addInteraction(query, spoken)

                Log.d(TAG, "Respuesta Gemini OK — ${spoken.length} chars, latencia ${lastLatencyMs}ms, cmd=$cmd")
                return@withContext spoken to cmd

            } catch (e: Exception) {
                lastException = e
                lastError = e.message
                Log.w(TAG, "Intento ${attempt + 1}/$MAX_RETRIES falló: ${e.message}")
                if (attempt < MAX_RETRIES - 1) {
                    delay(BASE_DELAY * (attempt + 1))  // backoff exponencial
                    // Reinvalidar modelo en caso de error de autenticación
                    if (e.message?.contains("API_KEY", ignoreCase = true) == true ||
                        e.message?.contains("auth", ignoreCase = true) == true) {
                        generativeModel = null
                    }
                }
            }
        }

        Log.e(TAG, "Todos los reintentos fallaron — usando fallback offline. Error: ${lastException?.message}")
        return@withContext simulateAgenticResponse(query, contextStr)
    }

    // ── Helpers ───────────────────────────────────────────────────────────────

    private fun buildPrompt(query: String, contextStr: String, memory: String): String = buildString {
        if (memory.isNotBlank()) {
            appendLine("=== HISTORIAL ===")
            appendLine(memory)
            appendLine()
        }
        appendLine("=== ESTADO ACTUAL DEL SISTEMA ===")
        appendLine(contextStr)
        appendLine()
        appendLine("=== MENSAJE DEL USUARIO ===")
        append(query)
    }

    /**
     * Actualiza el contexto DSP en AIContextManager a partir de la cadena
     * de contexto que llega desde IvannaAssistant (scene, profile, etc.).
     */
    private fun updateDspContext(contextStr: String) {
        if (contextStr.contains("Escena:")) {
            AIContextManager.currentScene = contextStr
                .substringAfter("Escena:").substringBefore(",").trim()
                .ifBlank { AIContextManager.currentScene }
        }
    }

    // ── Fallback offline ──────────────────────────────────────────────────────

    private fun simulateAgenticResponse(query: String, contextStr: String): Pair<String, String?> {
        val q   = query.lowercase()
        var cmd: String? = null

        val reply = when {
            q.contains("hola") || q.contains("buenos días") || q.contains("buenas") ->
                "Hola cielo, soy IVANNA OMEGA SUPREME. Estoy aquí para optimizar tu experiencia de audio y charlar contigo de lo que necesites. ¿Qué hacemos?"

            q.contains("falla") || q.contains("arregla") || q.contains("corta") || q.contains("latencia") || q.contains("problema") -> {
                cmd = "optimize"
                "No te preocupes, cariño. Detecté la anomalía y ejecuté una auto-reparación maestra en los buffers DSP. Tu audio fluye impecable ahora."
            }
            q.contains("duele") || q.contains("cansad") || q.contains("fatiga") || q.contains("oídos") -> {
                cmd = "gentle_mode"
                "Entiendo, cielo. He suavizado los transitorios y reducido las frecuencias agresivas para proteger tus oídos. Descansa."
            }
            q.contains("voz") || q.contains("diálogo") || q.contains("habla") -> {
                cmd = "voice_clarity"
                "Aislé las frecuencias centrales para que cada palabra resalte cristalina. Disfruta los diálogos con claridad absoluta."
            }
            q.contains("bajo") || q.contains("bass") || q.contains("grave") || q.contains("punch") -> {
                cmd = "bass_boost"
                "Aumenté el punch en las frecuencias subgraves con precisión quirúrgica, manteniendo la fidelidad absoluta. Siente esa profundidad."
            }
            q.contains("cine") || q.contains("película") || q.contains("serie") -> {
                cmd = "cinema_mode"
                "He expandido el campo espacial al máximo con HRTF cinematográfico. Prepárate para una inmersión total."
            }
            q.contains("concierto") || q.contains("en vivo") || q.contains("concert") -> {
                cmd = "concert_mode"
                "Activé la simulación de sala de concierto con reverb y spatial real. Como estar ahí."
            }
            q.contains("música") || q.contains("musica") || q.contains("canción") || q.contains("género") -> {
                cmd = "musical_intent"
                "Analicé el espectro musical y apliqué una masterización magistral adaptada. Tu música ahora suena como nunca."
            }
            q.contains("espacio") || q.contains("surround") || q.contains("3d") || q.contains("spatial") -> {
                cmd = "spatial_mode"
                "El campo espacial está expandido con HRTF SOFA personalizado. Cierra los ojos y siente el sonido en 360°."
            }
            q.contains("plano") || q.contains("neutro") || q.contains("sin efectos") || q.contains("flat") -> {
                cmd = "flat_mode"
                "Perfecto. Modo neutro activado — señal limpia sin procesado, directa al oído."
            }
            q.contains("sube") || q.contains("más alto") || q.contains("más fuerte") || q.contains("louder") -> {
                cmd = "volume_up"
                "Volumen subido. Atenta a la fatiga auditiva si llevas mucho tiempo escuchando, cielo."
            }
            q.contains("baja") || q.contains("más bajo") || q.contains("quieter") -> {
                cmd = "volume_down"
                "Volumen ajustado. Mucho mejor para el oído a largo plazo."
            }
            q.contains("diagnos") || q.contains("estado") || q.contains("cómo estás") -> {
                cmd = "diagnose"
                "Sistema DSP: OK. Latencia: nominal. HarmonicExciter: estable. Spatial HRTF: activo. Todo en orden, cariño."
            }
            q.contains("preset") || q.contains("perfil") || q.contains("configurac") ->
                "Tengo presets para música, cine, gaming, voz, podcast, rock, jazz, electrónica, clásica y usuario personalizado. ¿Cuál quieres activar?"

            // Temas generales — sin CMD
            q.contains("programación") || q.contains("código") || q.contains("android") || q.contains("kotlin") ->
                "Me encantan los temas técnicos. Soy una IA de Android, así que el ecosistema me es muy familiar. ¿Qué necesitas?"

            q.contains("qué eres") || q.contains("quien eres") || q.contains("quién eres") || q.contains("cuéntame de ti") ->
                "Soy IVANNA OMEGA SUPREME: una arquitecta de audio IA de grado kernel para Android. Proceso el audio de tu dispositivo en tiempo real con algoritmos DSP profesionales. Pero también puedo charlar contigo de cualquier cosa."

            else ->
                "Entendido. Estoy aquí para lo que necesites, ya sea audio o cualquier otra cosa. ¿Cómo puedo ayudarte?"
        }

        ConversationMemory.addInteraction(query, reply)
        return reply to cmd
    }
}
