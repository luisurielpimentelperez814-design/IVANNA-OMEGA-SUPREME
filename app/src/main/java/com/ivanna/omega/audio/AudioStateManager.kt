package com.ivanna.omega.audio

import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import kotlinx.coroutines.flow.drop
import kotlinx.coroutines.flow.collect
import android.content.Context
import android.util.Log
import com.ivanna.omega.core.IvannaNativeLib

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
    // Banda de presencia EQ (2-5 kHz) — slider propio, no reutiliza spatialWidth
    val eqPresence: Float = 0f,
    val masterGain: Float = 1.0f,
    val safetyMargin: Float = 0.9f,
    val manualModeEnabled: Boolean = false,
    val isDirty: Boolean = false,
    val isAudioRunning: Boolean = false,
    // Phase Oracle — intensidad global de coherencia de fase (0=off, 1=max)
    // Mapea a alpha/beta/gamma en nativeSetPhaseParameters
    val phaseOracleIntensity: Float = 0f
)

enum class AdaptiveMode(val label: String) {
    OFF("OFF"), NATURAL("NATURAL"), STUDIO("STUDIO"), EXTREME("EXTREME")
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
            Log.d(TAG, "Estado actualizado: mode=${validatedState.adaptiveMode}")
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
        if (current.phaseOracleIntensity != previousState.phaseOracleIntensity)
            deltas["phase_oracle_intensity"] = current.phaseOracleIntensity
        previousState = current.copy()
        return deltas
    }

    private fun validateState(state: AudioState): AudioState {
        var validated = state
        if (state.binaural && state.manifold) {
            validated = validated.copy(manifold = false)
        }
        validated = validated.copy(
            adaptiveIntensity = validated.adaptiveIntensity.coerceIn(0f, 1f),
            spatialWidth = validated.spatialWidth.coerceIn(0f, 2f),
            exciterAmount = validated.exciterAmount.coerceIn(0f, 1f),
            masterGain = validated.masterGain.coerceIn(0.1f, 2f),
            safetyMargin = validated.safetyMargin.coerceIn(0.5f, 1f),
            phaseOracleIntensity = validated.phaseOracleIntensity.coerceIn(0f, 1f)
        )
        return validated
    }

    fun revertState() {
        _audioState.value = previousState
        _audioStateLive.value = previousState
    }

    fun resetToDefaults() {
        updateState { AudioState() }
    }

    // Alias para compatibilidad con código que usa .state en lugar de .audioState
    val state: StateFlow<AudioState> get() = _audioState
    fun getCurrentState(): AudioState = _audioState.value

    // ────────────────────────────────────────────────────────────
    // FIX v3.7 — Persistencia automática de sliders/switches
    // Cualquier cambio en audioState se guarda a disco con debounce.
    // Llamar attachPersistence(ctx) UNA vez desde IVANNAApplication.
    // ────────────────────────────────────────────────────────────
    @Volatile private var persistenceAttached = false
    private val persistScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    fun attachPersistence(context: Context) {
        if (persistenceAttached) return
        persistenceAttached = true
        val store = ParameterStore(context.applicationContext)
        // Cargar estado previo al arranque
        val restored = store.loadParameters()
        _audioState.value = restored
        _audioStateLive.postValue(restored)

        // FIX P1+P4 (auditoría 2026-08-18): el restore anterior solo poblaba
        // el StateFlow — la UI mostraba los sliders en su posición guardada,
        // pero el motor DSP nativo seguía con los defaults de arranque. La
        // app "recordaba" visualmente y sonaba con otros valores. Se empujan
        // los campos restaurados al nativo una sola vez aquí, en el mismo
        // punto donde el estado entra en memoria.
        restoreToNative(restored)

        // Guardar en cada cambio (debounced dentro de ParameterStore)
        persistScope.launch {
            _audioState.drop(1).collect { store.saveParametersDebounced(it) }
        }
    }

    /**
     * Empuja un AudioState restaurado al motor nativo (libivanna_omega.so).
     *
     * No-op seguro si la librería no cargó (IvannaNativeLib.isLoaded=false) —
     * el DSP nativo no existe en ese contexto y no hay nada que restaurar.
     * Cada setter va en runCatching individual: un JNI ausente o un valor
     * fuera de rango en un campo no debe abortar el restore de los demás.
     *
     * Se llama SOLO desde attachPersistence (una vez por arranque del
     * proceso). No llamar desde updateState — eso re-escribiría el nativo
     * en cada tick de slider (el DSP ya se actualiza por su propio camino:
     * DSPState.pushToNative / AdaptiveBackend).
     */
    private fun restoreToNative(state: AudioState) {
        if (!IvannaNativeLib.isLoaded) {
            Log.d(TAG, "restoreToNative: lib no cargada — restore nativo omitido")
            return
        }
        runCatching {
            IvannaNativeLib.nativeSetEQParams(
                state.eqBass, state.eqMid, state.eqTreble, state.masterGain)
        }.onFailure { Log.w(TAG, "restore EQ falló: ${it.message}") }
        runCatching {
            IvannaNativeLib.nativeSetCompressorParams(
                state.compressorThreshold, state.compressorRatio,
                state.compressorAttack, state.compressorRelease)
        }.onFailure { Log.w(TAG, "restore compressor falló: ${it.message}") }
        runCatching {
            IvannaNativeLib.nativeSetSpatialWidthDirect(state.spatialWidth)
        }.onFailure { Log.w(TAG, "restore spatialWidth falló: ${it.message}") }
        runCatching {
            IvannaNativeLib.nativeSetSpatialWet(state.spatialIntensity)
        }.onFailure { Log.w(TAG, "restore spatialIntensity falló: ${it.message}") }
        runCatching {
            IvannaNativeLib.nativeSetHarmonicGain(state.exciterAmount)
        }.onFailure { Log.w(TAG, "restore exciter falló: ${it.message}") }
        Log.i(TAG, "restoreToNative: estado restaurado al DSP " +
            "(eq=[${state.eqBass},${state.eqMid},${state.eqTreble}] " +
            "master=${state.masterGain} comp=${state.compressorThreshold}dB/${state.compressorRatio} " +
            "width=${state.spatialWidth} wet=${state.spatialIntensity} exc=${state.exciterAmount})")
    }
}
