package com.ivanna.omega.ui.oem

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.ivanna.omega.audio.AudioBackendSelector
import com.ivanna.omega.audio.ThermalGovernor
import com.ivanna.omega.audio.UsbAudioProManager
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.magisk.OmegaDaemon
import com.ivanna.omega.magisk.OmegaEngineBridge
import com.ivanna.omega.saf.SaFBridge
import com.ivanna.omega.saf.SaFRoomBridge
import com.ivanna.omega.spatial.IvannaSpatialManager
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

class OemViewModel(app: Application) : AndroidViewModel(app) {

    private val _state = MutableStateFlow(OemState())
    val state: StateFlow<OemState> = _state.asStateFlow()

    init {
        viewModelScope.launch {
            while (true) {
                poll()
                delay(500)
            }
        }
    }

    private fun poll() {
        val loaded = IvannaNativeLib.isLoaded

        // ── Daemon ────────────────────────────────────────────────────────────
        // OmegaEngineBridge.isConnected es la propiedad real (@Volatile Boolean)
        val daemonAlive = OmegaEngineBridge.isConnected

        // ── Backend ───────────────────────────────────────────────────────────
        val backendMode = runCatching { AudioBackendSelector.mode.value }.getOrNull()
        val backend = when (backendMode) {
            AudioBackendSelector.Mode.ROOT_DAEMON    -> OemState.AudioBackend.HEXAGON_DSP
            AudioBackendSelector.Mode.ROOT_NO_DAEMON -> OemState.AudioBackend.NEON_ARM64
            AudioBackendSelector.Mode.NO_ROOT        -> OemState.AudioBackend.CPU_FALLBACK
            else                                     -> OemState.AudioBackend.UNKNOWN
        }

        // ── Thermal ───────────────────────────────────────────────────────────
        val thermalLoad  = ThermalGovernor.currentThermalLoad
        val thermalApiOk = ThermalGovernor.thermalApiAvailable
        val tempC        = runCatching { OmegaDaemon.getTemperature() }.getOrDefault(0f)
        val latencyMs    = runCatching { OmegaDaemon.getLatency()     }.getOrDefault(0f)

        // ── Estado del motor derivado de thermal ──────────────────────────────
        val engineState = when {
            !loaded              -> OemState.EngineState.UNKNOWN
            thermalLoad >= 0.8f  -> OemState.EngineState.RECOVERY
            thermalLoad >= 0.6f  -> OemState.EngineState.POWER_SAVE
            !daemonAlive         -> OemState.EngineState.SUSPENDED
            else                 -> OemState.EngineState.ACTIVE
        }

        // ── Métricas DSP (APIs reales de IvannaNativeLib) ─────────────────────
        val clipCount = if (loaded) runCatching {
            IvannaNativeLib.guardedNative(0) { IvannaNativeLib.nativeGetClipCount() }
        }.getOrDefault(0) else 0

        val evoBestFitness = if (loaded) runCatching {
            IvannaNativeLib.guardedNative(0f) { IvannaNativeLib.nativeGetEvoBestFitness() }
        }.getOrDefault(0f) else 0f

        val evoGeneration = if (loaded) runCatching {
            IvannaNativeLib.guardedNative(0) { IvannaNativeLib.nativeGetGeneration() }
        }.getOrDefault(0) else 0

        val phaseState = if (loaded) runCatching {
            IvannaNativeLib.guardedNative(0f) { IvannaNativeLib.nativeGetPhaseState() }
        }.getOrDefault(0f) else 0f

        // ── HRTF / Espacial ───────────────────────────────────────────────────
        val hrtfReady   = IvannaSpatialManager.ready
        val hrtfSubject = IvannaSpatialManager.activeSubject
        val hrtfLoaded  = IvannaSpatialManager.isHrtfDatasetLoaded()

        // ── SAF ───────────────────────────────────────────────────────────────
        val safConverged  = runCatching { SaFBridge.nativeSaFIsConverged()  }.getOrDefault(false)
        val safError      = runCatching { SaFBridge.nativeSaFGetError()     }.getOrDefault(0f)
        val safIteration  = runCatching { SaFBridge.nativeSaFGetIteration() }.getOrDefault(0)
        val safDiag       = runCatching { SaFRoomBridge.getDiagnostics()    }.getOrDefault(FloatArray(0))

        // ── USB ───────────────────────────────────────────────────────────────
        val usbStreaming = runCatching {
            UsbAudioProManager.getInstance(getApplication()).isActive()
        }.getOrDefault(false)

        // ── Telemetría daemon ─────────────────────────────────────────────────
        val daemonStatus = runCatching { OmegaEngineBridge.requestTelemetry() }.getOrDefault("")

        _state.value = OemState(
            engineState    = engineState,
            backend        = backend,
            nativeLoaded   = loaded,
            daemonAlive    = daemonAlive,
            thermalLoad    = thermalLoad,
            thermalApiOk   = thermalApiOk,
            tempC          = tempC,
            latencyMs      = latencyMs,
            clipCount      = clipCount,
            evoBestFitness = evoBestFitness,
            evoGeneration  = evoGeneration,
            phaseState     = phaseState,
            hrtfReady      = hrtfReady,
            hrtfSubject    = hrtfSubject,
            hrtfLoaded     = hrtfLoaded,
            safConverged   = safConverged,
            safError       = safError,
            safIteration   = safIteration,
            safDiag        = safDiag,
            usbStreaming   = usbStreaming,
            daemonStatus   = daemonStatus,
        )
    }

    // ── Acciones ──────────────────────────────────────────────────────────────

    fun resetClipCount() = runCatching {
        IvannaNativeLib.guardedNative(Unit) { IvannaNativeLib.nativeResetClipCount() }
    }

    fun setAdaptEnabled(en: Boolean) = runCatching {
        IvannaNativeLib.guardedNative(Unit) { IvannaNativeLib.nativeSetAdaptEnabled(en) }
    }

    fun setHrtfEnabled(en: Boolean) = runCatching {
        IvannaNativeLib.guardedNative(Unit) { IvannaNativeLib.nativeSetHRTFEnabled(en) }
    }

    fun setHrtfSubject(subject: String) = runCatching {
        IvannaSpatialManager.setHrtfSubject(subject)
    }

    fun setRoom(rt60: Float, wet: Float) = runCatching {
        OmegaEngineBridge.setRoom(rt60, wet)
    }

    fun disableRoom() = runCatching { OmegaEngineBridge.disableRoom() }

    fun safFeedback(direction: Int, positive: Boolean) = runCatching {
        SaFBridge.nativeSaFFeedback(direction, positive)
        SaFRoomBridge.step()
    }

    fun safReset() = runCatching {
        SaFRoomBridge.reset()
        SaFBridge.nativeSaFReset()
    }
}
