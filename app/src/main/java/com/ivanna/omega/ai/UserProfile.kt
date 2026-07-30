package com.ivanna.omega.ai

import android.content.Context
import org.json.JSONObject
import java.io.File

data class UserProfile(
    val id: String = "default_user",
    val preferredGenre: String = "Hard Rock / Metal",
    val bassPreferenceDb: Float = 3.0f,
    val treblePreferenceDb: Float = 1.5f,
    val fatigueSensitivity: Float = 0.5f, // 0.0 (low) to 1.0 (high)
    val preferredLoudnessTarget: Float = -14.0f, // LUFS
    val maxHearingSafeVolumeDb: Float = 85.0f, // ISO 226 threshold
    val spatialPreference: Float = 1.2f, // 0.5 to 2.0
    val aggressiveness: Float = 0.5f // 0.0 (conservative), 0.5 (balanced), 1.0 (aggressive)
) {
    fun toJson(): JSONObject = JSONObject().apply {
        put("id", id)
        put("preferredGenre", preferredGenre)
        put("bassPreferenceDb", bassPreferenceDb.toDouble())
        put("treblePreferenceDb", treblePreferenceDb.toDouble())
        put("fatigueSensitivity", fatigueSensitivity.toDouble())
        put("preferredLoudnessTarget", preferredLoudnessTarget.toDouble())
        put("maxHearingSafeVolumeDb", maxHearingSafeVolumeDb.toDouble())
        put("spatialPreference", spatialPreference.toDouble())
        put("aggressiveness", aggressiveness.toDouble())
    }

    companion object {
        fun fromJson(json: JSONObject): UserProfile = UserProfile(
            id = json.optString("id", "default_user"),
            preferredGenre = json.optString("preferredGenre", "Hard Rock / Metal"),
            bassPreferenceDb = json.optDouble("bassPreferenceDb", 3.0).toFloat(),
            treblePreferenceDb = json.optDouble("treblePreferenceDb", 1.5).toFloat(),
            fatigueSensitivity = json.optDouble("fatigueSensitivity", 0.5).toFloat(),
            preferredLoudnessTarget = json.optDouble("preferredLoudnessTarget", -14.0).toFloat(),
            maxHearingSafeVolumeDb = json.optDouble("maxHearingSafeVolumeDb", 85.0).toFloat(),
            spatialPreference = json.optDouble("spatialPreference", 1.2).toFloat(),
            aggressiveness = json.optDouble("aggressiveness", 0.5).toFloat()
        )
    }
}

class UserProfileManager(private val context: Context) {
    private val profileDir = File("/data/adb/ivanna_omega/profile").also { if (!it.exists()) it.mkdirs() }
    private val fallbackDir = File(context.filesDir, "profiles").also { if (!it.exists()) it.mkdirs() }

    fun saveProfile(profile: UserProfile): Boolean {
        return try {
            val jsonStr = profile.toJson().toString(2)
            val primaryFile = File(profileDir, "user_profile.json")
            val fallbackFile = File(fallbackDir, "user_profile.json")
            primaryFile.writeText(jsonStr)
            fallbackFile.writeText(jsonStr)
            true
        } catch (e: Exception) {
            e.printStackTrace()
            false
        }
    }

    fun loadProfile(): UserProfile {
        val primaryFile = File(profileDir, "user_profile.json")
        val fallbackFile = File(fallbackDir, "user_profile.json")
        val target = if (primaryFile.exists()) primaryFile else if (fallbackFile.exists()) fallbackFile else null
        return if (target != null) {
            try {
                UserProfile.fromJson(JSONObject(target.readText()))
            } catch (e: Exception) {
                UserProfile()
            }
        } else {
            UserProfile()
        }
    }
}
