package com.ivanna.omega.ui.oem

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.ivanna.omega.audio.AudioBackendSelector
import com.ivanna.omega.audio.AudioRouteManager
import com.ivanna.omega.audio.ThermalGovernor
import com.ivanna.omega.audio.UsbAudioProManager
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.magisk.OmegaEngineBridge
import com.ivanna.omega.spatial.IvannaSpatialManager
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

class OemViewModel(app: Application) : AndroidViewModel(app) {

    private val _state = MutableStateFlow(OemState())
    val state: StateFlow<OemState> = _state.asStateFlow()

    // Parámetros controlables (escritura desde UI → motor nativo)
    private val _spatialAngle  = MutableStateFlow(0f)
    private val _spatialElev   = MutableStateFlow(0f)
    private val _spatialDist   = MutableStateFlow(1f)
    private val _roomSize      = MutableStateFlow(0.5f)
    private val _roomPresence  = MutableStateFlow(0.5f)
    private val _roomDiffusion = MutableStateFlow(0.5f)
    private val _expertMode    = MutableStateFlow(false)
    val expertMode: StateFlow<Boolean> = _expertMode.asStateFlow()

    init {
        viewModelScope.launch(Dispatchers.IO) {
            while (true) {
                poll()
                delay(500L)
            }
        }
    }

    private fun poll() {
        val loaded   = IvannaNativeLib.isLoaded
        val daemon   = runCatching { OmegaEngineBridge.isDaemonRunning }.getOrDefault(false)
        val adaptive = if (loaded) runCatching { IvannaNativeLib.nativeIsAdaptiveEngineRunning() }.getOrDefault(false) else false

        val pipeline = if (loaded) runCatching { IvannaNativeLib.nativeGetUnifiedPipelineStatus() }.getOrNull() else null
        val telem    = if (loaded) runCatching { IvannaNativeLib.nativeGetAdaptiveTelemetry()    }.getOrNull() else null
        val chars    = if (loaded) runCatching { IvannaNativeLib.nativeGetAudioCharacteristics()  }.getOrNull() else null
        val probs    = if (loaded) runCatching { IvannaNativeLib.nativeGetClassifierProbabilities() }.getOrNull() else null
        val domClass = if (loaded) runCatching { IvannaNativeLib.nativeGetDominantClass() }.getOrDefault(-1) else -1
        val clips    = if (loaded) runCatching { IvannaNativeLib.nativeGetClipCount() }.getOrDefault(0) else 0
        val fitness  = if (loaded) runCatching { IvannaNativeLib.nativeGetEvoBestFitness() }.getOrDefault(0f) else 0f
        val gen      = if (loaded) runCatching { IvannaNativeLib.nativeGetGeneration() }.getOrDefault(0) else 0

        val backendMode = AudioBackendSelector.mode.value
        val backend = when (backendMode) {
            AudioBackendSelector.Mode.ROOT_DAEMON    -> OemState.AudioBackend.HEXAGON_DSP
            AudioBackendSelector.Mode.ROOT_NO_DAEMON -> OemState.AudioBackend.NEON_ARM64
            AudioBackendSelector.Mode.NO_ROOT        -> OemState.AudioBackend.CPU_FALLBACK
            else                                     -> OemState.AudioBackend.UNKNOWN
        }

        val engineState = when {
            !loaded                                    -> OemState.EngineState.UNKNOWN
            ThermalGovernor.currentThermalLoad >= 0.8f -> OemState.EngineState.RECOVERY
            ThermalGovernor.currentThermalLoad >= 0.6f -> OemState.EngineState.POWER_SAVE
            pipeline?.getOrElse(0) { 0f } == 0f && (pipeline?.getOrElse(1) { 0f } ?: 0f) < 1e-4f
                                                       -> OemState.EngineState.SUSPENDED
            else                                       -> OemState.EngineState.ACTIVE
        }

        _state.value = OemState(
            engineState      = engineState,
            backend          = backend,
            nativeLoaded     = loaded,
            daemonAlive      = daemon,
            adaptiveRunning  = adaptive,
            activeRoute      = pipeline?.getOrElse(0) { 0f } ?: 0f,
            rms              = pipeline?.getOrElse(1) { 0f } ?: 0f,
            peak             = pipeline?.getOrElse(2) { 0f } ?: 0f,
            voiceProtect     = pipeline?.getOrElse(3) { 0f } ?: 0f,
            compAmount       = pipeline?.getOrElse(4) { 0f } ?: 0f,
            exciterRed       = pipeline?.getOrElse(5) { 0f } ?: 0f,
            spatialWidth     = pipeline?.getOrElse(6) { 1f } ?: 1f,
            adaptiveActive   = pipeline?.getOrElse(7) { 0f } ?: 0f,
            grDb             = telem?.getOrElse(2) { 0f } ?: 0f,
            targetGain       = telem?.getOrElse(3) { 1f } ?: 1f,
            safetyMargin     = telem?.getOrElse(7) { 0f } ?: 0f,
            applied          = telem?.getOrElse(9) { 0f } ?: 0f,
            percussiveness   = chars?.getOrElse(2) { 0f } ?: 0f,
            tonality         = chars?.getOrElse(3) { 0f } ?: 0f,
            reverbLevel      = chars?.getOrElse(4) { 0f } ?: 0f,
            dynRange         = chars?.getOrElse(5) { 0f } ?: 0f,
            spectralCentroid = chars?.getOrElse(6) { 2500f } ?: 2500f,
            clipCount        = clips,
            thermalLoad      = ThermalGovernor.currentThermalLoad,
            thermalApiOk     = ThermalGovernor.thermalApiAvailable,
            probVoice        = probs?.getOrElse(0) { 0f } ?: 0f,
            probMusic        = probs?.getOrElse(1) { 0f } ?: 0f,
            probBass         = probs?.getOrElse(2) { 0f } ?: 0f,
            probSilence      = probs?.getOrElse(3) { 0f } ?: 0f,
            dominantClass    = domClass,
            hrtfReady        = IvannaSpatialManager.ready,
            hrtfSubject      = IvannaSpatialManager.activeSubject,
            usbStreaming     = UsbAudioProManager.getInstance(getApplication()).isActive(),
            evoBestFitness   = fitness,
            evoGeneration    = gen,
            latencyUs        = 0L  // medido bajo demanda, no polling
        )
    }

    fun setExpertMode(on: Boolean) { _expertMode.value = on }

    fun measureLatency() = viewModelScope.launch(Dispatchers.IO) {
        if (!IvannaNativeLib.isLoaded) return@launch
        val us = runCatching { IvannaNativeLib.nativeMeasureRoundTripLatencyUs() }.getOrDefault(0L)
        _state.value = _state.value.copy(latencyUs = us)
    }

    fun setSpatialAngle(deg: Float) {
        _spatialAngle.value = deg
        runCatching { IvannaNativeLib.nativeSetSpatialAngleRad(Math.toRadians(deg.toDouble()).toFloat()) }
    }

    fun setSpatialWidth(w: Float) {
        runCatching { IvannaNativeLib.nativeSetSpatialWidthDirect(w) }
    }

    fun resetClips() {
        runCatching { IvannaNativeLib.nativeResetClipCount() }
        _state.value = _state.value.copy(clipCount = 0)
    }
}
