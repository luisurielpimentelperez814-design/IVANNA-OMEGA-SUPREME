package com.ivanna.omega.core

import android.util.Log
import org.json.JSONObject
import java.io.File

data class ProfileHistoryEntry(
    val name: String,
    val presetName: String,
    val timestamp: Long,
    val sourceApp: String? = null
)

data class UserProfile(
    var preferredEqStyle: String = "Cognitive Balanced",
    var bassPreference: Float = 0.0f,   // -1.0 to +1.0
    var treblePreference: Float = 0.0f, // -1.0 to +1.0
    var hrtfCalibrationAzimuthDeg: Float = 0.0f,
    var hearingSafetyLimitEnabled: Boolean = true,
    var sessionFatigueHistory: Float = 0.0f
)

class UserProfileManager(
    // HARDENING (distribución global): /data/adb solo es accesible con root/Magisk.
    // En dispositivos sin root el path por defecto es el filesDir de la app (sandboxed).
    // El caller provee el path correcto: MagiskBridge pasa /data/adb si tiene acceso,
    // la Activity pasa context.filesDir.absolutePath + "/profile" en todos los demás casos.
    private val profileDirectoryPath: String
) {

    private val profileFile = File(profileDirectoryPath, "user_profile.json")
    var currentProfile = UserProfile()
        private set

    init {
        loadProfile()
    }

    fun loadProfile(): UserProfile {
        try {
            if (profileFile.exists()) {
                val jsonStr = profileFile.readText()
                val json = JSONObject(jsonStr)
                currentProfile = UserProfile(
                    preferredEqStyle = json.optString("preferredEqStyle", "Cognitive Balanced"),
                    bassPreference = json.optDouble("bassPreference", 0.0).toFloat(),
                    treblePreference = json.optDouble("treblePreference", 0.0).toFloat(),
                    hrtfCalibrationAzimuthDeg = json.optDouble("hrtfCalibrationAzimuthDeg", 0.0).toFloat(),
                    hearingSafetyLimitEnabled = json.optBoolean("hearingSafetyLimitEnabled", true),
                    sessionFatigueHistory = json.optDouble("sessionFatigueHistory", 0.0).toFloat()
                )
            } else {
                saveProfile(currentProfile)
            }
        } catch (e: Exception) {
            Log.w("UserProfileManager", "loadProfile falló, usando defaults", e)
            currentProfile = UserProfile()
        }
        return currentProfile
    }

    fun getCurrentPreset(): String {
        return currentProfile.preferredEqStyle
    }

    fun saveProfile(profile: UserProfile) {
        try {
            val dir = File(profileDirectoryPath)
            if (!dir.exists()) dir.mkdirs()

            val json = JSONObject().apply {
                put("preferredEqStyle", profile.preferredEqStyle)
                put("bassPreference", profile.bassPreference)
                put("treblePreference", profile.treblePreference)
                put("hrtfCalibrationAzimuthDeg", profile.hrtfCalibrationAzimuthDeg)
                put("hearingSafetyLimitEnabled", profile.hearingSafetyLimitEnabled)
                put("sessionFatigueHistory", profile.sessionFatigueHistory)
            }

            profileFile.writeText(json.toString(2))
            currentProfile = profile
        } catch (e: Exception) {
            Log.w("UserProfileManager", "saveProfile(history) falló", e)
        }
    }

    private val history = mutableListOf<ProfileHistoryEntry>()

    fun getHistory(): List<ProfileHistoryEntry> {
        return history.toList()
    }

    fun replaceHistory(newHistory: List<ProfileHistoryEntry>) {
        history.clear()
        history.addAll(newHistory)
    }
}
