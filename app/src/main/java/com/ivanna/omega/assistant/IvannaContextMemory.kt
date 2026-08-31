package com.ivanna.omega.assistant

import android.content.Context
import android.content.SharedPreferences
import android.util.Log
import org.json.JSONArray
import org.json.JSONObject

/**
 * IvannaContextMemory — memoria contextual especializada de IVANNA.
 *
 * NO es una memoria genérica de chat. Solo guarda lo que una inteligencia
 * acústica necesita recordar entre sesiones para servir mejor:
 *
 *   - preferencias auditivas (volumen preferido, ancho espacial, EQ)
 *   - perfiles favoritos (los que el usuario elige o pide por voz)
 *   - historial de ajustes que IVANNA aplicó (para explicabilidad)
 *   - contexto de sesión (escena dominante reciente, hora de uso)
 *   - explicaciones recientes (qué dijo IVANNA y por qué)
 *
 * Límites de privacidad y consumo (por diseño, no como opción):
 *   - Todo vive en SharedPreferences del dispositivo — nada sale del teléfono.
 *   - Tope de historial: MAX_HISTORY entradas (FIFO); nunca crece sin límite.
 *   - Tope de texto: cada nota se trunca a MAX_NOTE_CHARS caracteres.
 *   - clearAll() borra TODO — el usuario tiene control total desde la UI.
 *   - Nada de contenido de audio ni transcripciones largas: solo intenciones
 *     ya clasificadas y parámetros, nunca el audio en sí.
 */
class IvannaContextMemory(context: Context) {

    companion object {
        private const val TAG = "IvannaContextMemory"
        private const val PREFS_NAME = "ivanna_context_memory"
        private const val MAX_HISTORY = 32
        private const val MAX_NOTE_CHARS = 240
    }

    private val prefs: SharedPreferences =
        context.applicationContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

    // ── Preferencias auditivas (persisten entre sesiones) ────────────────────

    var preferredMasterVolume: Float
        get() = prefs.getFloat("pref_master_volume", 0.8f)
        set(v) = prefs.edit().putFloat("pref_master_volume", v.coerceIn(0f, 1f)).apply()

    var preferredSpatialWidth: Float
        get() = prefs.getFloat("pref_spatial_width", 1.0f)
        set(v) = prefs.edit().putFloat("pref_spatial_width", v.coerceIn(0.5f, 2f)).apply()

    var preferredListenPhon: Float
        get() = prefs.getFloat("pref_listen_phon", 60f)
        set(v) = prefs.edit().putFloat("pref_listen_phon", v.coerceIn(20f, 90f)).apply()

    /** Perfil favorito (nombre canónico: "music_mode", "flat_mode", …). */
    var favoriteProfile: String?
        get() = prefs.getString("favorite_profile", null)
        set(v) = prefs.edit().putString("favorite_profile", v).apply()

    // ── Historial de ajustes aplicados por IVANNA (para explicabilidad) ──────

    data class AdjustmentRecord(
        val timestampMs: Long,
        val action: String,
        val reason: String,
        val applied: Boolean
    )

    fun recordAdjustment(action: String, reason: String, applied: Boolean) {
        runCatching {
            val arr = JSONArray(prefs.getString("adjustments", "[]"))
            arr.put(JSONObject().apply {
                put("ts", System.currentTimeMillis())
                put("action", action.take(MAX_NOTE_CHARS))
                put("reason", reason.take(MAX_NOTE_CHARS))
                put("applied", applied)
            })
            // FIFO: conservar solo las últimas MAX_HISTORY entradas.
            while (arr.length() > MAX_HISTORY) arr.remove(0)
            prefs.edit().putString("adjustments", arr.toString()).apply()
        }.onFailure { Log.w(TAG, "recordAdjustment falló: ${it.message}") }
    }

    fun recentAdjustments(limit: Int = 8): List<AdjustmentRecord> = runCatching {
        val arr = JSONArray(prefs.getString("adjustments", "[]"))
        (0 until arr.length()).map { i ->
            val o = arr.getJSONObject(i)
            AdjustmentRecord(
                timestampMs = o.optLong("ts"),
                action = o.optString("action"),
                reason = o.optString("reason"),
                applied = o.optBoolean("applied")
            )
        }.takeLast(limit.coerceIn(1, MAX_HISTORY))
    }.getOrElse { emptyList() }

    // ── Contexto de sesión (escena dominante + notas recientes) ──────────────

    /** Escena dominante de la sesión anterior (para saludo contextual). */
    var lastScene: String?
        get() = prefs.getString("last_scene", null)
        set(v) = prefs.edit().putString("last_scene", v).apply()

    /** Última explicación que IVANNA dio (para "¿qué hiciste?" entre sesiones). */
    var lastExplanation: String?
        get() = prefs.getString("last_explanation", null)
        set(v) = prefs.edit().putString("last_explanation", v?.take(MAX_NOTE_CHARS)).apply()

    // ── Control del usuario ───────────────────────────────────────────────────

    /** Borra TODA la memoria (preferencias + historial + contexto). */
    fun clearAll() {
        prefs.edit().clear().apply()
        Log.i(TAG, "Memoria contextual borrada por el usuario")
    }
}
