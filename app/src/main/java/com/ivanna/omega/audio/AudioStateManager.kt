package com.ivanna.omega.audio

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

object AudioStateManager {
    private val lock = Any()
    private val _state = MutableStateFlow(AudioState())
    val state: StateFlow<AudioState> = _state

    fun updateState(block: (AudioState) -> AudioState) {
        synchronized(lock) {
            val current = _state.value
            val updated = block(current)
            _state.value = updated
        }
    }

    fun getCurrentState(): AudioState = synchronized(lock) { _state.value }

    fun applyProfileSafely(profile: IvannaAudioProfile) {
        synchronized(lock) {
            val current = _state.value
            val validator = ParameterValidator
            
            val gainSafe = validator.validateGain(profile.audioEngine.gain)
            val exciterSafe = validator.validateExciter(profile.audioEngine.exciterAmount)
            val eqSafe = validator.validateEQ(
                profile.audioEngine.eqGain,
                profile.audioEngine.eqGain,
                profile.audioEngine.eqGain,
                profile.audioEngine.gain
            )
            val widthSafe = validator.validateWidth(profile.audioEngine.widthAmount)

            val (sp, mu, ba) = validator.normalizeAntiDolbyScores(
                profile.antiDolby.speechThreshold,
                profile.antiDolby.bassThreshold
            )

            _state.value = current.copy(
                master = gainSafe,
                wet = exciterSafe,
                low = eqSafe[0],
                mid = eqSafe[1],
                high = eqSafe[2],
                presence = eqSafe[3],
                stereoWidth = widthSafe
            )

            // Aplicar a JNI
            JniSafeWrapper.safeSetCompressorParams(
                profile.audioEngine.eqGain,
                profile.audioEngine.eqGain,
                profile.audioEngine.eqGain,
                profile.audioEngine.eqGain
            )
            JniSafeWrapper.safeSetHarmonicGain(exciterSafe)
            JniSafeWrapper.safeSetSpatialWidth(widthSafe)
            JniSafeWrapper.safeSetAntiDolbyScores(sp, mu, ba)
        }
    }
}
