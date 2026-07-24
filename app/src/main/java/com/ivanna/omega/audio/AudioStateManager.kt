package com.ivanna.omega.audio

import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import android.util.Log

data class AudioState(
    val adaptiveMode: AdaptiveMode = AdaptiveMode.NATURAL,
    val adaptiveIntensity: Float = 0.7f,
    val compressorThreshold: Float = -20f,
    val compressorRatio: Float = 2.0f,
    val compressorAttack: Float = 10f,
    val compressorRelease: Float = 100f,
    val exciterAmount: Float = 0.5f,
    val exciterFreq: Float = 4000f,
    val spatialWidth: Float = 1.0f,
    val spatialIntensity: Float = 0.7f,
    val binaural: Boolean = true,
    val manifold: Boolean = false,
    val voiceProtectionEnabled: Boolean = false,
    val eqBass: Float = 0f,
    val eqMid: Float = 0f,
    val eqTreble: Float = 0f,
    val masterGain: Float = 1.0f,
    val safetyMargin: Float = 0.9f,
    val manualModeEnabled: Boolean = false,
    val isDirty: Boolean = false,
    val isAudioRunning: Boolean = false
)

enum class AdaptiveMode {
    OFF, NATURAL, STUDIO, EXTREME
}

object AudioStateManager {
    private const val TAG = "AudioStateManager"

    private val _audioState = MutableStateFlow(AudioState())
    val audioState: StateFlow<AudioState> = _audioState

    private val _audioStateLive = MutableLiveData(AudioState())
    val audioStateLive: LiveData<AudioState> = _audioStateLive

    private var previousState = AudioState()

    fun updateState(block: (AudioState) -> AudioState) {
        val newState = block(_audioState.value)
        val validatedState = validateState(newState)
        if (validatedState != _audioState.value) {
            _audioState.value = validatedState
            _audioStateLive.value = validatedState
            Log.d(TAG, "🎚️ Estado actualizado: mode=${validatedState.adaptiveMode}, intensity=${validatedState.adaptiveIntensity}, manual=${validatedState.manualModeEnabled}")
        }
    }

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

    private fun validateState(state: AudioState): AudioState {
        var validated = state
        if (state.binaural && state.manifold) {
            Log.w(TAG, "⚠️ Binaural y Manifold no pueden estar ambos activos. Desactivando Manifold.")
            validated = validated.copy(manifold = false)
        }

        validated = validated.copy(
            adaptiveIntensity = validated.adaptiveIntensity.coerceIn(0f, 1f),
            spatialWidth = validated.spatialWidth.coerceIn(0f, 2f),
            exciterAmount = validated.exciterAmount.coerceIn(0f, 1f),
            masterGain = validated.masterGain.coerceIn(0.1f, 2f),
            safetyMargin = validated.safetyMargin.coerceIn(0.5f, 1f)
        )

        return validated
    }

    fun revertState() {
        _audioState.value = previousState
        _audioStateLive.value = previousState
        Log.d(TAG, "↩️ Estado revertido")
    }

    fun resetToDefaults() {
        updateState { AudioState() }
        Log.d(TAG, "🔄 Reset a valores por defecto")
    }
}
