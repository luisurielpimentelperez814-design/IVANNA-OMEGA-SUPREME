package com.ivanna.omega.ui

import android.util.Log
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.tween
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.geometry.RoundRect
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.audio.ActiveRoute
import com.ivanna.omega.audio.IvannaEffectProfile
import com.ivanna.omega.audio.OmegaMetrics
import com.ivanna.omega.audio.PipelineState
import com.ivanna.omega.core.IvannaNativeLib
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.withContext
import com.ivanna.omega.neuromorphic.IvannaNpeEngine
import com.ivanna.omega.ui.theme.*
import kotlin.math.log10
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalLifecycleOwner
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun IvannaControlPanel(
    initialExciter: Float,
    initialEq: Float,
    initialWidth: Float,
    initialAntiDolby: Boolean = true,
    initialPreset: String = "IVANNA OMEGA",
    initialAutoMode: Boolean = true,
    initialOmegaMode: Int = 2,
    initialCompThreshold: Float = 0.35f,
    initialCompRatio: Float = 0.08f,
    initialNhoHarmonic: Float = 0.28f,
    initialSpatialAngle: Float = 0.5f,
    initialSpatialWidth: Float = 0.68f,
    initialEvoEnabled: Boolean = true,
    initialNpeBypass: Boolean = false,
    initialNpeHarmonic: Float = 0.60f,
    initialNpeLateralInhib: Float = 0.50f,
    initialNpeOhcCompression: Float = 0.40f,
    initialNpeMasterGain: Float = 2.5f,
    initialNpeAgcTarget: Float = -14.0f,
    initialNpeAgcRate: Float = 0.62f,
    initialNpeHrtf: Boolean = true,
    initialNpeCochlear: Boolean = true,
    initialNpeAdapt: Boolean = true,
    initialNpeManifold: Boolean = true,
    initialSpatialEnabled: Boolean = true,
    initialHoaUpmixingEnabled: Boolean = true,
    initialHoaImmersivity: Float = 1.25f,
    initialWfsEnabled: Boolean = true,
    initialWfsSpread: Float = 1.18f,
    initialAuxFieldEnabled: Boolean = false,
    roomGains: FloatArray? = null,
    roomEnabled: Boolean = false,
    roomBusy: Boolean = false,
    roomProgress: Float = 0f,
    roomStatus: String = "",
    onRoomMeasure: () -> Unit = {},
    onRoomToggle: (Boolean) -> Unit = {},
    onRoomClear: () -> Unit = {},
    initialTemporalFieldEnabled: Boolean = false,
    initialTemporalFieldAmount: Float = 0.6f,
    initialTemporalFieldAdaptive: Boolean = true,
    initialBassHapticEnabled: Boolean = false,
    initialBassHapticStrength: Float = 0.7f,
    initialBassHapticRumble: Float = 0f,
    initialBassHapticTrimMs: Float = 0f,
    bassHapticSupported: Boolean = true,
    initialAuxXoverHz: Float = 120f,
    initialAuxCoherence: Float = 1f,
    initialPunchAmount: Float = 0f,
    onExciterChange: (Float) -> Unit,
    onEqChange: (Float) -> Unit,
    onWidthChange: (Float) -> Unit,
    onAntiDolbyChange: (Boolean) -> Unit = {},
    onPresetSelected: (String) -> Unit = {},
    onAutoModeChange: (Boolean) -> Unit = {},
    onOmegaModeChange: (Int) -> Unit = {},
    onCompThresholdChange: (Float) -> Unit = {},
    onCompRatioChange: (Float) -> Unit = {},
    onNhoHarmonicChange: (Float) -> Unit = {},
    onSpatialAngleChange: (Float) -> Unit = {},
    onSpatialWidthChange: (Float) -> Unit = {},
    onEvoEnabledChange: (Boolean) -> Unit = {},
    onNpeBypassChange: (Boolean) -> Unit = {},
    onNpeHarmonicChange: (Float) -> Unit = {},
    onNpeLateralInhibChange: (Float) -> Unit = {},
    onNpeOhcCompressionChange: (Float) -> Unit = {},
    onNpeMasterGainChange: (Float) -> Unit = {},
    onNpeAgcChange: (Float, Float) -> Unit = { _, _ -> },
    onNpeFlagsChange: (Boolean, Boolean, Boolean) -> Unit = { _, _, _ -> },
    onNpeManifoldChange: (Boolean) -> Unit = {},
    onSpatialEnabledChange: (Boolean) -> Unit = {},
    onHoaUpmixingEnabledChange: (Boolean) -> Unit = {},
    onHoaImmersivityChange: (Float) -> Unit = {},
    onWfsEnabledChange: (Boolean) -> Unit = {},
    onWfsSpreadChange: (Float) -> Unit = {},
    onAuxFieldChange: (Boolean, Float, Float) -> Unit = { _, _, _ -> },
    onPunchChange: (Float) -> Unit = {},
    onTemporalFieldChange: (Boolean, Float, Boolean) -> Unit = { _, _, _ -> },
    onBassHapticChange: (Boolean, Float, Float, Float) -> Unit = { _, _, _, _ -> },
    onOpenVisualizer: () -> Unit = {},
    onOpenHiRes: () -> Unit = {},
    onOpenAdaptive: () -> Unit = {},
    onOpenAdaptiveEngineManual: () -> Unit = {},
    onOpenOpe: () -> Unit = {},
    onOpenBinaural: () -> Unit = {},
    onOpenTelemetry: () -> Unit = {},
    onOpenAdaptiveProfiles: () -> Unit = {},
    onOpenProfiles: () -> Unit = {},
    onOpenMagisk: () -> Unit = {},
    onOpenSupremeAxesHub: () -> Unit = {},
    metrics: OmegaMetrics = OmegaMetrics(),
    onMetricsUpdate: ((OmegaMetrics) -> Unit)? = null,
    adaptiveTelemetry: com.ivanna.omega.ui.AdaptiveTelemetrySnapshot = com.ivanna.omega.ui.AdaptiveTelemetrySnapshot(),
    adaptiveMode: com.ivanna.omega.audio.AdaptiveMode = com.ivanna.omega.audio.AdaptiveMode.STUDIO,
    onAdaptiveModeChange: (com.ivanna.omega.audio.AdaptiveMode) -> Unit = {},
    adaptiveIntensity: Float = 82f,
    onAdaptiveIntensityChange: (Float) -> Unit = {},
    voiceProtectionEnabled: Boolean = true,
    onVoiceProtectionChange: (Boolean) -> Unit = {},
    routeState: PipelineState = PipelineState(),
    // Phase Oracle — intensidad global de coherencia de fase (0=off, 1=max)
    initialPhaseOracleIntensity: Float = 0.72f,
    onPhaseOracleChange: (Float) -> Unit = {},
    modifier: Modifier = Modifier
) {
    // FIX (build): 'context' y 'savedState' estaban duplicados — una copia
    // quedó pegada DENTRO del lambda por defecto de onAntiDolbyChange (línea
    // 87-90), donde LocalContext.current/remember son ilegales por no ser
    // contexto @Composable. Se elimina esa copia y se deja una sola aquí.
    // Además phaseOracleIntensity se declaraba ANTES que savedState
    // (Unresolved reference: savedState) — ahora va después.
    val context = LocalContext.current
    val savedState = remember { AdaptiveControlsPrefs.load(context) }
    // Socket al daemon fuera del hilo UI; en un slider rápido solo interesa
    // el último valor, no una cola de cientos de comandos intermedios.
    val daemonCommands = remember {
        kotlinx.coroutines.channels.Channel<String>(kotlinx.coroutines.channels.Channel.CONFLATED)
    }
    LaunchedEffect(daemonCommands) {
        for (command in daemonCommands) {
            kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.IO) {
                runCatching { com.ivanna.omega.magisk.MagiskBridge.sendCommand(command) }
            }
        }
    }

    var phaseOracleIntensity by remember { mutableFloatStateOf(savedState.phaseOracleIntensity) }
    var antiDolbyThreshold by remember { mutableFloatStateOf(savedState.antiDolbyThreshold) }
    var spatialSuppression by remember { mutableFloatStateOf(savedState.spatialSuppression) }
    var spscRingFactor by remember {
        mutableFloatStateOf(
            if (IvannaNativeLib.isLoaded)
                runCatching { IvannaNativeLib.nativeGetSpscRingFactor() }.getOrDefault(savedState.spscRingFactor)
            else savedState.spscRingFactor
        )
    }
    var tinymlInferenceGain by remember { mutableFloatStateOf(savedState.tinymlInferenceGain) }
    var volterraEnabled by remember {
        mutableStateOf(
            if (IvannaNativeLib.isLoaded) runCatching { IvannaNativeLib.nativeIsVolterraEnabled() }.getOrDefault(true) else true
        )
    }
    var fastRpcEnabled by remember {
        mutableStateOf(
            if (IvannaNativeLib.isLoaded) runCatching { IvannaNativeLib.nativeIsFastRpcEnabled() }.getOrDefault(false) else false
        )
    }
    var atiEnabled by remember {
        mutableStateOf(
            if (IvannaNativeLib.isLoaded) runCatching { IvannaNativeLib.nativeIsAtiEnabled() }.getOrDefault(true) else true
        )
    }
    // v2.5.15: Acoustic Unity Engine — interruptor persistente (su propio SharedPreferences) que se
    // reaplica al nativo al componer, y telemetría MEDIDA (original vs salida) refrescada cada 500 ms.
    val unityPrefs = remember { context.getSharedPreferences("ivanna_unity", android.content.Context.MODE_PRIVATE) }
    var unityEnabled by remember { mutableStateOf(unityPrefs.getBoolean("enabled", true)) }
    var unityTelemetry by remember { mutableStateOf(FloatArray(0)) }
    var punchAmount by remember { mutableFloatStateOf(initialPunchAmount) }
    var punchTelemetry by remember { mutableStateOf(FloatArray(0)) }
    LaunchedEffect(Unit) {
        kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.Default) {
        if (IvannaNativeLib.isLoaded) {
            runCatching { IvannaNativeLib.nativeSetUnityEnabled(unityEnabled) }
            // La UI muestra el estado REAL del motor nativo, no el supuesto.
            unityEnabled = runCatching { IvannaNativeLib.nativeIsUnityEnabled() }.getOrDefault(unityEnabled)
        }
        while (isActive) {
            if (IvannaNativeLib.isLoaded) {
                unityTelemetry = runCatching { IvannaNativeLib.nativeGetUnityTelemetry() }.getOrDefault(FloatArray(0))
                punchTelemetry = runCatching { IvannaNativeLib.nativeGetPunchTelemetry() }.getOrDefault(FloatArray(0))
            }
            delay(500)
        }
    
        }
    }

    // Persistencia inmediata en cada cambio para todos los controles del panel.
    // FIX: phaseOracleIntensity y omegaMode sólo se guardaban en DisposableEffect
    // ON_STOP — si la app crasheaba o era matada por el sistema, el valor se perdía.
    // FIX (evo kernel descableado + sin persistencia, 2026-09-07): el toggle
    // KERNEL EVOLUTIVO persistía en AdaptiveControlsPrefs y cambiaba el hilo
    // nativo en caliente (onEvoEnabledChange → nativeStart/StopEvoThread),
    // pero al reabrir la app el panel restauraba initialEvoEnabled SIN
    // reaplicarlo al nativo — si el usuario lo apagó y reinició, el hilo
    // corría (init llama start_evo_thread) mientras la UI mostraba OFF, y
    // el estado persistido nunca llegaba al motor. Se reaplica una vez al
    // componer: OFF persistido → stop explícito; ON → no-op (ya corre).
    LaunchedEffect(Unit) {
        if (!initialEvoEnabled && IvannaNativeLib.isLoaded) {
            runCatching { IvannaNativeLib.guardedNative(Unit) { IvannaNativeLib.nativeStopEvoThread() } }
        }
    }

    LaunchedEffect(antiDolbyThreshold, spatialSuppression, spscRingFactor, tinymlInferenceGain) {
        val cur = AdaptiveControlsPrefs.load(context)
        AdaptiveControlsPrefs.save(context, cur.copy(
            antiDolbyThreshold = antiDolbyThreshold,
            spatialSuppression = spatialSuppression,
            spscRingFactor = spscRingFactor,
            tinymlInferenceGain = tinymlInferenceGain
        ))
    }
    LaunchedEffect(phaseOracleIntensity) {
        val cur = AdaptiveControlsPrefs.load(context)
        AdaptiveControlsPrefs.save(context, cur.copy(phaseOracleIntensity = phaseOracleIntensity))
    }
    var exciter by remember { mutableFloatStateOf(initialExciter) }
    var eq by remember { mutableFloatStateOf(initialEq) }
    var width by remember { mutableFloatStateOf(initialWidth) }

    var antiDolbyEnabled by remember { mutableStateOf(initialAntiDolby) }
    var selectedPreset by remember { mutableStateOf(initialPreset) }
    var autoMode by remember { mutableStateOf(initialAutoMode) }
    var omegaMode by remember { mutableIntStateOf(savedState.omegaMode) }
    var compThreshold by remember { mutableFloatStateOf(initialCompThreshold) }
    var compRatio by remember { mutableFloatStateOf(initialCompRatio) }
    var nhoHarmonic by remember { mutableFloatStateOf(savedState.nhoHarmonic) }
    var spatialAngle by remember { mutableFloatStateOf(savedState.spatialAngle) }
    var spatialWidth by remember { mutableFloatStateOf(savedState.spatialWidth) }
    var evoEnabled by remember { mutableStateOf(initialEvoEnabled) }
    var evoFitness by remember { mutableFloatStateOf(0f) }
    var evoGeneration by remember { mutableIntStateOf(0) }
    var npeBypass by remember { mutableStateOf(initialNpeBypass) }
    var npeHarmonic by remember { mutableFloatStateOf(savedState.npeHarmonic) }
    var npeLateralInhib by remember { mutableFloatStateOf(savedState.npeLateralInhib) }
    var npeOhcCompression by remember { mutableFloatStateOf(savedState.npeOhcCompression) }
    var npeMasterGain by remember { mutableFloatStateOf(savedState.npeMasterGain) }
    var npeAgcTarget by remember { mutableFloatStateOf(savedState.npeAgcTarget) }
    var npeAgcRate by remember { mutableFloatStateOf(savedState.npeAgcRate) }
    // FIX (build): estos 3 LaunchedEffect estaban ANTES de las declaraciones
    // de omegaMode/nhoHarmonic/spatialAngle/spatialWidth/npeHarmonic/etc.
    // (usados antes de declararse -> "Unresolved reference" en compileDebugKotlin,
    // build del APK roto). Se mueven aqui, justo despues de que todas las
    // variables que referencian ya existen.
    LaunchedEffect(omegaMode) {
        val cur = AdaptiveControlsPrefs.load(context)
        AdaptiveControlsPrefs.save(context, cur.copy(omegaMode = omegaMode))
    }
    LaunchedEffect(nhoHarmonic, spatialAngle, spatialWidth) {
        val cur = AdaptiveControlsPrefs.load(context)
        AdaptiveControlsPrefs.save(context, cur.copy(
            nhoHarmonic  = nhoHarmonic,
            spatialAngle = spatialAngle,
            spatialWidth = spatialWidth
        ))
    }
    LaunchedEffect(npeHarmonic, npeLateralInhib, npeOhcCompression, npeMasterGain, npeAgcTarget, npeAgcRate) {
        val cur = AdaptiveControlsPrefs.load(context)
        AdaptiveControlsPrefs.save(context, cur.copy(
            npeHarmonic       = npeHarmonic,
            npeLateralInhib   = npeLateralInhib,
            npeOhcCompression = npeOhcCompression,
            npeMasterGain     = npeMasterGain,
            npeAgcTarget      = npeAgcTarget,
            npeAgcRate        = npeAgcRate
        ))
    }
    var npeHrtf by remember { mutableStateOf(initialNpeHrtf) }
    var npeCochlear by remember { mutableStateOf(initialNpeCochlear) }
    var npeAdapt by remember { mutableStateOf(initialNpeAdapt) }
    var npeManifold by remember { mutableStateOf(initialNpeManifold) }
    var npeGenre by remember { mutableStateOf("\u2014") }
    var npeRmsDb by remember { mutableFloatStateOf(-60f) }
    var npeAgcGainDb by remember { mutableFloatStateOf(0f) }
    var npeClassifyConfidence by remember { mutableFloatStateOf(0f) }
    var npeClassifyThd by remember { mutableFloatStateOf(0f) }
    var npeInferenceUs by remember { mutableLongStateOf(-1L) }
    var lastAutoAppliedGenre by remember { mutableStateOf<String?>(null) }
    var spatialEnabled by remember { mutableStateOf(initialSpatialEnabled) }
    var hoaUpmixingEnabled by remember { mutableStateOf(initialHoaUpmixingEnabled) }
    var hoaImmersivity by remember { mutableFloatStateOf(initialHoaImmersivity) }
    var wfsEnabled by remember { mutableStateOf(initialWfsEnabled) }
    var wfsSpread by remember { mutableFloatStateOf(initialWfsSpread) }
    var auxEnabled by remember { mutableStateOf(initialAuxFieldEnabled) }
    var temporalEnabled by remember { mutableStateOf(initialTemporalFieldEnabled) }
    var temporalAmount by remember { mutableStateOf(initialTemporalFieldAmount) }
    var temporalAdaptive by remember { mutableStateOf(initialTemporalFieldAdaptive) }
    var hapticEnabled by remember { mutableStateOf(initialBassHapticEnabled && bassHapticSupported) }
    var hapticStrength by remember { mutableStateOf(initialBassHapticStrength) }
    var hapticRumble by remember { mutableStateOf(initialBassHapticRumble) }
    var hapticTrim by remember { mutableStateOf(initialBassHapticTrimMs) }
    var hapticSyncText by remember { mutableStateOf(com.ivanna.omega.audio.BassHaptics.describeSync()) }
    LaunchedEffect(hapticEnabled) {
        while (hapticEnabled) {
            hapticSyncText = com.ivanna.omega.audio.BassHaptics.describeSync()
            kotlinx.coroutines.delay(1000L)
        }
    }
    var auxXover by remember { mutableFloatStateOf(initialAuxXoverHz) }
    var auxCoherence by remember { mutableFloatStateOf(initialAuxCoherence) }

    
    // ── Persistencia automática al salir de la pantalla ──
    // FIX (build): LocalLifecycleOwner.current se leía DENTRO del cuerpo de
    // DisposableEffect y del onDispose — ninguno es contexto @Composable.
    // Se captura una sola vez aquí (sí composable) y se usa el lifecycle
    // capturado dentro del efecto; así addObserver/removeObserver operan
    // sobre el MISMO lifecycle (antes podían resolver a owners distintos).
    val lifecycleOwner = LocalLifecycleOwner.current
    DisposableEffect(lifecycleOwner) {
        val observer = LifecycleEventObserver { _, event ->
            if (event == Lifecycle.Event.ON_STOP) {
                val current = AdaptiveControlsPrefs.load(context)
                AdaptiveControlsPrefs.save(context, current.copy(
                    antiDolbyEnabled = antiDolbyEnabled,
                    selectedPreset = selectedPreset,
                    autoMode = autoMode,
                    omegaMode = omegaMode,
                    nhoHarmonic = nhoHarmonic,
                    spatialAngle = spatialAngle,
                    spatialWidth = spatialWidth,
                    evoEnabled = evoEnabled,
                    npeBypass = npeBypass,
                    npeHarmonic = npeHarmonic,
                    npeLateralInhib = npeLateralInhib,
                    npeOhcCompression = npeOhcCompression,
                    npeMasterGain = npeMasterGain,
                    npeAgcTarget = npeAgcTarget,
                    npeAgcRate = npeAgcRate,
                    npeHrtf = npeHrtf,
                    npeCochlear = npeCochlear,
                    npeAdapt = npeAdapt,
                    npeManifold = npeManifold,
                    spatialEnabled = spatialEnabled,
                    phaseOracleIntensity = phaseOracleIntensity
                ))
            }
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        onDispose { lifecycleOwner.lifecycle.removeObserver(observer) }
    }

    val rmsHistory = remember { mutableStateListOf<Float>().apply { repeat(32) { add(-60f) } } }

    LaunchedEffect(Unit) {
        kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.Default) {
        kotlinx.coroutines.delay(800)
        while (true) {
            // FIX (HUD "en vivo" mostraba datos congelados de la última
            // sesión de captura, para siempre, incluso con MediaProjection
            // ya terminado): sin este guard, npeGenre/npeRmsDb/etc. nunca se
            // reseteaban al morir la captura — el título dice "tiempo real"
            // pero el estado era "último valor conocido, sin caducidad".
            // FIX (HUD muerto fuera de captura): la telemetría sólo se daba por viva con
            // MediaProjection activo; con el motor NPE/nativo procesando por otra vía
            // (módulo Magisk, bridge) quedaba clavada en -60 dB / "—". Ahora se considera
            // viva si hay captura, inferencia NPE reciente o nivel en el bus compartido.
            val sharedRmsLin = com.ivanna.omega.audio.OmegaMetrics.shared.value.rmsLevel
            val npeLive = IvannaNpeEngine.isReady && IvannaNpeEngine.lastInferenceUs >= 0L
            if (!com.ivanna.omega.audio.PlaybackCaptureService.isCapturing.value &&
                !npeLive && sharedRmsLin <= 1e-6f) {
                npeGenre = "\u2014"
                npeRmsDb = -60f
                npeAgcGainDb = 0f
                npeClassifyConfidence = 0f
                npeClassifyThd = 0f
                npeInferenceUs = -1L
                rmsHistory.removeAt(0)
                rmsHistory.add(-60f)
                kotlinx.coroutines.delay(750)
                continue
            }
            npeGenre = if (IvannaNpeEngine.isReady) IvannaNpeEngine.getDetectedGenre() else "\u2014"
            val m = if (IvannaNpeEngine.isReady) IvannaNpeEngine.getMetrics() else FloatArray(0)
            val rmsLin = m.getOrElse(1) { 0f }.takeIf { it > 1e-6f } ?: sharedRmsLin
            npeRmsDb = if (rmsLin > 1e-6f) (20f * log10(rmsLin)) else -60f
            val agcLin = m.getOrElse(2) { 1f }
            npeAgcGainDb = if (agcLin > 1e-6f) (20f * log10(agcLin)) else 0f
            val c = if (IvannaNpeEngine.isReady) IvannaNpeEngine.getSynthClassify() else FloatArray(0)
            npeClassifyConfidence = c.getOrElse(1) { 0f }
            npeClassifyThd = c.getOrElse(2) { 0f }
            npeInferenceUs = IvannaNpeEngine.lastInferenceUs
            rmsHistory.removeAt(0)
            rmsHistory.add(npeRmsDb)

            // FIX ("presets automáticos" nunca existía pese a que autoMode ya
            // prometía "Auto IA seleccionando" en el subtítulo — solo subía
            // un multiplicador visual (omniLevel) y bloqueaba los controles
            // manuales; ningún código seleccionaba realmente un preset).
            // Mapeo honesto a los 8 géneros reales que autonomous_brain.hpp
            // puede devolver (ratios espectrales medidos, no adivinados),
            // sobre el catálogo de 14 presets ya construido — con debounce
            // para no recargar el mismo preset en cada bloque de 750ms.
            if (autoMode && npeGenre != lastAutoAppliedGenre && npeGenre != "\u2014") {
                val profile = when (npeGenre) {
                    "Hip-Hop / EDM"    -> com.ivanna.omega.audio.IvannaEffectProfile.PUNCH
                    "Pop"              -> com.ivanna.omega.audio.IvannaEffectProfile.IVANNA_OMEGA
                    "Reggaet\u00f3n"   -> com.ivanna.omega.audio.IvannaEffectProfile.PUNCH
                    "Rock / Metal"     -> com.ivanna.omega.audio.IvannaEffectProfile.ROCK_70S
                    "Jazz / Cl\u00e1sica" -> com.ivanna.omega.audio.IvannaEffectProfile.STUDIO_PRO
                    "Ac\u00fastica"    -> com.ivanna.omega.audio.IvannaEffectProfile.ABBEY_ROAD
                    "Electr\u00f3nica" -> com.ivanna.omega.audio.IvannaEffectProfile.SPATIAL
                    else               -> com.ivanna.omega.audio.IvannaEffectProfile.WARM  // "Mixto"
                }
                runCatching {
                    com.ivanna.omega.dsp.DSPState.globalEffectManager?.applyProfile(profile)
                }
                lastAutoAppliedGenre = npeGenre
            }

            kotlinx.coroutines.delay(750)
        }
    
        }
    }

    LaunchedEffect(Unit) {
        kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.Default) {
        kotlinx.coroutines.delay(1200)
        while (true) {
            try {
                // FIX (kernel evolutivo "no jalaba"): nativeGetBestFitness()
                // solo tenía implementación nativa en evolutionary_kernel.cpp
                // (v1) — archivo excluido del CMakeLists activo desde hace
                // tiempo. El símbolo JNI no existe en el .so real: esta
                // llamada lanzaba UnsatisfiedLinkError en CADA tick, atrapado
                // aquí y logueado como "no disponible todavía" para siempre.
                // El kernel v2 (evolutionary_kernel_v2.cpp, el que SÍ compila
                // y SÍ evoluciona en background via pd_engine.hpp) expone su
                // fitness real por nativeGetEvoBestFitness() — ya usado
                // correctamente más abajo en este mismo archivo (línea ~820).
                evoFitness = IvannaNativeLib.nativeGetEvoBestFitness()
                evoGeneration = IvannaNativeLib.nativeGetGeneration()
            } catch (e: Throwable) {
                Log.w("IvannaControlPanel", "Kernel evolutivo no disponible todavía", e)
            }
            kotlinx.coroutines.delay(2000)
        }
    
        }
    }

    val omniLevel by remember(omegaMode, npeBypass, spatialEnabled, evoEnabled, antiDolbyEnabled, autoMode) {
        mutableFloatStateOf(
            listOf(
                if (omegaMode > 0) 1f else 0.3f,
                if (!npeBypass) 1f else 0.15f,
                if (spatialEnabled) 1f else 0.2f,
                if (evoEnabled) 1f else 0.2f,
                if (antiDolbyEnabled) 1f else 0.4f,
                if (autoMode) 1f else 0.6f
            ).average().toFloat()
        )
    }

    Column(
        modifier = modifier
            .fillMaxSize()
            .background(
                Brush.radialGradient(
                    colors = listOf(
                        ObsidianSoft.copy(alpha = 0.65f),
                        ObsidianVoid.copy(alpha = 0.92f)
                    ),
                    radius = 1400f
                )
            )
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 16.dp, vertical = 20.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        RouteModeBanner()
        OmniHeroHeader(
            omegaMode = omegaMode,
            npeActive = !npeBypass,
            spatialActive = spatialEnabled,
            autoMode = autoMode,
            omniLevel = omniLevel,
            routeState = routeState
        )

        EngineStatusCard(metrics = metrics)

        LiveTelemetryHud(
            rmsDb = npeRmsDb,
            rmsHistory = rmsHistory,
            agcDb = npeAgcGainDb,
            genre = npeGenre,
            confidence = npeClassifyConfidence,
            thd = npeClassifyThd,
            evoFitness = evoFitness,
            evoGeneration = evoGeneration
        )

        // ── NAEL / ISO 226:2023 — Equal Loudness Compensation ─────────
        SectionLabel("NAEL · EQUAL LOUDNESS ISO 226:2023", AuroraCyan)
        NaelCard()

        SectionLabel("ANTI-DOLBY TinyML & SPSC LOCK-FREE KERNEL", AuroraCyan)
        GlassCard(
            title = "MOTOR ANTI-DOLBY SUPREME (TinyML ConvNeXt)",
            accent = AuroraCyan,
            subtitle = "Motor de Inferencia ConvNeXt INT8 & Ring Buffer SPSC sin Bloqueo"
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                StatBlock("MODELO IA", "ConvNeXt-v3", AuroraCyan, Modifier.weight(1f))
                StatBlock(
                    "INFERENCIA",
                    if (npeInferenceUs >= 0L) "${npeInferenceUs} µs" else "— µs",
                    PhosphorGreen, Modifier.weight(1f)
                )
                StatBlock(
                    "LATENCIA DSP",
                    if (npeInferenceUs >= 0L) "<%.2f ms".format(npeInferenceUs / 1000f) else "— ms",
                    PhosphorGreen, Modifier.weight(1f)
                )
            }
            Spacer(modifier = Modifier.height(10.dp))
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text("ESTADO TINYML KERNEL", fontSize = 11.sp, fontWeight = FontWeight.Bold, color = Color.White)
                Surface(
                    color = PhosphorGreen.copy(alpha = 0.18f),
                    shape = RoundedCornerShape(12.dp),
                    border = BorderStroke(1.dp, PhosphorGreen)
                ) {
                    Text(
                        "LOCK-FREE SPSC ACTIVE",
                        fontSize = 10.sp,
                        fontWeight = FontWeight.Bold,
                        color = PhosphorGreen,
                        modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp)
                    )
                }
            }
            Spacer(modifier = Modifier.height(10.dp))
            AuroraSlider("CANCELACIÓN OBJETOS ATMOS", antiDolbyThreshold, 0f..1f, unit = "×") {
                antiDolbyThreshold = it
                if (IvannaNativeLib.isLoaded) {
                    // La intensidad real = umbral × ganancia de inferencia
                    runCatching { IvannaNativeLib.nativeSetAntiDolbyIntensity(it * tinymlInferenceGain.coerceIn(0f, 2f)) }
                }
            }
            // FIX: spatialSuppression no tenía llamada nativa — solo guardaba en prefs.
            // Wired → nativeSetSpatialWet (controla el wet de la convolución espacial
            // del motor antiDolby: cuánta "supresión de coherencia falsa" pasa al DAC).
            AuroraSlider("SUPRESIÓN COHERENCIA FALSA", spatialSuppression, 0f..1f, unit = "%") {
                spatialSuppression = it
                if (IvannaNativeLib.isLoaded) {
                    runCatching { IvannaNativeLib.nativeSetSpatialWet(it) }
                }
            }
            // FIX (build + semantica): nativeSetAdaptiveControls(modeOrdinal, intensity)
            // Conexión nativa completa: ajusta factor del buffer circular SPSC
            // lock-free en C++ (ivanna_omega_jni) y propaga al daemon Magisk.
            AuroraSlider("FACTOR ANCHO RING BUFFER SPSC", spscRingFactor, 0.5f..1.5f, unit = "x") {
                spscRingFactor = it
                AdaptiveControlsPrefs.save(context, AdaptiveControlsPrefs.load(context).copy(spscRingFactor = it))
                if (IvannaNativeLib.isLoaded) {
                    runCatching { IvannaNativeLib.nativeSetSpscRingFactor(it) }
                }
                daemonCommands.trySend("""{"action":"SET_SPSC_RING_FACTOR","factor":$it}""")
            }
            // FIX: tinymlInferenceGain no tenía llamada nativa. Ahora escala
            // la intensidad antiDolby activa: umbral × gananciaInferencia.
            AuroraSlider("GANANCIA INFERENCIA TINYML", tinymlInferenceGain, 0f..2f, unit = "dB") {
                tinymlInferenceGain = it
                if (IvannaNativeLib.isLoaded) {
                    runCatching {
                        IvannaNativeLib.nativeSetAntiDolbyIntensity(antiDolbyThreshold * it.coerceIn(0f, 2f))
                    }
                }
            }
            Spacer(modifier = Modifier.height(8.dp))
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                FlagToggle("VOLTERRA H2", volterraEnabled, AuroraCyan, Modifier.weight(1f)) { en ->
                    volterraEnabled = en
                    if (IvannaNativeLib.isLoaded) runCatching { IvannaNativeLib.nativeSetVolterraEnabled(en) }
                }
                FlagToggle("FASTRPC cDSP", fastRpcEnabled, PhosphorGreen, Modifier.weight(1f)) { en ->
                    fastRpcEnabled = en
                    if (IvannaNativeLib.isLoaded) runCatching { IvannaNativeLib.nativeSetFastRpcEnabled(en) }
                    if (en) com.ivanna.omega.neuromorphic.IvannaDspManager.enable()
                    else com.ivanna.omega.neuromorphic.IvannaDspManager.disable()
                }
                FlagToggle("ATI GUARD", atiEnabled, NeonMagenta, Modifier.weight(1f)) { en ->
                    atiEnabled = en
                    if (IvannaNativeLib.isLoaded) runCatching { IvannaNativeLib.nativeSetAtiEnabled(en) }
                }
            }
            Spacer(modifier = Modifier.height(8.dp))
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                FlagToggle("UNITY MEDIDO", unityEnabled, PhosphorGreen, Modifier.weight(1f)) { en ->
                    unityEnabled = en
                    unityPrefs.edit().putBoolean("enabled", en).apply()
                    if (IvannaNativeLib.isLoaded) runCatching { IvannaNativeLib.nativeSetUnityEnabled(en) }
                }
                val ut = unityTelemetry
                Text(
                    when {
                        ut.size < 24 -> "Sin medición (motor nativo no activo)"
                        ut[15] < 1f  -> "Calibrando medición…"
                        else ->
                            "corr ${"%.2f".format(ut[0])}→${"%.2f".format(ut[1])}  cresta ${"%+.1f".format(-ut[6])} dB  agudos ${"%+.1f".format(ut[3])} dB\n" +
                            "presupuestos  espacio ${"%.0f".format(ut[11] * 100f)}%  sala ${"%.0f".format(ut[12] * 100f)}%  " +
                            "armónicos ${"%.0f".format(ut[13] * 100f)}%  dinámica ${"%.0f".format(ut[14] * 100f)}%"
                    },
                    style = MaterialTheme.typography.labelSmall,
                    color = TextMuted,
                    modifier = Modifier.weight(2f)
                )
            }
        }

        // ── Eje Supremo: Inversión Biomecánica Coclear (PINN) ──────────────
        SectionLabel("INVERSIÓN BIOMECÁNICA COCLEAR (PINN)", AuroraCyan)
        CochlearInverseCard()

        SectionLabel("ADAPTIVE ENGINE", AuroraCyan)

        com.ivanna.omega.ui.AdaptiveEngineStatusCard(telemetry = adaptiveTelemetry)

        com.ivanna.omega.ui.AdaptiveControlsCard(
            mode = adaptiveMode,
            onModeChange = onAdaptiveModeChange,
            intensity = adaptiveIntensity,
            onIntensityChange = onAdaptiveIntensityChange,
            spatialControlPercent = spatialWidth * 100f,
            onSpatialControlChange = { percent ->
                spatialWidth = percent / 100f
                onSpatialWidthChange(percent / 100f)
            },
            voiceProtectionEnabled = voiceProtectionEnabled,
            onVoiceProtectionChange = onVoiceProtectionChange
        )

        MasterBar(
            antiDolbyEnabled = antiDolbyEnabled,
            onAntiDolbyChange = { enabled -> antiDolbyEnabled = enabled; onAntiDolbyChange(enabled) },
            autoMode = autoMode,
            onAutoModeChange = { enabled -> autoMode = enabled; onAutoModeChange(enabled) },
            onOpenVisualizer = onOpenVisualizer,
            onOpenAdaptive = onOpenAdaptive,
            onOpenAdaptiveEngineManual = onOpenAdaptiveEngineManual,
            onOpenProfiles = onOpenProfiles,
            onOpenMagisk = onOpenMagisk
        )

        SectionLabel("CADENA DSP", AuroraCyan)

        GlassCard(
            title = "MOTOR OPE",
            accent = AuroraCyan,
            subtitle = when (omegaMode) {
                1 -> "DSP + NHO · saturación armónica no lineal"
                2 -> "DSP + NHO + Spatial · ITD/ILD, imagen estéreo"
                3 -> "DSP + NHO + HRTF · convolución binaural real (audífonos)"
                else -> "Solo DSP · EQ / Comp / Exciter / Widener"
            }
        ) {
            SingleChoiceSegmentedButtonRow(modifier = Modifier.fillMaxWidth()) {
                listOf("DSP", "+NHO", "+Spatial", "+HRTF").forEachIndexed { idx, label ->
                    SegmentedButton(
                        selected = omegaMode == idx,
                        onClick = { omegaMode = idx; onOmegaModeChange(idx) },
                        shape = SegmentedButtonDefaults.itemShape(idx, 4),
                        colors = SegmentedButtonDefaults.colors(
                            activeContainerColor = AuroraCyan.copy(alpha = 0.20f),
                            activeContentColor = AuroraCyan,
                            activeBorderColor = AuroraCyan,
                            inactiveContainerColor = Color.Transparent,
                            inactiveContentColor = TextSecondary,
                            inactiveBorderColor = ObsidianEdge
                        )
                    ) { Text(label, fontWeight = FontWeight.SemiBold) }
                }
            }
            Spacer(modifier = Modifier.height(10.dp))
            Row(
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                modifier = Modifier.fillMaxWidth()
            ) {
                OutlinedButton(
                    onClick = onOpenOpe,
                    modifier = Modifier.weight(1f),
                    colors = ButtonDefaults.outlinedButtonColors(contentColor = AuroraCyan)
                ) { Text("EQ / COMP", fontSize = 11.sp) }
                OutlinedButton(
                    onClick = onOpenBinaural,
                    modifier = Modifier.weight(1f),
                    colors = ButtonDefaults.outlinedButtonColors(contentColor = AuroraCyan)
                ) { Text("BINAURAL", fontSize = 11.sp) }
                OutlinedButton(
                    onClick = onOpenTelemetry,
                    modifier = Modifier.weight(1f),
                    colors = ButtonDefaults.outlinedButtonColors(contentColor = AuroraCyan)
                ) { Text("TELEMETRÍA", fontSize = 11.sp) }
                OutlinedButton(
                    onClick = onOpenAdaptiveProfiles,
                    modifier = Modifier.weight(1f),
                    colors = ButtonDefaults.outlinedButtonColors(contentColor = NeonMagenta)
                ) { Text("PERFILES", fontSize = 11.sp) }
            }
            Spacer(modifier = Modifier.height(8.dp))
            Row(
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                modifier = Modifier.fillMaxWidth()
            ) {
                // FIX (pantalla inalcanzable, 2026-09-19): HiResAudioScreen
                // (selector real de sample rate 16..384 kHz y profundidad
                // 16/24/32 bit, HiResAudioManager) existía con la ruta
                // "hires" registrada en el NavHost, pero ningún botón en
                // toda la app navegaba ahí — el propietario tenía razón,
                // no era una alucinación.
                OutlinedButton(
                    onClick = onOpenHiRes,
                    modifier = Modifier.weight(1f),
                    colors = ButtonDefaults.outlinedButtonColors(contentColor = AuroraCyan)
                ) { Text("AUDIO HI-RES", fontSize = 11.sp) }
            }
        }

        GlassCard(
            title = "PRESETS DE SONIDO",
            accent = NeonMagenta,
            subtitle = if (autoMode) "Auto IA seleccionando · manual bloqueado"
                        else "Selección manual · IvannaEffectProfile"
        ) {
            LazyRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                items(IvannaEffectProfile.byName.keys.toList()) { name ->
                    FilterChip(
                        selected = selectedPreset == name,
                        enabled = !autoMode,
                        onClick = { selectedPreset = name; onPresetSelected(name) },
                        label = { Text(name, fontWeight = FontWeight.Medium) },
                        colors = FilterChipDefaults.filterChipColors(
                            containerColor = Color.Transparent,
                            labelColor = TextSecondary,
                            selectedContainerColor = NeonMagenta.copy(alpha = 0.24f),
                            selectedLabelColor = NeonMagenta
                        ),
                        border = FilterChipDefaults.filterChipBorder(
                            enabled = !autoMode,
                            selected = selectedPreset == name,
                            borderColor = ObsidianEdge,
                            selectedBorderColor = NeonMagenta
                        )
                    )
                }
            }
        }

        GlassCard(title = "DSP CORE", accent = AuroraCyan, subtitle = "EQ · Exciter · Widener · Gain") {
            AuroraSlider("EXCITER", exciter, 0f..1f, unit = "×") { exciter = it; onExciterChange(it) }
            AuroraSlider("EQ GAIN", eq, -18f..18f, unit = "dB") { eq = it; onEqChange(it) }
            AuroraSlider("STEREO WIDTH", width, 0f..1.5f, unit = "γ") { width = it; onWidthChange(it) }
        }

        GlassCard(title = "COMPRESOR", accent = AmberSignal, subtitle = "g_comp · dinámica lock-free") {
            AuroraSlider("THRESHOLD", compThreshold, 0f..1f,
                displayValue = { "%.1f dB".format(-24f + it * 24f) }) {
                compThreshold = it; onCompThresholdChange(it)
            }
            AuroraSlider("RATIO", compRatio, 0f..1f,
                displayValue = { "%.1f:1".format(1f + it * 19f) }) {
                compRatio = it; onCompRatioChange(it)
            }
        }

        GlassCard(
            title = "PHASE ORACLE",
            accent = PhosphorGreen,
            subtitle = "Coherencia de fase · all-pass 10 bandas · α/β/γ → nativo"
        ) {
            AuroraSlider(
                "COHERENCIA DE FASE",
                phaseOracleIntensity,
                0f..1f,
                displayValue = {
                    when {
                        it < 0.01f -> "OFF"
                        it < 0.35f -> "SUAVE %.0f%%".format(it * 100f)
                        it < 0.70f -> "MEDIO %.0f%%".format(it * 100f)
                        else       -> "MÁXIMO %.0f%%".format(it * 100f)
                    }
                }
            ) {
                phaseOracleIntensity = it
                onPhaseOracleChange(it)
            }
            if (phaseOracleIntensity > 0.01f) {
                Spacer(Modifier.height(4.dp))
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    StatBlock("α LF",  "%.2f".format(phaseOracleIntensity),        PhosphorGreen, Modifier.weight(1f))
                    StatBlock("β MF",  "%.2f".format(phaseOracleIntensity * 0.7f), AuroraCyan,    Modifier.weight(1f))
                    StatBlock("γ HF",  "%.2f".format(phaseOracleIntensity * 0.5f), NeonMagenta,   Modifier.weight(1f))
                }
            }
        }

        SectionLabel("ESPACIAL Y NEUROMÓRFICO", NeonMagenta)
        
        GlassCard(
            title = "INTELIGENCIA ESPACIAL (HOA → BINAURAL)",
            accent = NeonMagenta,
            subtitle = "Upmixing M/S Real a Anillo HOA",
            rightSlot = {
                Switch(hoaUpmixingEnabled, onCheckedChange = {
                    hoaUpmixingEnabled = it
                    onHoaUpmixingEnabledChange(it)
                })
            }
        ) {
            AuroraSlider("INMERSIVIDAD HOA", hoaImmersivity, 0f..2f) {
                hoaImmersivity = it
                onHoaImmersivityChange(it)
            }
        }

        GlassCard(
            title = "MODO AUX · ALTAVOCES",
            accent = PhosphorGreen,
            subtitle = if (auxEnabled)
                "Activo: graves en fase y misma presión en ambos lados · HRTF, HOA y WFS binaurales en pausa"
            else
                "Para salida por AUX/amplificador: graves coherentes (Linkwitz-Riley 4º orden). Apagado = audio intacto",
            rightSlot = {
                Switch(auxEnabled, onCheckedChange = {
                    auxEnabled = it
                    onAuxFieldChange(it, auxXover, auxCoherence)
                })
            }
        ) {
            AuroraSlider("CRUCE DE GRAVES", auxXover, 60f..200f, unit = " Hz",
                displayValue = { "${(60 + 20 * Math.round((it - 60f) / 20f)).coerceIn(60, 200)} Hz" }) {
                auxXover = it
                onAuxFieldChange(auxEnabled, it, auxCoherence)
            }
            AuroraSlider("COHERENCIA DE GRAVES", auxCoherence, 0f..1f,
                displayValue = { "${Math.round(it * 7f) * 100 / 7} %" }) {
                auxCoherence = it
                onAuxFieldChange(auxEnabled, auxXover, it)
            }
        }

        GlassCard(
            title = "CORRECCIÓN ACÚSTICA · SALA / BOCINAS",
            accent = AuroraCyan,
            subtitle = when {
                roomBusy -> "Midiendo… no hables ni te muevas. Tonos de prueba por tus bocinas (~35 s)"
                roomGains == null -> "Mide tu sala con el micrófono del teléfono y corrige el balance tonal de 50 Hz a 6 kHz. Sin medición = audio intacto"
                roomEnabled -> "Activa: corrección medida aplicada en todas las salidas (AUX, USB-C, Bluetooth)"
                else -> "Medición guardada, corrección apagada"
            },
            rightSlot = {
                Switch(roomEnabled, enabled = roomGains != null && !roomBusy, onCheckedChange = { onRoomToggle(it) })
            }
        ) {
            if (roomBusy) {
                LinearProgressIndicator(progress = { roomProgress }, modifier = Modifier.fillMaxWidth())
            }
            if (roomGains != null && !roomBusy) {
                val labels = arrayOf("31.5", "63", "125", "250", "500", "1k", "2k", "4k", "8k", "16k")
                val parts = (0 until 10).filter { kotlin.math.abs(roomGains[it]) >= 0.05f }
                    .joinToString(" · ") { "${labels[it]} Hz ${if (roomGains[it] > 0) "+" else ""}${"%.1f".format(java.util.Locale.US, roomGains[it])} dB" }
                Text(
                    if (parts.isEmpty()) "Tu sala ya suena pareja: no hace falta corrección" else parts,
                    style = MaterialTheme.typography.labelSmall, color = TextSecondary
                )
            }
            if (roomStatus.isNotEmpty()) {
                Text(roomStatus, style = MaterialTheme.typography.labelSmall, color = TextSecondary)
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedButton(enabled = !roomBusy, onClick = onRoomMeasure) {
                    Text(if (roomGains == null) "MEDIR SALA" else "MEDIR DE NUEVO")
                }
                if (roomGains != null) {
                    OutlinedButton(enabled = !roomBusy, onClick = onRoomClear) { Text("BORRAR MEDICIÓN") }
                }
            }
            com.ivanna.omega.spatial.RoomGeometryPreviewPanel()
            Text(
                "Límites: el micrófono del teléfono no está calibrado; corrige el balance amplio (boom, caída de graves, presencia), no modos estrechos ni la fase, y mide el punto donde está el teléfono.",
                style = MaterialTheme.typography.labelSmall, color = TextSecondary
            )
        }

        GlassCard(
            title = "DINÁMICA ESPACIAL · 4D",
            accent = PhosphorGreen,
            subtitle = if (temporalEnabled)
                "Activo: en cada golpe la imagen se enfoca; en las colas se abre · el centro no cambia, los graves tampoco"
            else
                "La cuarta dimensión es el tiempo: el ancho lateral respira con la música (golpe = enfoque, cola = apertura). Apagado = audio intacto",
            rightSlot = {
                Switch(temporalEnabled, onCheckedChange = {
                    temporalEnabled = it
                    onTemporalFieldChange(it, temporalAmount, temporalAdaptive)
                })
            }
        ) {
            AuroraSlider(if (temporalAdaptive) "PROFUNDIDAD MÁXIMA" else "PROFUNDIDAD TEMPORAL", temporalAmount, 0f..1f,
                displayValue = { "${Math.round(it * 7f) * 100 / 7} %" }) {
                temporalAmount = it
                onTemporalFieldChange(temporalEnabled, it, temporalAdaptive)
            }
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    "ADAPTATIVO · escala con la densidad de golpes (el slider es el techo)",
                    style = MaterialTheme.typography.labelSmall, color = TextSecondary,
                    modifier = Modifier.weight(1f)
                )
                Switch(temporalAdaptive, onCheckedChange = {
                    temporalAdaptive = it
                    onTemporalFieldChange(temporalEnabled, temporalAmount, it)
                })
            }
        }

        GlassCard(
            title = "HÁPTICA DE GRAVES · 4D",
            accent = AuroraCyan,
            subtitle = when {
                !bassHapticSupported -> "Este teléfono no tiene motor de vibración"
                hapticEnabled -> "Activo: el teléfono vibra con cada golpe de bombo/sub (solo con el motor de la app activo)"
                else -> "El golpe de graves también se siente: vibración sincronizada con bombo y sub. Apagado = sin vibración"
            },
            rightSlot = {
                Switch(hapticEnabled, enabled = bassHapticSupported, onCheckedChange = {
                    hapticEnabled = it
                    onBassHapticChange(it, hapticStrength, hapticRumble, hapticTrim)
                })
            }
        ) {
            AuroraSlider("FUERZA DE VIBRACIÓN", hapticStrength, 0f..1f,
                displayValue = { "${Math.round(it * 100f)} %" }) {
                hapticStrength = it
                onBassHapticChange(hapticEnabled, it, hapticRumble, hapticTrim)
            }
            AuroraSlider("RETUMBO (sub sostenido)", hapticRumble, 0f..1f,
                displayValue = { if (it <= 0f) "OFF" else "${Math.round(it * 100f)} %" }) {
                hapticRumble = it
                onBassHapticChange(hapticEnabled, hapticStrength, it, hapticTrim)
            }
            AuroraSlider("SINCRONÍA (ajuste fino)", hapticTrim, -100f..200f,
                displayValue = { "${Math.round(it)} ms" }) {
                hapticTrim = it
                onBassHapticChange(hapticEnabled, hapticStrength, hapticRumble, it)
            }
            if (hapticEnabled) {
                Text(hapticSyncText, style = MaterialTheme.typography.labelSmall, color = TextSecondary)
            }
            OutlinedButton(
                enabled = bassHapticSupported,
                onClick = {
                    // Un toque: campo coherente por AUX/DAC + dinámica espacial temporal + háptica con retumbo suave
                    // + impacto predictivo al 50 % (el ataque de cada golpe llega 1.5 ms antes; el pico no sube)
                    punchAmount = 0.5f
                    onPunchChange(0.5f)
                    auxEnabled = true
                    onAuxFieldChange(true, auxXover, auxCoherence)
                    temporalEnabled = true
                    onTemporalFieldChange(true, temporalAmount, temporalAdaptive)
                    hapticEnabled = true
                    hapticStrength = 0.7f
                    hapticRumble = 0.3f
                    onBassHapticChange(true, 0.7f, 0.3f, hapticTrim)
                }
            ) { Text("PERFIL 4D PARA AUX / DAC") }
        }

        GlassCard(
            title = "IMPACTO PREDICTIVO · 4D",
            accent = AuroraCyan,
            subtitle = if (punchAmount > 0f)
                "Activo: el ataque de cada golpe se realza 1.5 ms ANTES de sonar, en 3 bandas · el pico no sube y la guarda de graves protege altavoces y etapa"
            else
                "El tiempo como impacto: realza el ataque de bombo, caja y bajo sin subir el nivel medio ni inventar headroom. Apagado = audio intacto y sin latencia"
        ) {
            AuroraSlider("IMPACTO", punchAmount, 0f..1f,
                displayValue = { "${Math.round(it * 15f) * 100 / 15} %" }) {
                punchAmount = it
                onPunchChange(it)
            }
            val pt = punchTelemetry
            Text(
                when {
                    punchAmount <= 0f -> "Apagado"
                    pt.size < 6 -> "Sin telemetría (motor nativo no activo)"
                    else ->
                        "bajo ${"%+.1f".format(pt[0])} · medio ${"%+.1f".format(pt[1])} · alto ${"%+.1f".format(pt[2])} dB   " +
                        "ataque ${"%.0f".format(pt[3] * 100f)} %   latencia ${"%.1f".format(pt[5])} ms" +
                        (if (pt[4] > 0.5f) "   · guarda activa" else "")
                },
                style = MaterialTheme.typography.labelSmall,
                color = TextMuted
            )
        }

        GlassCard(
            title = "WAVE FIELD SYNTHESIS",
            accent = AuroraCyan,
            subtitle = "Array virtual de 16 altavoces — campo de onda físico (renderer nativo testeado)",
            rightSlot = {
                Switch(wfsEnabled, onCheckedChange = {
                    wfsEnabled = it
                    onWfsEnabledChange(it)
                })
            }
        ) {
            AuroraSlider("APERTURA WFS", wfsSpread, 0f..2f) {
                wfsSpread = it
                onWfsSpreadChange(it)
            }
        }

        GlassCard(
            title = "NHO / ESPACIAL",
            accent = PhosphorGreen,
            subtitle = "PDEngine g_pd · activo en +NHO / +Spatial"
        ) {
            AuroraSlider("GANANCIA ARMÓNICA (NHO)", nhoHarmonic, 0f..1f) {
                nhoHarmonic = it; onNhoHarmonicChange(it)
            }
            AuroraSlider("ÁNGULO ESPACIAL", spatialAngle, 0f..1.33f, unit = "rad") {
                spatialAngle = it; onSpatialAngleChange(it)
            }
            AuroraSlider("ANCHO ESPACIAL", spatialWidth, 0f..1.5f) {
                spatialWidth = it; onSpatialWidthChange(it)
            }
        }

        GlassCard(
            title = "MOTOR NPE · NEUROMÓRFICO",
            accent = AuroraCyan,
            subtitle = "NHO + LIF + BiquadEnvelopeBank + AutonomousBrain",
            rightSlot = {
                ToggleSwitch(!npeBypass, { on -> npeBypass = !on; onNpeBypassChange(!on) }, AuroraCyan)
            }
        ) {
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                StatBlock("GÉNERO", npeGenre, NeonMagenta, Modifier.weight(1.4f))
                StatBlock("CONF.", if (npeGenre == "\u2014" || npeGenre.isBlank()) "\u2014" else "%.0f%%".format(npeClassifyConfidence * 100f), PhosphorGreen, Modifier.weight(1f))
                StatBlock("ASPEREZA", "%.1f%%".format(npeClassifyThd), AmberSignal, Modifier.weight(1f))
            }
            val npeSig = remember(npeInferenceUs) { IvannaNpeEngine.getSynthSignature() }
            val npeBuildTag = remember { IvannaNpeEngine.getBuildTag() }
            val npeCopyright = remember { IvannaNpeEngine.getCopyright() }
            val lstmNpSat = remember(npeInferenceUs) { com.ivanna.omega.neuromorphic.PiLstmBridge.getNpSat() }
            val lstmError = remember(npeInferenceUs) { com.ivanna.omega.neuromorphic.PiLstmBridge.getError() }
            if (npeSig.size >= 5) {
                Text(
                    "FIRMA 5B (dB): SUB=${"%+.1f".format(npeSig[0])} · BASS=${"%+.1f".format(npeSig[1])} · " +
                    "MID=${"%+.1f".format(npeSig[2])} · PRES=${"%+.1f".format(npeSig[3])} · AIR=${"%+.1f".format(npeSig[4])} · " +
                    "NP=${"%.2f".format(lstmNpSat)} · ERR=${"%.4f".format(lstmError)}",
                    color = AuroraCyan,
                    fontSize = 9.sp,
                    fontFamily = FontFamily.Monospace
                )
            }
            Spacer(Modifier.height(4.dp))
            AuroraSlider("GANANCIA ARMÓNICA · NHO", npeHarmonic, 0f..2f, unit = "×") {
                npeHarmonic = it
                onNpeHarmonicChange(it)
                IvannaNpeEngine.setParameters(
                    alpha = npeOhcCompression,
                    beta = npeLateralInhib,
                    gamma = 1.0f,
                    delta = npeAgcRate,
                    eta = 0.5f,
                    zeta = npeLateralInhib,
                    nonlinearity = it
                )
            }
            AuroraSlider("INHIBICIÓN LATERAL", npeLateralInhib, 0f..1f) {
                npeLateralInhib = it; onNpeLateralInhibChange(it)
            }
            AuroraSlider("COMPRESIÓN OHC", npeOhcCompression, 0f..1f) {
                npeOhcCompression = it
                onNpeOhcCompressionChange(it)
                com.ivanna.omega.neuromorphic.PiLstmBridge.setOdeDamping(it * 2.5f)
            }
            AuroraSlider("MASTER GAIN", npeMasterGain, -18f..18f, unit = "dB") {
                npeMasterGain = it; onNpeMasterGainChange(it)
            }
            AuroraSlider("AGC TARGET", npeAgcTarget, -36f..0f, unit = "dB") {
                npeAgcTarget = it; onNpeAgcChange(it, npeAgcRate)
            }
            AuroraSlider("AGC RATE", npeAgcRate, 0f..1f) {
                npeAgcRate = it; onNpeAgcChange(npeAgcTarget, it)
            }
            Spacer(Modifier.height(4.dp))
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                FlagToggle("HRTF", npeHrtf, AuroraCyan, Modifier.weight(1f)) {
                    npeHrtf = it; onNpeFlagsChange(it, npeCochlear, npeAdapt)
                }
                FlagToggle("COCLEAR", npeCochlear, NeonMagenta, Modifier.weight(1f)) {
                    npeCochlear = it; onNpeFlagsChange(npeHrtf, it, npeAdapt)
                }
                FlagToggle("ADAPT/LIF", npeAdapt, PhosphorGreen, Modifier.weight(1f)) {
                    npeAdapt = it; onNpeFlagsChange(npeHrtf, npeCochlear, it)
                }
            }
            Spacer(Modifier.height(6.dp))
            FlagToggle("MANIFOLD (Volterra H2)", npeManifold, AuroraCyan, Modifier.fillMaxWidth()) {
                npeManifold = it
                if (it && spatialEnabled) {
                    spatialEnabled = false
                    onSpatialEnabledChange(false)
                }
                onNpeManifoldChange(it)
            }
            Spacer(Modifier.height(6.dp))
            var npeNullText by remember { mutableStateOf("") }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
                OutlinedButton(
                    onClick = {
                        val r = com.ivanna.omega.neuromorphic.PiLstmBridge.measureContribution(npeHarmonic, npeLateralInhib, npeOhcCompression, npeMasterGain)
                        npeNullText = if (r == null) "No disponible: motor nativo no cargado" else {
                            val names = arrayOf("sub", "cuerpo", "definición", "presencia", "aire")
                            "Aporte del NPE (señal de prueba): residuo ${"%.1f".format(java.util.Locale.US, r[0])} dB · ganancia ${"%+.1f".format(java.util.Locale.US, r[1])} dB · " +
                                "${r[3].toInt()} de 5 bandas audibles (" + names.indices.joinToString(" ") { "${names[it]} ${"%.0f".format(java.util.Locale.US, r[4 + it])}" } + " dB). Orienta, no sustituye escuchar."
                        }
                    },
                    colors = ButtonDefaults.outlinedButtonColors(contentColor = AuroraCyan)
                ) { Text("MEDIR APORTE DEL NPE", fontSize = 10.sp) }
            }
            if (npeNullText.isNotEmpty()) Text(npeNullText, color = TextSecondary, fontSize = 10.sp)
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    "$npeBuildTag ${if (npeCopyright.isNotBlank()) "· $npeCopyright" else ""}",
                    color = TextMuted,
                    fontSize = 9.sp,
                    fontFamily = FontFamily.Monospace,
                    modifier = Modifier.weight(1f)
                )
                OutlinedButton(
                    onClick = {
                        IvannaNpeEngine.reset()
                        com.ivanna.omega.neuromorphic.PiLstmBridge.resetTelemetry()
                    },
                    colors = ButtonDefaults.outlinedButtonColors(contentColor = AuroraCyan)
                ) { Text("RESET NPE", fontSize = 10.sp) }
            }
        }

        GlassCard(
            title = "MOTOR BINAURAL · 32 OBJETOS",
            accent = NeonMagenta,
            subtitle = "Upmix neural + VBAP/HRTF + head-tracking 6DoF",
            rightSlot = {
                ToggleSwitch(spatialEnabled, {
                    spatialEnabled = it
                    if (it && npeManifold) {
                        npeManifold = false
                        onNpeManifoldChange(false)
                    }
                    onSpatialEnabledChange(it)
                }, NeonMagenta)
            }
        ) {
            Text(
                "Activa el renderer de objetos completo: separa hasta 32 stems " +
                "virtuales, los posiciona en el anillo VBAP y aplica convolución " +
                "HRTF con seguimiento de cabeza en tiempo real.",
                style = MaterialTheme.typography.bodySmall,
                color = TextSecondary
            )
        }

        SectionLabel("KERNEL EVOLUTIVO", AmberSignal)

        GlassCard(
            title = "KERNEL EVOLUTIVO",
            accent = AmberSignal,
            subtitle = "g_population · hilo de baja prioridad · CMA-ES 8λ",
            rightSlot = {
                ToggleSwitch(evoEnabled, { evoEnabled = it; onEvoEnabledChange(it) }, AmberSignal)
            }
        ) {
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                StatBlock("GENERACIÓN", evoGeneration.toString(), AmberSignal, Modifier.weight(1f))
                StatBlock("FITNESS", "%.3f".format(evoFitness), PhosphorGreen, Modifier.weight(1f))
            }

            // FIX: evoPopSize/Generations/MutationRate existían en AdaptiveControlsPrefs
            // pero NUNCA se cargaban al IvannaControlPanel ni se aplicaban al nativo.
            // Ahora se muestran como sliders y llaman nativeInitializeEvolution +
            // nativeSetMutationRate en cada cambio.
            var evoPopSize by remember { mutableIntStateOf(savedState.evoPopSize) }
            var evoGenerations by remember { mutableIntStateOf(savedState.evoGenerations) }
            var evoMutationRate by remember { mutableFloatStateOf(savedState.evoMutationRate) }

            Spacer(Modifier.height(6.dp))
            AuroraSlider(
                "TAMAÑO POBLACIÓN",
                evoPopSize.toFloat().coerceIn(32f, 128f), 32f..128f,
                displayValue = { "${it.toInt()}" }
            ) { v ->
                evoPopSize = v.toInt()
                AdaptiveControlsPrefs.save(context, AdaptiveControlsPrefs.load(context).copy(evoPopSize = evoPopSize))
                if (IvannaNativeLib.isLoaded && evoEnabled) {
                    runCatching { IvannaNativeLib.nativeSetEvolutionParams(evoPopSize, evoGenerations) }
                }
            }
            AuroraSlider(
                "GENERACIONES MAX",
                evoGenerations.toFloat(), 10f..500f,
                displayValue = { "${it.toInt()}" }
            ) { v ->
                evoGenerations = v.toInt()
                AdaptiveControlsPrefs.save(context, AdaptiveControlsPrefs.load(context).copy(evoGenerations = evoGenerations))
                if (IvannaNativeLib.isLoaded && evoEnabled) {
                    runCatching { IvannaNativeLib.nativeSetEvolutionParams(evoPopSize, evoGenerations) }
                }
            }
            AuroraSlider(
                "TASA DE MUTACIÓN",
                evoMutationRate, 0f..0.5f,
                displayValue = { "%.3f".format(it) }
            ) { v ->
                evoMutationRate = v
                AdaptiveControlsPrefs.save(context, AdaptiveControlsPrefs.load(context).copy(evoMutationRate = v))
                if (IvannaNativeLib.isLoaded) {
                    runCatching { IvannaNativeLib.nativeSetMutationRate(v) }
                }
            }

            Spacer(Modifier.height(6.dp))
            // FIX: nativeSaveEvoState / nativeLoadEvoState existían pero nunca
            // tenían un punto de entrada desde UI — el estado evolutivo se perdía.
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedButton(
                    onClick = {
                        if (IvannaNativeLib.isLoaded) {
                            runCatching { IvannaNativeLib.nativeSaveEvoState() }
                        }
                    },
                    modifier = Modifier.weight(1f),
                    colors = ButtonDefaults.outlinedButtonColors(contentColor = AmberSignal)
                ) { Text("GUARDAR EVO", fontSize = 10.sp) }
                OutlinedButton(
                    onClick = {
                        if (IvannaNativeLib.isLoaded) {
                            runCatching {
                                IvannaNativeLib.nativeLoadEvoState()
                                evoFitness = IvannaNativeLib.nativeGetEvoBestFitness()
                                evoGeneration = IvannaNativeLib.nativeGetGeneration()
                            }
                        }
                    },
                    modifier = Modifier.weight(1f),
                    colors = ButtonDefaults.outlinedButtonColors(contentColor = PhosphorGreen)
                ) { Text("RESTAURAR EVO", fontSize = 10.sp) }
            }
        }

        SectionLabel("5 EJES DE SUPREMACÍA CUÁNTICO-NEUROMÓRFICA", PhosphorGreen)
        GlassCard(
            title = "CENTRO MAESTRO · 5 EJES DE SUPREMACÍA C++23",
            accent = PhosphorGreen,
            subtitle = "Celosía Deformada · CVNN+DDSP · SNN-HOA 4º · Pinna INR-SDF · SHM Farrow 5º"
        ) {
            Text(
                "Controla en tiempo real los 5 motores zero-copy y lock-free del pipeline: " +
                "inversión de transductores Bl(x), reconstrucción transarmónica Hilbert >16 kHz, " +
                "separación SNN INT8 hacia Ambisonics de 4º orden (16 canales), calibración " +
                "fotogramétrica INR-SDF del pabellón auricular y arbitraje SHM con Farrow de 5º orden.",
                style = MaterialTheme.typography.bodySmall,
                color = TextSecondary
            )
            OutlinedButton(
                onClick = onOpenSupremeAxesHub,
                modifier = Modifier.fillMaxWidth(),
                border = BorderStroke(1.dp, PhosphorGreen)
            ) {
                Text(
                    "ABRIR PANEL DE CONTROL DE LOS 5 EJES SUPREMOS →",
                    color = PhosphorGreen,
                    fontSize = 11.sp,
                    fontWeight = FontWeight.Bold
                )
            }
        }

        SectionLabel("ACOUSTIC REALITY RECONSTRUCTION HYPERENGINE", AuroraCyan)
        AcousticRealityReconstructionCard()

        // Guía transversal: explica los controles de CONTROL, SOUND, BRAIN,
        // ADAPTIVE, SPATIAL, PROFILER y las salidas físicas.
        ControlHelpCatalogPanel()

        Spacer(Modifier.height(4.dp))
        Text(
            "IVANNA-OMEGA-SUPREME · GORE TNS / LUPP-OR9 © 2026",
            style = MaterialTheme.typography.labelSmall,
            color = TextMuted,
            textAlign = TextAlign.Center,
            modifier = Modifier.fillMaxWidth()
        )
        Spacer(Modifier.height(8.dp))
    }
}

@Composable
private fun AcousticRealityReconstructionCard() {
    val context = LocalContext.current
    var axesState by remember { mutableStateOf(SupremeAxesPrefs.load(context)) }
    // null = el orquestador aún no ha corrido un ciclo vivo con audio real (no hay medición).
    var telemetry by remember {
        mutableStateOf(com.ivanna.omega.core.NativeBridge.safeGetRealityTelemetryOrNull())
    }
    var cogTelemetry by remember {
        mutableStateOf(com.ivanna.omega.core.NativeBridge.safeGetCognitiveTelemetryOrNull())
    }

    LaunchedEffect(axesState.realityReconstructionEnabled) {
        com.ivanna.omega.core.NativeBridge.safeSetRealityReconstructionEnabled(axesState.realityReconstructionEnabled)
        com.ivanna.omega.core.NativeBridge.safeSetRealityIntensity(axesState.realityIntensity)
        com.ivanna.omega.core.NativeBridge.safeSetPersonalAuditoryProfile(
            headRadiusM = axesState.personalHeadRadiusM,
            pinnaDepthM = axesState.personalPinnaDepthM,
            elevationBiasDeg = axesState.personalElevationBiasDeg,
            transducerType = axesState.personalTransducerType,
            sensitivityScore = 1.0f
        )
        while (isActive) {
            telemetry = withContext(Dispatchers.Default) {
                com.ivanna.omega.core.NativeBridge.safeGetRealityTelemetryOrNull()
            }
            cogTelemetry = withContext(Dispatchers.Default) {
                com.ivanna.omega.core.NativeBridge.safeGetCognitiveTelemetryOrNull()
            }
            delay(300L)
        }
    }

    val rt: (Int) -> Float? = { i -> telemetry?.getOrNull(i)?.takeIf { it.isFinite() } }
    val ct: (Int) -> Float? = { i -> cogTelemetry?.getOrNull(i)?.takeIf { it.isFinite() } }
    val pct0: (Float?) -> String = { v -> v?.let { "%.0f%%".format(it * 100f) } ?: "—" }
    val pct1: (Float?) -> String = { v -> v?.let { "%.1f%%".format(it * 100f) } ?: "—" }
    val presence = rt(0); val naturalness = rt(1); val separation = rt(2)
    val fatigue = rt(3); val immersion = rt(4); val realism = rt(5)
    val roomW = rt(6); val roomD = rt(7); val roomH = rt(8); val roomRt60 = rt(9)
    val microGain = rt(11); val coherence = rt(15)
    val topPriorityIdx = ct(0)?.toInt()
    val topPriorityLabel = when (topPriorityIdx) {
        null -> "—"
        0 -> "1º PROFUNDIDAD"
        1 -> "1º MICRODINÁMICA"
        2 -> "1º CLARIDAD VOCAL"
        else -> "1º EXP. AMBIENTAL"
    }
    val humanJudgeVerdict = ct(8)
    val execCoherence     = ct(10)
    val homeostasisIdx    = ct(12)
    val digitalTwinIdx    = ct(13)
    val evoFitness        = ct(14)

    GlassCard(
        title = "RECONSTRUCCIÓN DE REALIDAD & CEREBRO EJECUTIVO (FASES 1–15)",
        accent = if (axesState.realityReconstructionEnabled) AuroraCyan else TextMuted,
        subtitle = "SOURCE → ROOM → AIR → EAR · ExecutiveBrain · Homeostasis · DigitalTwin",
        rightSlot = {
            ToggleSwitch(
                checked = axesState.realityReconstructionEnabled,
                onCheckedChange = { en ->
                    val next = axesState.copy(realityReconstructionEnabled = en)
                    axesState = next
                    SupremeAxesPrefs.save(context, next)
                    SupremeAxesPrefs.applyToNative(next)
                },
                accent = AuroraCyan
            )
        }
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            StatBlock(
                label = "REALISMO",
                value = pct1(realism),
                accent = PhosphorGreen,
                modifier = Modifier.weight(1f)
            )
            StatBlock(
                label = "PRESENCIA",
                value = pct0(presence),
                accent = AuroraCyan,
                modifier = Modifier.weight(1f)
            )
            StatBlock(
                label = "NATURALIDAD",
                value = pct0(naturalness),
                accent = NeonMagenta,
                modifier = Modifier.weight(1f)
            )
        }
        Spacer(Modifier.height(6.dp))
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            StatBlock(
                label = "SEPARACIÓN",
                value = pct0(separation),
                accent = AuroraCyan,
                modifier = Modifier.weight(1f)
            )
            StatBlock(
                label = "INMERSIÓN 4D",
                value = pct0(immersion),
                accent = PhosphorGreen,
                modifier = Modifier.weight(1f)
            )
            StatBlock(
                label = "FATIGA",
                value = pct0(fatigue),
                accent = if ((fatigue ?: 0f) > 0.35f) CoralWarn else PhosphorGreen,
                modifier = Modifier.weight(1f)
            )
        }
        Spacer(Modifier.height(6.dp))
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            StatBlock(
                label = "RECINTO 4D",
                value = if (roomW != null && roomD != null && roomH != null) "%.1f×%.1f×%.1fm".format(roomW, roomD, roomH) else "—",
                accent = AmberSignal,
                modifier = Modifier.weight(1.3f)
            )
            StatBlock(
                label = "RT60 SALA",
                value = roomRt60?.let { "%.2fs".format(it) } ?: "—",
                accent = AuroraCyan,
                modifier = Modifier.weight(0.85f)
            )
            StatBlock(
                label = "INTELIGIB.",
                value = microGain?.let { "%.2f×".format(it) } ?: "—",
                accent = PhosphorGreen,
                modifier = Modifier.weight(0.85f)
            )
        }
        Spacer(Modifier.height(6.dp))
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            StatBlock(
                label = "INTENT FASE 9",
                value = topPriorityLabel,
                accent = AuroraCyan,
                modifier = Modifier.weight(1.2f)
            )
            StatBlock(
                label = "JUEZ HUMANO",
                value = pct0(humanJudgeVerdict),
                accent = PhosphorGreen,
                modifier = Modifier.weight(0.9f)
            )
            StatBlock(
                label = "HOMEOSTASIS",
                value = pct0(homeostasisIdx),
                accent = NeonMagenta,
                modifier = Modifier.weight(0.9f)
            )
        }
        Spacer(Modifier.height(6.dp))
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            StatBlock(
                label = "EXEC BRAIN",
                value = pct0(execCoherence),
                accent = AuroraCyan,
                modifier = Modifier.weight(1f)
            )
            StatBlock(
                label = "DIGITAL TWIN",
                value = pct0(digitalTwinIdx),
                accent = AmberSignal,
                modifier = Modifier.weight(1f)
            )
            StatBlock(
                label = "EVO CMA-ES/Q",
                value = pct1(evoFitness),
                accent = PhosphorGreen,
                modifier = Modifier.weight(1f)
            )
        }

        Spacer(Modifier.height(6.dp))
        AuroraSlider(
            label = "INTENSIDAD DE RECONSTRUCCIÓN DEL EVENTO",
            value = axesState.realityIntensity,
            range = 0f..1f,
            displayValue = { "%.0f%% · Coherencia %s".format(it * 100f, pct0(coherence)) }
        ) { v ->
            val next = axesState.copy(realityIntensity = v)
            axesState = next
            SupremeAxesPrefs.save(context, next)
            com.ivanna.omega.core.NativeBridge.safeSetRealityIntensity(v)
        }

        AuroraSlider(
            label = "RADIO CEFÁLICO PERSONAL (MODELO AUDITIVO FASE 5)",
            value = axesState.personalHeadRadiusM,
            range = 0.075f..0.105f,
            displayValue = { "%.1f mm".format(it * 1000f) }
        ) { r ->
            val next = axesState.copy(personalHeadRadiusM = r)
            axesState = next
            SupremeAxesPrefs.save(context, next)
            com.ivanna.omega.core.NativeBridge.safeSetPersonalAuditoryProfile(
                headRadiusM = r,
                pinnaDepthM = next.personalPinnaDepthM,
                elevationBiasDeg = next.personalElevationBiasDeg,
                transducerType = next.personalTransducerType,
                sensitivityScore = 1.0f
            )
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════
// NAEL: toggle + mini gráfico de 3 barras (low/mid/high) con la corrección
// actual en dB. El JNI devuelve FloatArray[10] (bandas ISO 1/1-oct); se
// pliega a los tres cubos del ControlFrame (mismo folding que en
// audio_control_plane.cpp: low=bandas 0-3, mid=4-6, high=7-9).
// ═══════════════════════════════════════════════════════════════════════
@Composable
private fun NaelCard() {
    // FIX (NAEL no persistía): el toggle arrancaba siempre en OFF y no se reaplicaba
    // al motor tras cerrar la app. Ahora se guarda en prefs y se reenvía al nativo.
    val naelPrefs = androidx.compose.ui.platform.LocalContext.current
        .getSharedPreferences("ivanna_ui_prefs", android.content.Context.MODE_PRIVATE)
    var enabled by remember { mutableStateOf(naelPrefs.getBoolean("nael_enabled", false)) }
    var corrections by remember { mutableStateOf(FloatArray(10)) }
    LaunchedEffect(Unit) {
        if (enabled) runCatching { IvannaNativeLib.nativeSetNaelEnabled(true) }
    }

    LaunchedEffect(enabled) {
        while (isActive && enabled) {
            val snap = withContext(Dispatchers.Default) {
                runCatching {
                    IvannaNativeLib.nativeGetNaelCorrections()
                }.getOrNull() ?: FloatArray(10)
            }
            corrections = snap
            delay(400)
        }
    }

    GlassCard(
        title = "NAEL · ISO 226:2023",
        accent = AuroraCyan,
        subtitle = if (enabled)
            "Compensación de loudness activa · timbre constante por volumen"
        else
            "Curva de igual sonoridad · corrige graves/agudos a bajo volumen",
        rightSlot = {
            ToggleSwitch(enabled, { on ->
                enabled = on
                naelPrefs.edit().putBoolean("nael_enabled", on).apply()
                runCatching { IvannaNativeLib.nativeSetNaelEnabled(on) }
                    .onFailure { Log.w("IvannaControlPanel", "NAEL toggle falla: ${it.message}") }
            }, AuroraCyan)
        }
    ) {
        if (enabled) {
            // Pliega bandas ISO 10 -> 3 buckets (mismo folding que C++):
            // low=0..3 (31-250 Hz), mid=4..6 (500-2k Hz), high=7..9 (4-16k Hz).
            val lowAdd  = if (corrections.size >= 4)
                (corrections[0] + corrections[1] + corrections[2] + corrections[3]) * 0.25f else 0f
            val midAdd  = if (corrections.size >= 7)
                (corrections[4] + corrections[5] + corrections[6]) / 3f else 0f
            val highAdd = if (corrections.size >= 10)
                (corrections[7] + corrections[8] + corrections[9]) / 3f else 0f

            Spacer(Modifier.height(6.dp))
            Row(
                modifier = Modifier.fillMaxWidth().height(64.dp),
                horizontalArrangement = Arrangement.spacedBy(10.dp),
                verticalAlignment = Alignment.Bottom
            ) {
                NaelBar("LOW",  lowAdd,  AuroraCyan,    Modifier.weight(1f))
                NaelBar("MID",  midAdd,  PhosphorGreen, Modifier.weight(1f))
                NaelBar("HIGH", highAdd, NeonMagenta,   Modifier.weight(1f))
            }
            Spacer(Modifier.height(4.dp))
            Text(
                "Rango: ±8 dB por banda · EMA τ=500ms · LUFS integrado BS.1770-4",
                style = MaterialTheme.typography.labelSmall,
                color = TextMuted
            )
        } else {
            Text(
                "Activa para mantener el timbre constante entre volumen bajo y " +
                "referencia de mastering (83 phon). Usa el LUFS integrado real " +
                "del pipeline — sin latencia extra en el hilo de audio.",
                style = MaterialTheme.typography.bodySmall,
                color = TextSecondary
            )
        }
    }
}

@Composable
private fun NaelBar(label: String, db: Float, accent: Color, modifier: Modifier = Modifier) {
    // Escalar |db| a altura 0..40dp (rango total ±8dB → 8dB = 40dp).
    val magnitude = kotlin.math.abs(db).coerceAtMost(8f)
    val heightDp  = (magnitude / 8f * 40f).coerceAtLeast(2f)
    Column(
        modifier = modifier,
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Bottom
    ) {
        Text(
            "%+.1f dB".format(db),
            style = MaterialTheme.typography.labelSmall,
            color = accent
        )
        Spacer(Modifier.height(2.dp))
        Box(
            Modifier
                .fillMaxWidth()
                .height(heightDp.dp)
                .background(accent.copy(alpha = 0.75f), RoundedCornerShape(3.dp))
        )
        Spacer(Modifier.height(4.dp))
        Text(label, style = MaterialTheme.typography.labelSmall, color = TextSecondary)
    }
}

@Composable
private fun SectionLabel(text: String, accent: Color) {
    Row(
        modifier = Modifier.fillMaxWidth().padding(top = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(10.dp)
    ) {
        Text(
            text,
            style = MaterialTheme.typography.titleSmall,
            color = accent,
            fontWeight = FontWeight.Bold
        )
        Spacer(
            Modifier
                .weight(1f)
                .height(1.dp)
                .background(Brush.horizontalGradient(listOf(accent.copy(alpha = 0.5f), Color.Transparent)))
        )
    }
}
