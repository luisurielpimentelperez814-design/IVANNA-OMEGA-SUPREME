package com.ivanna.omega.assistant.core

import android.content.Context
import android.content.SharedPreferences
import org.json.JSONArray
import org.json.JSONObject

/**
 * ConversationMemory — memoria conversacional persistente de IVANNA.
 *
 * MEJORAS vs versión anterior:
 *  - Persiste en SharedPreferences: sobrevive reinicios de la app.
 *  - Short-term: últimos 20 turnos en RAM (conversación activa).
 *  - Long-term: últimos 100 turnos en disco (historial entre sesiones).
 *  - buildRichContext(): fusiona ambos niveles para el prompt de Gemini.
 *  - init() obligatorio antes de usar (llama desde IvannaGeminiAgent.init).
 */
object ConversationMemory {

    private const val PREFS_NAME   = "ivanna_conversation_memory"
    private const val KEY_HISTORY  = "long_term_history"
    private const val MAX_SHORT    = 20   // turnos en RAM
    private const val MAX_LONG     = 100  // turnos en disco

    private val shortTerm = ArrayDeque<String>()
    private var prefs: SharedPreferences? = null
    private var initialized = false

    // ── Init ─────────────────────────────────────────────────────────────────

    fun init(context: Context) {
        if (initialized) return
        prefs = context.applicationContext
            .getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        // Cargar historial largo en short-term para esta sesión
        val stored = loadLongTerm()
        val start  = maxOf(0, stored.size - MAX_SHORT)
        for (i in start until stored.size) shortTerm.addLast(stored[i])
        initialized = true
    }

    // ── Escritura ─────────────────────────────────────────────────────────────

    fun addInteraction(user: String, ivanna: String) {
        val entry = "User: $user\nIVANNA: $ivanna"
        // Short-term (RAM)
        shortTerm.addLast(entry)
        if (shortTerm.size > MAX_SHORT) shortTerm.removeFirst()
        // Long-term (disco)
        persistEntry(entry)
    }

    // ── Lectura ───────────────────────────────────────────────────────────────

    /** Contexto breve para el prompt (últimos MAX_SHORT turnos en RAM). */
    fun getContext(): String = shortTerm.joinToString("\n---\n")

    /**
     * Contexto enriquecido para Gemini:
     *  - Sección SHORT: últimos N turnos de esta sesión.
     *  - Sección LONG:  resumen de sesiones anteriores (si existen).
     */
    fun buildRichContext(shortCount: Int = 10): String {
        val recent   = shortTerm.takeLast(shortCount)
        val allLong  = loadLongTerm()
        val pastOnly = allLong.dropLast(minOf(shortCount, allLong.size))
        return buildString {
            if (pastOnly.isNotEmpty()) {
                append("=== SESIONES ANTERIORES (resumen) ===\n")
                // Mostrar máximo últimas 20 entradas de historial largo
                pastOnly.takeLast(20).forEach { append(it).append("\n---\n") }
            }
            if (recent.isNotEmpty()) {
                append("=== CONVERSACIÓN ACTUAL ===\n")
                recent.forEach { append(it).append("\n---\n") }
            }
        }.trim()
    }

    fun clear() {
        shortTerm.clear()
        prefs?.edit()?.remove(KEY_HISTORY)?.apply()
    }

    // ── Disco ─────────────────────────────────────────────────────────────────

    private fun persistEntry(entry: String) {
        val p = prefs ?: return
        runCatching {
            val arr = JSONArray(p.getString(KEY_HISTORY, "[]"))
            arr.put(entry)
            while (arr.length() > MAX_LONG) arr.remove(0)
            p.edit().putString(KEY_HISTORY, arr.toString()).apply()
        }
    }

    private fun loadLongTerm(): List<String> {
        val p = prefs ?: return emptyList()
        return runCatching {
            val arr = JSONArray(p.getString(KEY_HISTORY, "[]"))
            List(arr.length()) { arr.getString(it) }
        }.getOrDefault(emptyList())
    }
}
