package com.ivanna.omega.assistant.core

import android.content.Context
import android.content.SharedPreferences
import com.ivanna.omega.ai.memory.IvannaMemoryArchitecture
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*
import kotlinx.serialization.Serializable
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json

class IvannaAdaptivePresetEngine(context: Context, private val memory: IvannaMemoryArchitecture? = null) {
    companion object {
        private const val PREFS_NAME = "ivanna_adaptive_presets"
        private const val KEY_USER_PRESETS = "user_presets"
        private const val KEY_ACTIVE_PRESET = "active_preset"
        private const val KEY_GENRE_AFFINITY = "genre_affinity"
        private const val KEY_LEARNING_ENABLED = "learning_enabled"
    }

    private val prefs: SharedPreferences = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
    private val json = Json { prettyPrint = false; ignoreUnknownKeys = true }
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)

    private val _state = MutableStateFlow(EngineState())
    val state: StateFlow<EngineState> = _state.asStateFlow()

    data class EngineState(val activePreset: AdaptivePreset? = null, val isLearning: Boolean = false, val confidenceScore: Float = 0f, val lastAdjustment: String? = null, val genreAffinity: Map<String, Float> = emptyMap())

    @Serializable
    data class AdaptivePreset(val id: String, val name: String, val category: PresetCategory, val parameters: PresetParameters, val createdAt: Long = System.currentTimeMillis())

    @Serializable
    enum class PresetCategory { BASE, AI_GENERATED, USER }

    @Serializable
    data class PresetParameters(
        val eqBands: List<Float> = List(10) { 0f }, val exciterAmount: Float = 0f, val spatialWidth: Float = 1.0f,
        val reverbRt60: Float = 0f, val compressorAmount: Float = 0f, val bassGainDb: Float = 0f,
        val midGainDb: Float = 0f, val highGainDb: Float = 0f, val highCutFreq: Float = 20000f,
        val hrtfDataset: String = "default", val spatialMode: String = "off", val genreTag: String = "general"
    )

    enum class UserFeedbackSentiment { LOVED, LIKED, NEUTRAL, DISLIKED, HATED }

    private val basePresets = mapOf(
        "studio_reference" to createPreset("Studio Reference", PresetCategory.BASE, flat = true),
        "bass_boost" to createPreset("Bass Boost", PresetCategory.BASE, bassGainDb = 6f),
        "vocal_clarity" to createPreset("Vocal Clarity", PresetCategory.BASE, midGainDb = 3f, highGainDb = 2f),
        "live_room" to createPreset("Live Room", PresetCategory.BASE, reverbRt60 = 1.2f, spatialWidth = 1.3f),
        "cinematic" to createPreset("Cinematic", PresetCategory.BASE, reverbRt60 = 1.8f, spatialWidth = 1.5f, exciterAmount = 0.7f),
        "electronic" to createPreset("Electronic", PresetCategory.BASE, bassGainDb = 4f, exciterAmount = 0.8f, spatialWidth = 1.4f),
        "acoustic" to createPreset("Acoustic", PresetCategory.BASE, midGainDb = 2f, reverbRt60 = 0.8f),
        "rock_70s" to createPreset("Rock 70s", PresetCategory.BASE, bassGainDb = 3f, midGainDb = 1f, exciterAmount = 0.5f),
        "podcast" to createPreset("Podcast", PresetCategory.BASE, midGainDb = 4f, bassGainDb = -2f, spatialWidth = 0.8f),
        "flat" to createPreset("Flat", PresetCategory.BASE, flat = true)
    )

    init { loadPersistedState() }

    fun applyPreset(name: String): Boolean {
        val preset = findPreset(name) ?: return false
        _state.value = _state.value.copy(activePreset = preset, lastAdjustment = "Applied: ${preset.name}")
        persistState()
        return true
    }

    fun saveUserPreset(name: String, parameters: PresetParameters): Boolean {
        val preset = AdaptivePreset(id = "user_${System.currentTimeMillis()}", name = name, category = PresetCategory.USER, parameters = parameters)
        val userPresets = loadUserPresets().toMutableList().apply { add(preset) }
        prefs.edit().putString(KEY_USER_PRESETS, json.encodeToString(userPresets)).apply()
        _state.value = _state.value.copy(lastAdjustment = "Saved user preset: $name")
        return true
    }

    fun recordFeedback(feedback: UserFeedback) {
        val current = _state.value
        val updated = current.genreAffinity.toMutableMap()
        val genre = feedback.detectedGenre ?: current.activePreset?.parameters?.genreTag ?: "unknown"
        val currentAffinity = updated[genre] ?: 0.5f
        val delta = when (feedback.sentiment) { UserFeedbackSentiment.LOVED -> 0.15f; UserFeedbackSentiment.LIKED -> 0.08f; UserFeedbackSentiment.NEUTRAL -> 0f; UserFeedbackSentiment.DISLIKED -> -0.08f; UserFeedbackSentiment.HATED -> -0.15f }
        updated[genre] = (currentAffinity + delta).coerceIn(0f, 1f)
        _state.value = current.copy(genreAffinity = updated, lastAdjustment = "Feedback: ${feedback.sentiment} for $genre")
        scope.launch { memory?.learnFact("genre_affinity_$genre", updated[genre].toString(), IvannaMemoryArchitecture.SemanticRecord.SemanticCategory.AUDIO_PREFERENCE) }
        persistState()
    }

    fun setLearningEnabled(enabled: Boolean) { _state.value = _state.value.copy(isLearning = enabled) }

    suspend fun generateAIPreset(): AdaptivePreset? {
        val affinity = _state.value.genreAffinity
        if (affinity.isEmpty()) return null
        val dominant = affinity.maxByOrNull { it.value }?.key ?: return null
        val params = when (dominant.lowercase()) {
            "rock" -> PresetParameters(bassGainDb = 4f, midGainDb = 2f, exciterAmount = 0.6f, spatialWidth = 1.3f, genreTag = "rock")
            "electronic" -> PresetParameters(bassGainDb = 5f, exciterAmount = 0.9f, spatialWidth = 1.5f, genreTag = "electronic")
            "classical" -> PresetParameters(midGainDb = 1f, highGainDb = 2f, reverbRt60 = 1.5f, exciterAmount = 0.2f, genreTag = "classical")
            "jazz" -> PresetParameters(bassGainDb = 2f, midGainDb = 3f, reverbRt60 = 0.9f, genreTag = "jazz")
            "hiphop" -> PresetParameters(bassGainDb = 7f, compressorAmount = 0.4f, genreTag = "hiphop")
            "podcast", "voice" -> PresetParameters(midGainDb = 5f, bassGainDb = -3f, spatialWidth = 0.7f, highCutFreq = 14000f, genreTag = "voice")
            else -> PresetParameters(genreTag = dominant)
        }
        return AdaptivePreset(id = "ai_${System.currentTimeMillis()}", name = "AI Optimized ($dominant)", category = PresetCategory.AI_GENERATED, parameters = params)
    }

    private fun findPreset(name: String): AdaptivePreset? {
        basePresets[name]?.let { return it }
        return loadUserPresets().find { it.name.equals(name, ignoreCase = true) || it.id == name }
    }

    private fun loadUserPresets(): List<AdaptivePreset> {
        val raw = prefs.getString(KEY_USER_PRESETS, "[]") ?: "[]"
        return runCatching { json.decodeFromString<List<AdaptivePreset>>(raw) }.getOrDefault(emptyList())
    }

    private fun persistState() {
        val state = _state.value
        prefs.edit()
            .putString(KEY_ACTIVE_PRESET, state.activePreset?.let { json.encodeToString(it) })
            .putString(KEY_GENRE_AFFINITY, json.encodeToString(state.genreAffinity))
            .putBoolean(KEY_LEARNING_ENABLED, state.isLearning)
            .apply()
    }

    private fun loadPersistedState() {
        val active = prefs.getString(KEY_ACTIVE_PRESET, null)?.let { runCatching { json.decodeFromString<AdaptivePreset>(it) }.getOrNull() }
        val affinity = prefs.getString(KEY_GENRE_AFFINITY, null)?.let { runCatching { json.decodeFromString<Map<String, Float>>(it) }.getOrDefault(emptyMap()) } ?: emptyMap()
        _state.value = EngineState(activePreset = active, isLearning = prefs.getBoolean(KEY_LEARNING_ENABLED, false), genreAffinity = affinity)
    }

    private fun createPreset(name: String, category: PresetCategory, flat: Boolean = false, bassGainDb: Float = 0f, midGainDb: Float = 0f, highGainDb: Float = 0f, exciterAmount: Float = 0f, spatialWidth: Float = 1.0f, reverbRt60: Float = 0f, compressorAmount: Float = 0f, highCutFreq: Float = 20000f, genreTag: String = "general"): AdaptivePreset {
        val eq = if (flat) List(10) { 0f } else listOf(
            bassGainDb * 0.8f, bassGainDb * 0.9f, bassGainDb, midGainDb * 0.5f,
            midGainDb, midGainDb, midGainDb * 0.8f, highGainDb * 0.7f, highGainDb, highGainDb * 0.6f
        )
        return AdaptivePreset(id = name.lowercase().replace(" ", "_"), name = name, category = category,
            parameters = PresetParameters(eqBands = eq, exciterAmount = exciterAmount, spatialWidth = spatialWidth,
                reverbRt60 = reverbRt60, compressorAmount = compressorAmount, bassGainDb = bassGainDb,
                midGainDb = midGainDb, highGainDb = highGainDb, highCutFreq = highCutFreq, genreTag = genreTag))
    }

    data class UserFeedback(val sentiment: UserFeedbackSentiment, val detectedGenre: String? = null)
}
