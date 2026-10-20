package com.ivanna.omega.ui

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
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

    // La pestaña CONTROL es la pantalla activa que dibuja AdaptiveEngineStatusCard.
    // No depender de que otra ruta (dashboard legado o AdaptiveEngineScreen) haya
    // iniciado el sondeo: sin este ciclo el panel conserva el snapshot inicial 0/OFFLINE.
    DisposableEffect(adaptiveBack) {
        adaptiveBack.startTelemetry()
        onDispose { adaptiveBack.stopTelemetry() }
    }
    val audioState by AudioStateManager.audioState.collectAsState()
    val routeState by com.ivanna.omega.audio.IvannaUnifiedPipeline.state.collectAsState()
    val voiceActive by voiceMgr.voiceProtectionActive.observeAsState(false)
    // FIX (persistencia): el bypass NPE arrancaba siempre en false aunque el
    // usuario lo hubiera activado y ParameterStore ya lo guardara
    // (KEY_NPE_BYPASS). Ahora se restaura desde prefs y se reaplica al motor
    // nativo — el estado del toggle sobrevive cierre de app y reboot.
    var npeBypassState by remember { mutableStateOf(paramStore.isNpeBypass()) }

    // ── Corrección acústica de sala (medición con el micrófono) ─────────────
    var roomGains by remember { mutableStateOf(paramStore.getRoomCorrectionGains()) }
    var roomEnabled by remember { mutableStateOf(paramStore.isRoomCorrectionEnabled()) }
    var roomBusy by remember { mutableStateOf(false) }
    var roomProgress by remember { mutableStateOf(0f) }
    var roomStatus by remember { mutableStateOf("") }
    val roomScope = rememberCoroutineScope()
    // Canal conflado: los cambios rápidos del slider conservan solo el último
    // estado y el socket del daemon nunca bloquea el hilo de interfaz.
    val perceptualCommands = remember { Channel<String>(Channel.CONFLATED) }
    LaunchedEffect(perceptualCommands) {
        for (command in perceptualCommands) {
            withContext(Dispatchers.IO) {
                runCatching { com.ivanna.omega.magisk.MagiskBridge.sendCommand(command) }
            }
        }
    }
    val applyRoom: (FloatArray?, Boolean) -> Unit = { g, en ->
        if (IvannaNativeLib.isLoaded)
            runCatching { IvannaNativeLib.nativeSetRoomCorrection(if (en && g != null) g else FloatArray(10)) }
    }
    val startRoomMeasure: () -> Unit = {
        if (!roomBusy) roomScope.launch {
            val prevGains = roomGains; val prevEnabled = roomEnabled
            roomBusy = true; roomProgress = 0f
            roomStatus = "Prepara: volumen cómodo, teléfono donde te sientas, sala en silencio."
            try {
                when (val out = com.ivanna.omega.audio.RoomMeasurement.runVerified(
                    context, prevGains, prevEnabled, applyRoom
                ) { roomProgress = it }) {
                    is com.ivanna.omega.audio.RoomMeasurement.VerifiedOutcome.Success -> {
                        paramStore.setRoomCorrection(out.gainsDb, true)
                        runCatching { com.ivanna.omega.audio.BassHaptics.setMeasuredLatency(context, out.latencyMs) }
                        roomGains = out.gainsDb; roomEnabled = true
                        applyRoom(out.gainsDb, true)
                        roomStatus = "Verificado · error tonal ${"%.2f".format(java.util.Locale.ROOT, out.beforeErrorDb)} → ${"%.2f".format(java.util.Locale.ROOT, out.afterErrorDb)} dB · decisión #${out.decisionId}"
                    }
                    is com.ivanna.omega.audio.RoomMeasurement.VerifiedOutcome.Failure -> {
                        applyRoom(prevGains, prevEnabled)
                        roomStatus = "Reversión automática · ${out.reason} · decisión #${out.decisionId}"
                    }
                }
            } catch (t: Throwable) {
                applyRoom(prevGains, prevEnabled)
                roomStatus = "Falló la calibración; se restauró el perfil previo: ${t.message ?: "error desconocido"}"
            } finally {
                roomBusy = false
            }
        }
    }
    val micLauncher = androidx.activity.compose.rememberLauncherForActivityResult(
        androidx.activity.result.contract.ActivityResultContracts.RequestPermission()
    ) { granted ->
        if (granted) startRoomMeasure()
        else roomStatus = com.ivanna.omega.hardware.PermissionPolicy.micDeniedMessage(
            com.ivanna.omega.hardware.PermissionGate.stateFor(context, android.Manifest.permission.RECORD_AUDIO)
        )
    }

    LaunchedEffect(Unit) {
        if (npeBypassState && PiLstmBridge.isReady) {
            PiLstmBridge.setBypass(true)
        }
        VolterraSwitch.enabled = paramStore.isNpeManifoldEnabled()
    }

    // Refresco vivo del panel ENGINE (2 Hz) mientras CONTROL está en pantalla.
    LaunchedEffect(Unit) {
        kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.Default) {
            while (true) {
                runCatching { OmegaMetrics.refreshFromNative() }
                kotlinx.coroutines.delay(500)
            }
        }
    }

    val adaptiveTelemetryRaw by adaptiveBack.telemetry.collectAsState()
    val adaptiveTelemetry = adaptiveTelemetryRaw.toSnapshot()

    val antiDolbyController = remember(context) {
        AntiDolbyController(context).also { ctrl ->
            ctrl.onDspUpdate = { exciter, width, eqGainDb ->
                dsp.value = dsp.value.copy(wet = exciter, stereoWidth = width)
                dsp.value.pushToNative()
                if (IvannaNativeLib.isLoaded)
                    IvannaNativeLib.guardedNative(Unit) { IvannaNativeLib.nativeSetEQParams(eqGainDb, eqGainDb, eqGainDb, dsp.value.master) }
            }
        }
    }

    LaunchedEffect(antiDolbyController) {
        kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.Default) {
            antiDolbyController.initialize()
            if (paramStore.isAntiDolbyEnabled()) {
                antiDolbyController.enableAntiDolby()
            }
        }
    }

    IvannaControlPanel(
        initialExciter       = dsp.value.wet,
        initialEq            = dsp.value.mid,
        initialWidth         = dsp.value.stereoWidth,
        initialAntiDolby     = paramStore.isAntiDolbyEnabled(),
        initialPreset        = paramStore.getCurrentPreset(),
        initialCompThreshold = dsp.value.alpha,
        initialCompRatio     = dsp.value.beta,
        initialAutoMode      = paramStore.isAutoModeEnabled(),
        initialOmegaMode     = paramStore.getOmegaMode(),
        initialEvoEnabled    = paramStore.isEvoEnabled(),
        initialNpeBypass     = npeBypassState,
        initialNpeHrtf       = paramStore.getNpeHrtf(),
        initialNpeCochlear   = paramStore.getNpeCochlear(),
        initialNpeAdapt      = paramStore.getNpeAdapt(),
        initialNpeManifold   = paramStore.isNpeManifoldEnabled(),
        initialSpatialEnabled = IvannaSpatialEngine.enabled,
        initialHoaUpmixingEnabled = paramStore.isHoaUpmixingEnabled(),
        initialHoaImmersivity = paramStore.getHoaImmersivity(),
        initialWfsEnabled = paramStore.isWfsEnabled(),
        initialWfsSpread = paramStore.getWfsSpread(),
        initialAuxFieldEnabled = paramStore.isAuxFieldEnabled(),
        roomGains = roomGains,
        roomEnabled = roomEnabled,
        roomBusy = roomBusy,
        roomProgress = roomProgress,
        roomStatus = roomStatus,
        onRoomMeasure = {
            // El micrófono solo se pide aquí, cuando el usuario pulsa MEDIR. Si ya quedó denegado para
            // siempre (o se revocó en Ajustes) el sistema no vuelve a mostrar el diálogo: se explica y se
            // abre Ajustes en vez de repetir una petición muda.
            val micState = com.ivanna.omega.hardware.PermissionGate.stateFor(context, android.Manifest.permission.RECORD_AUDIO)
            when (micState) {
                com.ivanna.omega.hardware.PermissionState.GRANTED -> startRoomMeasure()
                com.ivanna.omega.hardware.PermissionState.OPEN_SETTINGS -> {
                    roomStatus = com.ivanna.omega.hardware.PermissionPolicy.micDeniedMessage(micState)
                    com.ivanna.omega.hardware.PermissionGate.openAppSettings(context)
                }
                else -> {
                    com.ivanna.omega.hardware.PermissionGate.markRequested(context, android.Manifest.permission.RECORD_AUDIO)
                    micLauncher.launch(android.Manifest.permission.RECORD_AUDIO)
                }
            }
        },
        onRoomToggle = { en ->
            roomEnabled = en && roomGains != null
            paramStore.setRoomCorrection(roomGains, roomEnabled)
            applyRoom(roomGains, roomEnabled)
        },
        onRoomClear = {
            roomGains = null; roomEnabled = false; roomStatus = ""
            paramStore.setRoomCorrection(null, false)
            applyRoom(null, false)
        },
        initialTemporalFieldEnabled = paramStore.isTemporalFieldEnabled(),
        initialTemporalFieldAmount = paramStore.getTemporalFieldAmount(),
        initialTemporalFieldAdaptive = paramStore.isTemporalFieldAdaptive(),
        initialBassHapticEnabled = paramStore.isBassHapticEnabled(),
        initialBassHapticStrength = paramStore.getBassHapticStrength(),
        initialBassHapticRumble = paramStore.getBassHapticRumble(),
        initialBassHapticTrimMs = paramStore.getBassHapticTrimMs(),
        bassHapticSupported = com.ivanna.omega.audio.BassHaptics.isSupported(context),
        initialAuxXoverHz = paramStore.getAuxXoverHz(),
        initialAuxCoherence = paramStore.getAuxCoherence(),
        initialPunchAmount = paramStore.getPunchAmount(),
        initialPhaseOracleIntensity = audioState.phaseOracleIntensity,
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
            // Fuera de modo manual el cambio se quedaba sólo en AudioState: el motor
            // adaptativo nunca se enteraba. Misma llamada que usa BRAIN → ADAPTATIVO.
            if (IvannaNativeLib.isLoaded)
                runCatching { IvannaNativeLib.nativeSetAdaptiveControls(mode.ordinal, AudioStateManager.audioState.value.adaptiveIntensity * 100f) }
            if (audioState.manualModeEnabled)
                adaptiveBack.applyManualState(AudioStateManager.audioState.value)
        },
        adaptiveIntensity = audioState.adaptiveIntensity * 100f,
        onAdaptiveIntensityChange = { percent ->
            AudioStateManager.updateState { it.copy(adaptiveIntensity = percent / 100f) }
            if (IvannaNativeLib.isLoaded)
                runCatching { IvannaNativeLib.nativeSetAdaptiveControls(AudioStateManager.audioState.value.adaptiveMode.ordinal, percent.coerceIn(0f, 100f)) }
            if (audioState.manualModeEnabled)
                adaptiveBack.applyManualState(AudioStateManager.audioState.value)
        },
        voiceProtectionEnabled  = voiceActive,
        onVoiceProtectionChange = { voiceMgr.toggle() },

        // ── Intelligent Upmixing (HOA → Binaural) ────────────────────────
        // Persiste en ParameterStore (misma fuente que PersistedStateRestorer
        // leía al arranque) + push inmediato al motor nativo vía JNI.
        onHoaUpmixingEnabledChange = { enabled ->
            paramStore.setHoaUpmixingEnabled(enabled)
            if (IvannaNativeLib.isLoaded)
                runCatching { IvannaNativeLib.nativeSetIntelligentUpmixingEnabled(enabled) }
            perceptualCommands.trySend("""{"action":"SET_PERCEPTUAL_STATE","upmixingEnabled":${if (enabled) 1.0f else 0.0f}}""")
        },
        onHoaImmersivityChange = { v ->
            paramStore.setHoaImmersivity(v)
            if (IvannaNativeLib.isLoaded)
                runCatching { IvannaNativeLib.nativeSetUpmixingImmersivity(v.coerceIn(0f, 2f)) }
            perceptualCommands.trySend("""{"action":"SET_PERCEPTUAL_STATE","upmixingImmersivity":${v.coerceIn(0f, 2f)}}""")
        },

        // ── Wave Field Synthesis — mismo patrón que Upmixing (persist + JNI + daemon),
        // con runCatching para no romper en builds sin el símbolo nativo ──
        onWfsEnabledChange = { enabled ->
            paramStore.setWfsEnabled(enabled)
            if (IvannaNativeLib.isLoaded)
                runCatching { IvannaNativeLib.nativeSetWfsEnabled(enabled) }
        },
        onPunchChange = { a ->
            paramStore.setPunchAmount(a)
            if (IvannaNativeLib.isLoaded)
                runCatching { IvannaNativeLib.nativeSetPunch(a.coerceIn(0f, 1f)) }
        },
        onAuxFieldChange = { en, xo, co ->
            paramStore.setAuxField(en, xo, co)
            if (IvannaNativeLib.isLoaded)
                runCatching { IvannaNativeLib.nativeSetAuxField(en, xo.coerceIn(60f, 200f), co.coerceIn(0f, 1f)) }
        },
        onTemporalFieldChange = { en, am, ad ->
            paramStore.setTemporalField(en, am, ad)
            if (IvannaNativeLib.isLoaded)
                runCatching { IvannaNativeLib.nativeSetTemporalField(en, am.coerceIn(0f, 1f), ad) }
        },
        onBassHapticChange = { en, st, ru, tr ->
            paramStore.setBassHaptic(en, st, ru, tr)
            com.ivanna.omega.audio.BassHaptics.configure(context, en, st, ru, tr.toInt())
        },
        onWfsSpreadChange = { v ->
            paramStore.setWfsSpread(v)
            if (IvannaNativeLib.isLoaded)
                runCatching { IvannaNativeLib.nativeSetWfsSpread(v.coerceIn(0f, 2f)) }
        },

        // ── Navegación ──────────────────────────────────────────────────
        // CAUSA RAIZ del boton "AUDIO HI-RES" que no hacia nada: la pantalla viva
        // es MainScaffold -> ControlTabScreen -> IvannaControlPanel, y aqui nunca
        // se pasaba onOpenHiRes (default `{}`). Los arreglos previos solo cablearon
        // el DashboardScreen legacy de MainActivity, que ya no es la pantalla real.
        // La ruta "hires" vive en el NavHost externo -> outerNav.
        onOpenHiRes                = { outerNav.navigate("hires") },
        onOpenVisualizer           = { outerNav.navigate("visualizer") },
        onOpenAdaptive             = onOpenAdaptiveTab,
        onOpenAdaptiveEngineManual = { outerNav.navigate("adaptive") },
        onOpenOpe                  = { outerNav.navigate(IvannaRoute.SOUND) { launchSingleTop = true } },
        onOpenBinaural             = { outerNav.navigate("spatial_audio") },
        onOpenTelemetry            = { outerNav.navigate(IvannaRoute.SYSTEM) { launchSingleTop = true } },
        onOpenAdaptiveProfiles     = { outerNav.navigate("adaptive_profiles") },
        onOpenProfiles             = { outerNav.navigate("profiles") },
        onOpenMagisk               = { outerNav.navigate("magisk") },
        onOpenSupremeAxesHub       = { outerNav.navigate(IvannaRoute.SUPREME_AXES_HUB) },
        modifier                   = modifier
    )
}
