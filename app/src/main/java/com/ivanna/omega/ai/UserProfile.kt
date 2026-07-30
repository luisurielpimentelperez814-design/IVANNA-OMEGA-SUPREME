package com.ivanna/omega.ai

import org.json.JSONObject
import java.io.File
import java.io.FileReader
import java.io.FileWriter
import java.util.concurrent.locks.ReentrantReadWriteLock
import kotlin.concurrent.read
import kotlin.concurrent.write

/**
 * IVANNA OMEGA SUPREME v6.0 - User Profile Data Model & Manager
 * Handles persistent JSON serialization for acoustic preferences, fatigue sensitivity, and listening statistics.
 */
data class UserProfile(
    var listeningStyle: String = "Cognitive Balanced", // "Cognitive Balanced", "Audiophile Flat", "Bass Supreme", "Vocal Protect"
    var bassBoostDb: Float = 1.5f,
    var trebleBoostDb: Float = 0.8f,
    var fatigueSensitivity: Float = 0.5f, // 0.0 (low) to 1.0 (high)
    var moodPreference: Float = 0.5f,     // 0.0 (calm/chill) to 1.0 (energetic/excite)
    var maxVolumeLimitDb: Float = 0.0f,
    var autoEqEnabled: Boolean = true,
    var totalListeningMinutes: Long = 0L,
    var peakFatigueRecorded: Float = 0.12f
) {
    fun toJson(): JSONObject {
        return JSONObject().apply {
            put("listeningStyle", listeningStyle)
            put("bassBoostDb", bassBoostDb.toDouble())
            put("trebleBoostDb", trebleBoostDb.toDouble())
            put("fatigueSensitivity", fatigueSensitivity.toDouble())
            put("moodPreference", moodPreference.toDouble())
            put("maxVolumeLimitDb", maxVolumeLimitDb.toDouble())
            put("autoEqEnabled", autoEqEnabled)
            put("totalListeningMinutes", totalListeningMinutes)
            put("peakFatigueRecorded", peakFatigueRecorded.toDouble())
        }
    }

    companion object {
        fun fromJson(json: JSONObject): UserProfile {
            return UserProfile(
                listeningStyle = json.optString("listeningStyle", "Cognitive Balanced"),
                bassBoostDb = json.optDouble("bassBoostDb", 1.5).toFloat(),
                trebleBoostDb = json.optDouble("trebleBoostDb", 0.8).toFloat(),
                fatigueSensitivity = json.optDouble("fatigueSensitivity", 0.5).toFloat(),
                moodPreference = json.optDouble("moodPreference", 0.5).toFloat(),
                maxVolumeLimitDb = json.optDouble("maxVolumeLimitDb", 0.0).toFloat(),
                autoEqEnabled = json.optBoolean("autoEqEnabled", true),
                totalListeningMinutes = json.optLong("totalListeningMinutes", 0L),
                peakFatigueRecorded = json.optDouble("peakFatigueRecorded", 0.12).toFloat()
            )
        }
    }
}

object UserProfileManager {
    private const val PROFILE_PATH = "/data/adb/ivanna_omega/profile/user_profile.json"
    private val lock = ReentrantReadWriteLock()
    private var cachedProfile: UserProfile? = null

    fun getProfile(): UserProfile {
        lock.read {
            cachedProfile?.let { return it }
        }

        lock.write {
            val file = File(PROFILE_PATH)
            if (!file.exists()) {
                file.parentFile?.mkdirs()
                val defaultProfile = UserProfile()
                saveProfileInternal(defaultProfile, file)
                cachedProfile = defaultProfile
                return defaultProfile
            }

            return try {
                val jsonStr = FileReader(file).use { it.readText() }
                val profile = UserProfile.fromJson(JSONObject(jsonStr))
                cachedProfile = profile
                profile
            } catch (e: Exception) {
                val fallback = UserProfile()
                cachedProfile = fallback
                fallback
            }
        }
    }

    fun saveProfile(profile: UserProfile) {
        lock.write {
            cachedProfile = profile
            val file = File(PROFILE_PATH)
            file.parentFile?.mkdirs()
            saveProfileInternal(profile, file)
        }
    }

    private fun saveProfileInternal(profile: UserProfile, file: File) {
        try {
            FileWriter(file).use { writer ->
                writer.write(profile.toJson().toString(2))
            }
        } catch (e: Exception) {
            e.printStackTrace()
        }
    }
}
