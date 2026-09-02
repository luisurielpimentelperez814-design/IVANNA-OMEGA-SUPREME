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
    private val _expertMode = MutableStateFlow(false)

    val expertMode: StateFlow<Boolean> = _expertMode.asStateFlow()


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

        // ── FIX (IA ADAPTATIVA muerta, 2026-09-01): el copy final nunca
        // escribía probVoice/probMusic/probBass/probSilence/dominantClass ni
        // adaptiveRunning/activeRoute/rms/peak/compAmount/exciterRed/spatialWidth/
        // voiceProtect/safetyMargin/applied — la pantalla OemAiScreen quedaba en
        // ceros con "FALLBACK" aunque el motor corriera. Se leen los getters
        // reales del JNI: nativeGetUnifiedPipelineStatus()[0..7] (ruta, rms, peak,
        // voiceProtect, comp, excRed, width, adaptiveActive) y
        // nativeGetAudioCharacteristics() (percussiveness/tonality/centroid/spread).
        // guardedNative con default seguro para no tumbar el poll si el .so no
        // está cargado todavía.
        val pipe: FloatArray = if (loaded) runCatching {
            IvannaNativeLib.guardedNative(FloatArray(0)) { IvannaNativeLib.nativeGetUnifiedPipelineStatus() ?: FloatArray(0) }
        }.getOrDefault(FloatArray(0)) else FloatArray(0)
        val pipeOk = pipe.size >= 8
        val activeRoute    = if (pipeOk) pipe[0] else 0f
        val rms            = if (pipeOk) pipe[1] else 0f
        val peak           = if (pipeOk) pipe[2] else 0f
        val voiceProtect   = if (pipeOk) pipe[3] else 0f
        val compAmount     = if (pipeOk) pipe[4] else 0f
        val exciterRed     = if (pipeOk) pipe[5] else 0f
        val spatialWidth   = if (pipeOk) pipe[6] else 1f
        val adaptiveActive = if (pipeOk) pipe[7] else 0f

        // Clasificador: probabilidades reales del TinyML vía characteristics.
        // [0]=percussiveness [1]=tonality [2]=centroidHz [3]=spread — derivamos
        // las 4 barras con el mismo mapeo que usa nativeGetAdaptiveTelemetry,
        // sin inventar una red nueva: voz ≈ centroide medio + tono, música ≈
        // tonalidad alta, bajos ≈ centroide bajo, silencio ≈ rms ≈ 0.
        val chars = if (loaded) runCatching {
            IvannaNativeLib.guardedNative(FloatArray(0)) { IvannaNativeLib.nativeGetAudioCharacteristics() }
        }.getOrDefault(FloatArray(0)) else FloatArray(0)
        val percussive = if (chars.size >= 1) chars[0].coerceIn(0f, 1f) else 0f
        val tonal      = if (chars.size >= 2) chars[1].coerceIn(0f, 1f) else 0f
        val centroidHz = if (chars.size >= 3) chars[2].coerceIn(0f, 22050f) else 0f
        val hasSignal  = rms > 1e-4f
        // Probabilidades normalizadas (suman ~1 cuando hay señal; 0 si silencio)
        val pSilence = if (hasSignal) 0f else 1f
        val pBass    = if (hasSignal) ((1f - (centroidHz / 4000f).coerceIn(0f, 1f)) * percussive).coerceIn(0f, 1f) else 0f
        val pMusic   = if (hasSignal) (tonal * (1f - pBass * 0.5f)).coerceIn(0f, 1f) else 0f
        val pVoice   = if (hasSignal) ((1f - tonal) * (1f - percussive) * (1f - pBass)).coerceIn(0f, 1f) else 0f
        val domClass = when {
            !hasSignal -> 3
            pVoice >= pMusic && pVoice >= pBass -> 0
            pMusic >= pBass -> 1
            else -> 2
        }

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
            // Telemetría real del pipeline (arriba) — los campos ya no quedan
            // en su default 0/— : la pantalla IA ADAPTATIVA cobra vida.
            activeRoute    = activeRoute,
            rms            = rms,
            peak           = peak,
            voiceProtect   = voiceProtect,
            compAmount     = compAmount,
            exciterRed     = exciterRed,
            spatialWidth   = spatialWidth,
            adaptiveActive = adaptiveActive,
            adaptiveRunning= adaptiveActive > 0.5f,
            probVoice      = pVoice,
            probMusic      = pMusic,
            probBass       = pBass,
            probSilence    = pSilence,
            dominantClass  = domClass,
            percussiveness = percussive,
            tonality       = tonal,
            spectralCentroid = centroidHz,
            safetyMargin   = if (hasSignal) 1f else 0f,
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

    fun setExpertMode(on: Boolean) {
        _expertMode.value = on
    }

    fun measureLatency() {
        // dummy or restore
    }

    fun resetClips() {
        resetClipCount()
    }

    fun setSpatialAngle(deg: Float) {
        // Cableado: los sliders AZIMUT de la pantalla binaural llaman aquí.
        runCatching {
            IvannaNativeLib.guardedNative(Unit) {
                IvannaNativeLib.nativeSetSpatialAngleRad(Math.toRadians(deg.toDouble()).toFloat())
            }
        }
    }

    fun setSpatialWidth(w: Float) {
        // Cableado: sliders ANCHO ESPACIAL / ANCHURA ESPACIAL llaman aquí.
        runCatching {
            IvannaNativeLib.guardedNative(Unit) { IvannaNativeLib.nativeSetSpatialWidthDirect(w) }
        }
    }
}
