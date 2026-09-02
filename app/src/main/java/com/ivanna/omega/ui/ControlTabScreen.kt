package com.ivanna.omega.ui

import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.runtime.livedata.observeAsState
import androidx.navigation.NavHostController
import com.ivanna.omega.audio.AdaptiveBackend
import com.ivanna.omega.audio.AntiDolbyController
import com.ivanna.omega.audio.AudioStateManager
import com.ivanna.omega.audio.OmegaMetrics
import com.ivanna.omega.audio.ProfilesLoader
import com.ivanna.omega.audio.VoiceProtectionManager
import com.ivanna.omega.audio.VolterraSwitch
import com.ivanna.omega.audio.toSnapshot
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.core.ParameterStore
import com.ivanna.omega.dsp.DSPState
import com.ivanna.omega.magisk.OmegaEngineBridge
import com.ivanna.omega.neuromorphic.PiLstmBridge
import com.ivanna.omega.spatial.IvannaSpatialEngine
import kotlin.math.PI

/**
 * ControlTabScreen — FIX C (crítico):
 *
 * El tab CONTROL de [MainScaffold] llamaba a [IvannaControlPanel] con sólo 9
 * parámetros; los otros 19 callbacks (anti-Dolby, presets, auto/omega mode,
 * compresor, NHO, spatial, EVO, NPE, Phase Oracle) caían en su valor por
 * defecto `{}`, así que mover esos knobs no producía NINGÚN cambio de audio.
 *
 * Este composable concentra el cableado real —el mismo que ya existía en el
 * DashboardScreen legacy de MainActivity— para que el tab CONTROL controle de
 * verdad el motor. No se borra nada: MainScaffold simplemente delega aquí.
 */
@Composable
fun ControlTabScreen(
    outerNav : NavHostController,
    dsp      : MutableState<DSPState>,
    adaptiveBack : AdaptiveBackend,
    voiceMgr : VoiceProtectionManager,
    metrics  : OmegaMetrics = OmegaMetrics(),
    onOpenAdaptiveTab : () -> Unit = {},
    onOpenSpatialTab  : () -> Unit = {},
    onOpenBrainTab    : () -> Unit = {},
    modifier : Modifier = Modifier
) {
    val context = LocalContext.current
    val paramStore = remember { ParameterStore(context) }
    val audioState by AudioStateManager.audioState.collectAsState()
    val routeState by com.ivanna.omega.audio.IvannaUnifiedPipeline.state.collectAsState()
    val voiceActive by voiceMgr.voiceProtectionActive.observeAsState(false)
    // FIX (persistencia): el bypass NPE arrancaba siempre en false aunque el
    // usuario lo hubiera activado y ParameterStore ya lo guardara
    // (KEY_NPE_BYPASS). Ahora se restaura desde prefs y se reaplica al motor
    // nativo — el estado del toggle sobrevive cierre de app y reboot.
    var npeBypassState by remember { mutableStateOf(paramStore.isNpeBypass()) }

    LaunchedEffect(Unit) {
        if (npeBypassState && PiLstmBridge.isReady) {
            PiLstmBridge.setBypass(true)
        }
    }

    val adaptiveTelemetryRaw by adaptiveBack.telemetry.collectAsState()
    val adaptiveTelemetry = adaptiveTelemetryRaw.toSnapshot()

    val antiDolbyController = remember {
        AntiDolbyController(context).also { ctrl ->
            ctrl.initialize()
            ctrl.onDspUpdate = { exciter, width, eqGainDb ->
                dsp.value = dsp.value.copy(wet = exciter, stereoWidth = width)
                dsp.value.pushToNative()
                if (IvannaNativeLib.isLoaded)
                    IvannaNativeLib.guardedNative(Unit) { IvannaNativeLib.nativeSetEQParams(eqGainDb, eqGainDb, eqGainDb, dsp.value.master) }
            }
        }
    }

    IvannaControlPanel(
        initialExciter       = dsp.value.wet,
        initialEq            = dsp.value.mid,
        initialWidth         = dsp.value.stereoWidth,
        initialCompThreshold = dsp.value.alpha,
        initialCompRatio     = dsp.value.beta,
        initialAutoMode      = paramStore.isAutoModeEnabled(),
        initialOmegaMode     = paramStore.getOmegaMode(),
        initialSpatialEnabled = IvannaSpatialEngine.enabled,
        metrics              = metrics,
        adaptiveTelemetry    = adaptiveTelemetry,
        routeState           = routeState,

        // ── DSP core ────────────────────────────────────────────────────
        onExciterChange = { dsp.value = dsp.value.copy(wet = it); dsp.value.pushToNative() },
        onEqChange      = {
            dsp.value = dsp.value.copy(low = it, mid = it, high = it, presence = it)
            dsp.value.pushToNative()
        },
        onWidthChange         = { dsp.value = dsp.value.copy(stereoWidth = it); dsp.value.pushToNative() },
        onCompThresholdChange = { dsp.value = dsp.value.copy(alpha = it); dsp.value.pushToNative() },
        onCompRatioChange     = { dsp.value = dsp.value.copy(beta = it); dsp.value.pushToNative() },
        onNhoHarmonicChange   = { if (IvannaNativeLib.isLoaded) IvannaNativeLib.nativeSetHarmonicGain(it) },
        onEvoEnabledChange    = { enabled ->
            if (IvannaNativeLib.isLoaded) {
                if (enabled) IvannaNativeLib.guardedNative(Unit) { IvannaNativeLib.nativeStartEvoThread() }
                else IvannaNativeLib.guardedNative(Unit) { IvannaNativeLib.nativeStopEvoThread() }
            }
        },

        // ── NPE / PI-LSTM ───────────────────────────────────────────────
        onNpeBypassChange = { on ->
            npeBypassState = on
            paramStore.setNpeBypass(on)   // persistir — antes solo vivía en memoria
            if (PiLstmBridge.isReady) PiLstmBridge.setBypass(on)
        },
        onNpeHarmonicChange       = { v -> if (PiLstmBridge.isReady && !npeBypassState) PiLstmBridge.setHarmonicGain(v) },
        onNpeLateralInhibChange   = { v -> if (PiLstmBridge.isReady && !npeBypassState) PiLstmBridge.setBeta(v) },
        onNpeOhcCompressionChange = { v -> if (PiLstmBridge.isReady && !npeBypassState) PiLstmBridge.setAlpha(v) },
        onNpeMasterGainChange     = { v -> if (PiLstmBridge.isReady && !npeBypassState) PiLstmBridge.setMasterGain(v) },
        onNpeAgcChange            = { targetDb, rate ->
            if (PiLstmBridge.isReady && !npeBypassState) PiLstmBridge.setAgc(targetDb, rate)
        },
        onNpeFlagsChange = { hrtf, cochlear, adapt ->
            if (PiLstmBridge.isReady) {
                PiLstmBridge.setHrtfEnabled(hrtf)
                PiLstmBridge.setCochlearEnabled(cochlear)
                PiLstmBridge.setAdaptEnabled(adapt)
            }
        },
        onNpeManifoldChange = { enabled -> VolterraSwitch.enabled = enabled },

        // ── Spatial ─────────────────────────────────────────────────────
        onSpatialAngleChange = {
            val rad = (it - 0.5f) * 2f * PI.toFloat()
            if (IvannaNativeLib.isLoaded) IvannaNativeLib.nativeSetSpatialAngleRad(rad)
            IvannaSpatialEngine.setAzimuth(rad)
        },
        onSpatialWidthChange = {
            if (IvannaNativeLib.isLoaded) IvannaNativeLib.nativeSetSpatialWidthDirect(it)
            IvannaSpatialEngine.setWidth(it)
        },
        onSpatialEnabledChange = { on -> IvannaSpatialEngine.enabled = on },

        // ── Anti-Dolby / presets / modos ────────────────────────────────
        onAntiDolbyChange = { enabled ->
            if (enabled) antiDolbyController.enableAntiDolby()
            else antiDolbyController.disableAntiDolby()
        },
        onPresetSelected = { presetName ->
            val profile = ProfilesLoader.load(context)
                .firstOrNull { it.name.equals(presetName, ignoreCase = true) }
            if (profile != null) {
                dsp.value = dsp.value.copy(
                    wet         = profile.audioEngine.exciterAmount,
                    low         = profile.audioEngine.eqGain,
                    mid         = profile.audioEngine.eqGain,
                    high        = profile.audioEngine.eqGain,
                    presence    = profile.audioEngine.eqGain,
                    stereoWidth = profile.audioEngine.widthAmount
                )
                dsp.value.pushToNative()
            }
        },
        onAutoModeChange = { enabled ->
            paramStore.setAutoModeEnabled(enabled)
            AudioStateManager.updateState { it.copy(manualModeEnabled = !enabled) }
        },
        onOmegaModeChange = { mode ->
            paramStore.setOmegaMode(mode)
            OmegaEngineBridge.setIntensity(mode / 2f)
        },
        onPhaseOracleChange = { intensity ->
            val i = intensity.coerceIn(0f, 1f)
            AudioStateManager.updateState { it.copy(phaseOracleIntensity = i) }
            if (IvannaNativeLib.isLoaded) {
                runCatching { IvannaNativeLib.nativeSetPhaseParameters(i, i * 0.7f, i * 0.5f) }
            }
        },

        // ── Adaptive / voz ──────────────────────────────────────────────
        adaptiveMode = audioState.adaptiveMode,
        onAdaptiveModeChange = { mode ->
            AudioStateManager.updateState { it.copy(adaptiveMode = mode) }
            if (audioState.manualModeEnabled)
                adaptiveBack.applyManualState(AudioStateManager.audioState.value)
        },
        adaptiveIntensity = audioState.adaptiveIntensity * 100f,
        onAdaptiveIntensityChange = { percent ->
            AudioStateManager.updateState { it.copy(adaptiveIntensity = percent / 100f) }
            if (audioState.manualModeEnabled)
                adaptiveBack.applyManualState(AudioStateManager.audioState.value)
        },
        voiceProtectionEnabled  = voiceActive,
        onVoiceProtectionChange = { voiceMgr.toggle() },

        // ── Navegación ──────────────────────────────────────────────────
        onOpenVisualizer           = onOpenSpatialTab,
        onOpenAdaptive             = onOpenAdaptiveTab,
        onOpenAdaptiveEngineManual = onOpenAdaptiveTab,
        onOpenOpe                  = onOpenSpatialTab,
        onOpenBinaural             = onOpenSpatialTab,
        onOpenTelemetry            = onOpenAdaptiveTab,
        onOpenAdaptiveProfiles     = { outerNav.navigate("adaptive_profiles") },
        onOpenProfiles             = { outerNav.navigate("profiles") },
        onOpenMagisk               = { outerNav.navigate("magisk") },
        modifier                   = modifier
    )
}
