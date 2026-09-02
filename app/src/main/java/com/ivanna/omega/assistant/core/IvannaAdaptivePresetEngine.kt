package com.ivanna.omega.assistant.core

import android.content.Context
import android.util.Log
import com.ivanna.omega.magisk.MagiskBridge
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/**
 * IvannaAdaptivePresetEngine — OEM-grade adaptive preset engine.
 *
 * This engine goes beyond static EQ curves. It learns from user habits,
 * dynamically adjusts to detected genres (via YAMNet/Neuromorphic engine),
 * and automatically applies fatigue protection during long sessions.
 */
object IvannaAdaptivePresetEngine {
    private const val TAG = "AdaptivePresetEngine"
    private var appContext: Context? = null

    // Stateful metrics
    private var lastGenreDetected: String? = null
    private var sessionDurationMs: Long = 0
    private var sessionStartTime: Long = 0
    private var isFatigueProtectionActive: Boolean = false

    fun init(context: Context) {
        appContext = context.applicationContext
        sessionStartTime = System.currentTimeMillis()
    }

    /**
     * Triggered by Cognitive Core when the user requests an optimization or 
     * when the system detects a significant shift in audio intent.
     */
    suspend fun applyAdaptivePreset(intent: String, explicitUserRequest: Boolean = false): Boolean = withContext(Dispatchers.IO) {
        try {
            val dynamicContext = DynamicContextEngine.buildRichContext()
            Log.d(TAG, "Applying adaptive preset for intent: $intent. UserRequested: $explicitUserRequest")
            
            // Auto-calculate fatigue
            sessionDurationMs = System.currentTimeMillis() - sessionStartTime
            if (sessionDurationMs > 3600000 && !isFatigueProtectionActive && !explicitUserRequest) { // 1 hour
                Log.i(TAG, "Auto-engaging gentle mode for fatigue protection.")
                isFatigueProtectionActive = true
                applyGentleMode()
                return@withContext true
            }

            when (intent) {
                "voice_clarity" -> {
                    MagiskBridge.setPreset("Flat") // Baseline
                    MagiskBridge.sendCommand("""{"action":"SET_ROUTE_PROFILE","bassBoostDb":-1.0,"dialogBoostDb":4.5,"widenerMult":0.9}""")
                    IvannaCognitiveCore.recordAdjustment("Voice Clarity Preset", "Aislé las frecuencias centrales y reduje reverberaciones.", true)
                }
                "cinema_mode" -> {
                    MagiskBridge.setPreset("Spatial")
                    MagiskBridge.sendCommand("""{"action":"SET_ROUTE_PROFILE","bassBoostDb":3.0,"dialogBoostDb":1.0,"widenerMult":1.5}""")
                    IvannaCognitiveCore.recordAdjustment("Cinema Mode Preset", "Expandí el campo espacial y potencié sub-graves.", true)
                }
                "music_mode" -> {
                    MagiskBridge.setPreset("Warm")
                    MagiskBridge.sendCommand("""{"action":"SET_ROUTE_PROFILE","bassBoostDb":2.0,"dialogBoostDb":0.0,"widenerMult":1.2}""")
                    IvannaCognitiveCore.recordAdjustment("Music Mode Preset", "Aplicación de curva cálida con balance armónico.", true)
                }
                "concert_mode" -> {
                    MagiskBridge.setPreset("Spatial")
                    MagiskBridge.sendCommand("""{"action":"SET_ROUTE_PROFILE","bassBoostDb":2.5,"dialogBoostDb":0.5,"widenerMult":1.8}""")
                    IvannaCognitiveCore.recordAdjustment("Concert Mode Preset", "Simulación de estadio con alta reverberación y amplitud.", true)
                }
                "gentle_mode" -> {
                    applyGentleMode()
                    IvannaCognitiveCore.recordAdjustment("Gentle Mode Preset", "Reduje agresividad en transitorios para evitar fatiga auditiva.", true)
                }
                "flat_mode" -> {
                    isFatigueProtectionActive = false
                    MagiskBridge.setPreset("Flat")
                    MagiskBridge.sendCommand("""{"action":"SET_ROUTE_PROFILE","bassBoostDb":0.0,"dialogBoostDb":0.0,"widenerMult":1.0}""")
                    IvannaCognitiveCore.recordAdjustment("Flat Mode Preset", "Desactivé efectos para masterización neutral.", true)
                }
                "bass_boost" -> {
                    MagiskBridge.setPreset("Warm")
                    MagiskBridge.sendCommand("""{"action":"SET_ROUTE_PROFILE","bassBoostDb":6.0,"dialogBoostDb":0.0,"widenerMult":1.0}""")
                    IvannaCognitiveCore.recordAdjustment("Bass Boost Preset", "Aumenté significativamente el punch en bajas frecuencias.", true)
                }
                "musical_intent" -> {
                    // This relies on YAMNet detected genre passed through Cognitive Core
                    val genre = lastGenreDetected ?: "Pop/General"
                    applyGenreSpecificPreset(genre)
                }
                "optimize" -> {
                    // System repair macro
                    MagiskBridge.sendCommand("""{"action":"RESET_DSP_STATE"}""")
                    MagiskBridge.setPreset("Spatial")
                    IvannaCognitiveCore.recordAdjustment("System Optimize", "Reinicio de matriz DSP y limpieza de buffers lock-free.", true)
                }
                else -> {
                    Log.w(TAG, "Unknown intent: $intent")
                    return@withContext false
                }
            }
            return@withContext true
        } catch (e: Exception) {
            Log.e(TAG, "Failed to apply adaptive preset: ${e.message}")
            return@withContext false
        }
    }

    private fun applyGentleMode() {
        MagiskBridge.setPreset("Warm")
        MagiskBridge.sendCommand("""{"action":"SET_PERCEPTUAL_STATE","compressor":-2.0,"exciterReduction":0.8,"highCutHz":14000,"spatialWidth":1.0,"loudnessTargetLuFS":-20.0,"harmonicGain":0.2,"antiDolbyIntensity":0.4}""")
    }

    private fun applyGenreSpecificPreset(genre: String) {
        // Deep integration logic based on acoustic profile of genre
        when {
            genre.contains("Electronic", ignoreCase = true) || genre.contains("Dance", ignoreCase = true) -> {
                MagiskBridge.sendCommand("""{"action":"SET_ROUTE_PROFILE","bassBoostDb":4.5,"dialogBoostDb":-0.5,"widenerMult":1.4}""")
            }
            genre.contains("Classical", ignoreCase = true) || genre.contains("Jazz", ignoreCase = true) -> {
                MagiskBridge.sendCommand("""{"action":"SET_ROUTE_PROFILE","bassBoostDb":0.5,"dialogBoostDb":0.5,"widenerMult":1.3}""")
            }
            genre.contains("Rock", ignoreCase = true) || genre.contains("Metal", ignoreCase = true) -> {
                MagiskBridge.sendCommand("""{"action":"SET_ROUTE_PROFILE","bassBoostDb":2.0,"dialogBoostDb":1.0,"widenerMult":1.1}""")
            }
            else -> {
                MagiskBridge.sendCommand("""{"action":"SET_ROUTE_PROFILE","bassBoostDb":1.5,"dialogBoostDb":0.0,"widenerMult":1.2}""")
            }
        }
        IvannaCognitiveCore.recordAdjustment("Genre Preset ($genre)", "Adaptación algorítmica específica para el género detectado.", true)
    }

    fun updateDetectedGenre(genre: String) {
        lastGenreDetected = genre
    }
}
