package com.ivanna.omega.assistant

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject

/**
 * IvannaListenerProfile — memoria personal del oyente. 100% local.
 *
 * Guarda en SharedPreferences (no en la nube, nunca):
 *  - Preferencias acústicas aprendidas del uso real.
 *  - Perfiles favoritos (los que el usuario aplica más veces).
 *  - Historial de ajustes (qué se pidió, qué se aplicó, cuándo).
 *  - Sensibilidad auditiva (si el usuario reporta fatiga frecuente, etc.)
 *  - Comandos frecuentes para pre-sugerir en la UI.
 */
class IvannaListenerProfile(context: Context) {

    private val prefs = context.getSharedPreferences("ivanna_listener_profile", Context.MODE_PRIVATE)

    // ── Preferencias acústicas ────────────────────────────────────────────────

    var preferredMode: String
        get() = prefs.getString("preferred_mode", "flat_mode") ?: "flat_mode"
        set(v) = prefs.edit().putString("preferred_mode", v).apply()

    var preferredSpatialWidth: Float
        get() = prefs.getFloat("spatial_width", 1.0f)
        set(v) = prefs.edit().putFloat("spatial_width", v).apply()

    var preferredBassGain: Float
        get() = prefs.getFloat("bass_gain", 0f)
        set(v) = prefs.edit().putFloat("bass_gain", v).apply()

    var preferredVoiceBoost: Boolean
        get() = prefs.getBoolean("voice_boost", false)
        set(v) = prefs.edit().putBoolean("voice_boost", v).apply()

    // ── Sensibilidad / bienestar ──────────────────────────────────────────────

    /** Número de veces que el usuario reportó fatiga auditiva. */
    var fatigueReports: Int
        get() = prefs.getInt("fatigue_reports", 0)
        set(v) = prefs.edit().putInt("fatigue_reports", v).apply()

    /** Si fatigueReports >= 3, IVANNA sugiere modo gentle proactivamente. */
    val shouldSuggestGentle: Boolean get() = fatigueReports >= 3

    // ── Historial de ajustes (últimos 50) ────────────────────────────────────

    fun recordAdjustment(command: String, scene: String) {
        val arr = runCatching { JSONArray(prefs.getString("adj_history", "[]")) }
            .getOrDefault(JSONArray())
        val entry = JSONObject().apply {
            put("cmd", command)
            put("scene", scene)
            put("ts", System.currentTimeMillis())
        }
        arr.put(entry)
        // Mantener solo los últimos 50
        val trimmed = if (arr.length() > 50) {
            JSONArray().also { t -> (arr.length() - 50 until arr.length()).forEach { i -> t.put(arr.get(i)) } }
        } else arr
        prefs.edit().putString("adj_history", trimmed.toString()).apply()
        // Actualizar modo preferido (el más frecuente en los últimos 20)
        updatePreferredMode(trimmed)
        // Contar fatiga
        if (command == "gentle_mode") fatigueReports = fatigueReports + 1
    }

    private fun updatePreferredMode(arr: JSONArray) {
        val counts = mutableMapOf<String, Int>()
        val start = maxOf(0, arr.length() - 20)
        for (i in start until arr.length()) {
            val cmd = arr.getJSONObject(i).optString("cmd")
            if (cmd.isNotBlank()) counts[cmd] = (counts[cmd] ?: 0) + 1
        }
        counts.maxByOrNull { it.value }?.key?.let { preferredMode = it }
    }

    /** Comandos más frecuentes (top 3) para sugerir en la UI. */
    fun topCommands(): List<String> {
        val arr = runCatching { JSONArray(prefs.getString("adj_history", "[]")) }
            .getOrDefault(JSONArray())
        val counts = mutableMapOf<String, Int>()
        for (i in 0 until arr.length()) {
            val cmd = arr.getJSONObject(i).optString("cmd")
            if (cmd.isNotBlank()) counts[cmd] = (counts[cmd] ?: 0) + 1
        }
        return counts.entries.sortedByDescending { it.value }.take(3).map { it.key }
    }

    /** Etiqueta legible de un comando canónico. */
    fun labelOf(command: String): String = when (command) {
        "voice_clarity"  -> "Claridad vocal"
        "cinema_mode"    -> "Modo cine"
        "music_mode"     -> "Modo música"
        "concert_mode"   -> "Concierto"
        "spatial_mode"   -> "Espacial"
        "gentle_mode"    -> "Modo suave"
        "flat_mode"      -> "Neutro"
        "volume_up"      -> "Subir volumen"
        "volume_down"    -> "Bajar volumen"
        "bass_boost"     -> "Graves"
        "treble_reduce"  -> "Menos agudos"
        "optimize"       -> "Optimizar"
        else             -> command
    }

    fun clearAll() = prefs.edit().clear().apply()
}
