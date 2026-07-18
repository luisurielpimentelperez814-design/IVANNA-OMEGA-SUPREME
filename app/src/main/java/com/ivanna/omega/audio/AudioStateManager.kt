// AudioStateManager.kt
// ============================================================================
// CENTRALIZADO STATE MANAGER — Fuente única de verdad para todo el estado de audio
// LiveData/StateFlow basado para reactividad sin callbacks directos
// © 2026 Luis Uriel Pimentel Pérez
// ============================================================================

package com.ivanna.omega.audio

import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import android.util.Log

data class AudioState(
    // Modo y intensidad del motor adaptativo
    val adaptiveMode: AdaptiveMode = AdaptiveMode.NATURAL,
    val adaptiveIntensity: Float = 0.7f,  // 0..1
    
    // Parámetros de compressor
    val compressorThreshold: Float = -20f,  // dB
    val compressorRatio: Float = 2.0f,
    val compressorAttack: Float = 10f,      // ms
    val compressorRelease: Float = 100f,    // ms
    
    // Parámetros de exciter
    val exciterAmount: Float = 0.5f,        // 0..1
    val exciterFreq: Float = 4000f,         // Hz
    
    // Parámetros espaciales
    val spatialWidth: Float = 1.0f,         // 0..2
    val spatialIntensity: Float = 0.7f,     // 0..1
    val binaural: Boolean = true,
    val manifold: Boolean = false,
    
    // Protección de voz
    val voiceProtectionEnabled: Boolean = false,
    
    // EQ dinámico
    val eqBass: Float = 0f,                 // dB
    val eqMid: Float = 0f,
    val eqTreble: Float = 0f,
    
    // Master
    val masterGain: Float = 1.0f,
    val safetyMargin: Float = 0.9f,
    
    // Flags
    val isDirty: Boolean = false,           // Si hay cambios sin aplicar
    val isAudioRunning: Boolean = false
)

enum class AdaptiveMode {
    OFF, NATURAL, STUDIO, EXTREME
}

object AudioStateManager {
    private const val TAG = "AudioStateManager"
    
    private val _audioState = MutableStateFlow(AudioState())
    val audioState: StateFlow<AudioState> = _audioState
    
    // LiveData para observadores legacy
    private val _audioStateLive = MutableLiveData(AudioState())
    val audioStateLive: LiveData<AudioState> = _audioStateLive
    
    // Histórico para detectar cambios (delta updates)
    private var previousState = AudioState()
    
    /**
     * Actualizar estado con validación y coherencia
     */
    fun updateState(block: (AudioState) -> AudioState) {
        val newState = block(_audioState.value)
        
        // Validar coherencia
        val validatedState = validateState(newState)
        
        // Actualizar si cambió
        if (validatedState != _audioState.value) {
            _audioState.value = validatedState
            _audioStateLive.value = validatedState
            Log.d(TAG, "🎚️ Estado actualizado: mode=${validatedState.adaptiveMode}, intensity=${validatedState.adaptiveIntensity}")
        }
    }
    
    /**
     * Obtener solo los parámetros que cambiaron (para delta updates)
     */
    fun getDeltaChanges(): Map<String, Float> {
        val current = _audioState.value
        val deltas = mutableMapOf<String, Float>()
        
        if (current.compressorThreshold != previousState.compressorThreshold) 
            deltas["compressor_threshold"] = current.compressorThreshold
        if (current.compressorRatio != previousState.compressorRatio)
            deltas["compressor_ratio"] = current.compressorRatio
        if (current.exciterAmount != previousState.exciterAmount)
            deltas["exciter_amount"] = current.exciterAmount
        if (current.spatialWidth != previousState.spatialWidth)
            deltas["spatial_width"] = current.spatialWidth
        if (current.eqBass != previousState.eqBass)
            deltas["eq_bass"] = current.eqBass
        if (current.eqMid != previousState.eqMid)
            deltas["eq_mid"] = current.eqMid
        if (current.eqTreble != previousState.eqTreble)
            deltas["eq_treble"] = current.eqTreble
        if (current.masterGain != previousState.masterGain)
            deltas["master_gain"] = current.masterGain
        
        previousState = current.copy()
        return deltas
    }
    
    /**
     * Validar que el estado sea coherente (ej: no ambos binaural y manifold activos)
     */
    private fun validateState(state: AudioState): AudioState {
        // Exclusión mutua: binaural y manifold no pueden estar ambos activos
        var validated = state
        if (state.binaural && state.manifold) {
            Log.w(TAG, "⚠️ Binaural y Manifold no pueden estar ambos activos. Desactivando Manifold.")
            validated = validated.copy(manifold = false)
        }
        
        // Rango de valores
        validated = validated.copy(
            adaptiveIntensity = validated.adaptiveIntensity.coerceIn(0f, 1f),
            spatialWidth = validated.spatialWidth.coerceIn(0f, 2f),
            exciterAmount = validated.exciterAmount.coerceIn(0f, 1f),
            masterGain = validated.masterGain.coerceIn(0.1f, 2f),
            safetyMargin = validated.safetyMargin.coerceIn(0.5f, 1f)
        )
        
        return validated
    }
    
    /**
     * Revertir a estado anterior (para rollback)
     */
    fun revertState() {
        _audioState.value = previousState
        _audioStateLive.value = previousState
        Log.d(TAG, "↩️ Estado revertido")
    }
    
    /**
     * Reset a valores por defecto
     */
    fun resetToDefaults() {
        updateState { AudioState() }
        Log.d(TAG, "🔄 Reset a valores por defecto")
    }
}
