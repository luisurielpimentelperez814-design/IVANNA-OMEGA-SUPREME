package com.ivanna.omega.core

import android.content.Context
import android.content.SharedPreferences
import com.ivanna.omega.audio.IvannaEffectProfile
import org.json.JSONObject
import java.text.SimpleDateFormat
import java.util.*

class UserProfileManager(private val context: Context) {
    private val prefs: SharedPreferences = context.getSharedPreferences("ivanna_user_profiles", Context.MODE_PRIVATE)
    private val currentProfileKey = "current_profile"
    private val profileHistoryKey = "profile_history"

    data class UserProfile(
        val name: String,
        val presetName: String,
        val timestamp: Long,
        val sourceApp: String? = null
    )

    fun saveCurrentProfile(presetName: String, sourceApp: String? = null) {
        val history = getHistory().toMutableList()
        history.add(UserProfile(
            name = "Perfil ${history.size + 1}",
            presetName = presetName,
            timestamp = System.currentTimeMillis(),
            sourceApp = sourceApp
        ))
        // Guardar solo últimos 50
        val trimmed = history.takeLast(50)
        saveHistory(trimmed)
        // Establecer como actual
        prefs.edit().putString(currentProfileKey, presetName).apply()
    }

    fun getCurrentPreset(): String = prefs.getString(currentProfileKey, "Warm") ?: "Warm"

    fun getHistory(): List<UserProfile> {
        val json = prefs.getString(profileHistoryKey, "[]") ?: "[]"
        val arr = org.json.JSONArray(json)
        val list = mutableListOf<UserProfile>()
        for (i in 0 until arr.length()) {
            val obj = arr.getJSONObject(i)
            list.add(UserProfile(
                name = obj.getString("name"),
                presetName = obj.getString("presetName"),
                timestamp = obj.getLong("timestamp"),
                sourceApp = obj.optString("sourceApp", null)
            ))
        }
        return list
    }

    private fun saveHistory(history: List<UserProfile>) {
        val arr = org.json.JSONArray()
        for (p in history) {
            val obj = JSONObject().apply {
                put("name", p.name)
                put("presetName", p.presetName)
                put("timestamp", p.timestamp)
                p.sourceApp?.let { put("sourceApp", it) }
            }
            arr.put(obj)
        }
        prefs.edit().putString(profileHistoryKey, arr.toString()).apply()
    }

    fun getMostUsedPreset(): String {
        val history = getHistory()
        if (history.isEmpty()) return "Warm"
        val freq = history.groupingBy { it.presetName }.eachCount()
        return freq.maxByOrNull { it.value }?.key ?: "Warm"
    }

    fun getPresetForTimeOfDay(): String {
        val hour = Calendar.getInstance().get(Calendar.HOUR_OF_DAY)
        return when (hour) {
            in 6..11 -> "Warm"      // Mañana: cálido
            in 12..17 -> "Punch"    // Tarde: enérgico
            in 18..22 -> "Spatial"  // Noche: espacial
            else -> "Flat"          // Madrugada: plano
        }
    }

    fun applySmartProfile(app: IVANNAApplication) {
        val preset = when {
            // Si hay una app en primer plano, usar su perfil guardado
            getMostUsedPreset() != "Warm" -> getMostUsedPreset()
            else -> getPresetForTimeOfDay()
        }
        val profile = IvannaEffectProfile.byName[preset] ?: IvannaEffectProfile.WARM
        app.globalEffectManager.applyProfile(profile)
    }
}
