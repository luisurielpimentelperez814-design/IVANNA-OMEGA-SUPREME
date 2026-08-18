package com.ivanna.omega.ui

import android.content.Intent
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.*
import androidx.compose.foundation.*
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.*
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.audio.*
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.dsp.DSPBridge
import com.ivanna.omega.dsp.DSPState
import com.ivanna.omega.magisk.OmegaEngineBridge
import com.ivanna.omega.neuromorphic.IvannaNpeEngine
import com.ivanna.omega.neuromorphic.PiLstmBridge
import com.ivanna.omega.spatial.IvannaSpatialEngine
import com.ivanna.omega.spatial.IvannaSpatialManager
import com.ivanna.omega.spatial.SaFOptimizer
import com.ivanna.omega.spatial.HrtfSubjectSelector
import com.ivanna.omega.ui.theme.*
import kotlin.math.PI
import kotlin.math.roundToInt
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext

// ─── Tokens ────────────────────────────────────────────────────────────────
private val SectionBg   = Color(0xFF0C1220)
private val CardBg      = Color(0xFF111827)
private val Divider     = Color(0xFF1E2D45)
private val Mono        = FontFamily.Monospace

// ─── Tab enum ──────────────────────────────────────────────────────────────
private enum class Tab(val label: String, val icon: androidx.compose.ui.graphics.vector.ImageVector) {
    HOME("HOME", Icons.Filled.RadioButtonChecked),
    SOUND("SOUND", Icons.Filled.Tune),
    SPATIAL("SPATIAL", Icons.Filled.BlurOn),
    BRAIN("BRAIN", Icons.Filled.Psychology),
    PLAYER("PLAYER", Icons.Filled.PlayCircle)
}

// ─── Entry point ────────────────────────────────────────────────────────────
@Composable
fun IvannaAppShell(
    dsp: MutableState<DSPState>,
    adaptiveBackend: AdaptiveBackend,
    voiceProtectionManager: VoiceProtectionManager,
    onRequestCapture: () -> Unit
) {
    val context = LocalContext.current
    var activeTab by remember { mutableStateOf(Tab.HOME) }

    // ── Shared state ──────────────────────────────────────────────────────
    val audioState by AudioStateManager.audioState.collectAsState()
    val isCapturing by PlaybackCaptureService.isCapturing.collectAsState()
    val yamnet by AudioPipeline.sharedYamnetResult.collectAsState()
    val routeState by IvannaUnifiedPipeline.state.collectAsState()
    val adaptiveTelemetryRaw by adaptiveBackend.telemetry.collectAsState()

    val antiDolbyController = remember {
        AntiDolbyController(context).also { it.initialize()
            it.onDspUpdate = { exciter, width, eqGainDb ->
                dsp.value = dsp.value.copy(wet = exciter, stereoWidth = width)
                dsp.value.pushToNative()
                if (IvannaNativeLib.isLoaded)
                    IvannaNativeLib.nativeSetEQParams(eqGainDb, eqGainDb, eqGainDb, dsp.value.master)
            }
        }
    }
    var antiDolbyEnabled by remember { mutableStateOf(false) }
    var npeBypassState   by remember { mutableStateOf(false) }

    val player = remember { IvannaBridgePlayer(context) }
    DisposableEffect(player) {
        MediaSessionManager.init(context, player)
        onDispose { player.release(); MediaSessionManager.release() }
    }
    val playerPositionMs by player.currentPositionMs.collectAsState()
    val playerDurationMs by player.durationMs.collectAsState()
    var playerState by remember { mutableStateOf(player.state) }
    var currentUri  by remember { mutableStateOf<Uri?>(null) }
    var queue       by remember { mutableStateOf<List<Uri>>(emptyList()) }
    var queueIdx    by remember { mutableStateOf(-1) }
    LaunchedEffect(player) {
        player.onQueueAdvance = { nextUri -> currentUri = nextUri; queueIdx = queue.indexOf(nextUri) }
        while (true) { playerState = player.state; delay(100L) }
    }
    val singlePicker = rememberLauncherForActivityResult(ActivityResultContracts.GetContent()) {
        uri -> uri?.let { currentUri = it; queue = listOf(it); queueIdx = 0 }
    }
    val queuePicker = rememberLauncherForActivityResult(ActivityResultContracts.GetMultipleContents()) {
        uris -> if (uris.isNotEmpty()) { queue = uris; queueIdx = 0; currentUri = uris.first() }
    }

    // ── Layout ────────────────────────────────────────────────────────────
    Scaffold(
        containerColor = ObsidianVoid,
        bottomBar = {
            NavigationBar(
                containerColor = Color(0xFF070D18),
                tonalElevation = 0.dp
            ) {
                Tab.values().forEach { tab ->
                    NavigationBarItem(
                        selected  = activeTab == tab,
                        onClick   = { activeTab = tab },
                        icon      = {
                            Icon(tab.icon, contentDescription = null,
                                 modifier = Modifier.size(20.dp))
                        },
                        label     = {
                            Text(tab.label, fontFamily = Mono, fontSize = 9.sp,
                                 letterSpacing = 1.5.sp)
                        },
                        colors = NavigationBarItemDefaults.colors(
                            selectedIconColor   = AuroraCyan,
                            selectedTextColor   = AuroraCyan,
                            unselectedIconColor = TextMuted,
                            unselectedTextColor = TextMuted,
                            indicatorColor      = Color(0xFF0D2035)
                        )
                    )
                }
            }
        }
    ) { paddingValues ->
        Box(Modifier.fillMaxSize().padding(paddingValues)) {
            when (activeTab) {
                Tab.HOME    -> HomeTab(
                    isCapturing         = isCapturing,
                    yamnet              = yamnet,
                    audioState          = audioState,
                    dsp                 = dsp.value,
                    routeState          = routeState,
                    onRequestCapture    = onRequestCapture
                )
                Tab.SOUND   -> SoundTab(
                    dsp               = dsp,
                    npeBypassState    = npeBypassState,
                    onNpeBypassChange = { npeBypassState = it; if (PiLstmBridge.isReady) PiLstmBridge.setBypass(it) }
                )
                Tab.SPATIAL -> SpatialTab(
                    dsp                   = dsp,
                    antiDolbyEnabled      = antiDolbyEnabled,
                    onAntiDolbyChange     = { enabled ->
                        antiDolbyEnabled = enabled
                        if (enabled) antiDolbyController.enableAntiDolby()
                        else antiDolbyController.disableAntiDolby()
                    }
                )
                Tab.BRAIN   -> BrainTab(
                    adaptiveTelemetryRaw = adaptiveTelemetryRaw,
                    yamnet               = yamnet,
                    audioState           = audioState
                )
                Tab.PLAYER  -> PlayerTab(
                    player         = player,
                    playerState    = playerState,
                    currentUri     = currentUri,
                    queue          = queue,
                    queueIdx       = queueIdx,
                    playerPositionMs = playerPositionMs,
                    playerDurationMs = playerDurationMs,
                    onPlayPause    = {
                        when (playerState) {
                            IvannaBridgePlayer.State.PLAYING -> player.pause()
                            IvannaBridgePlayer.State.PAUSED  -> player.resume()
                            else -> currentUri?.let { player.play(it) }
                        }
                    },
                    onPickFile     = { singlePicker.launch("audio/*") },
                    onPickQueue    = { queuePicker.launch("audio/*") }
                )
            }
        }
    }
}

// ════════════════════════════════════════════════════════════════════════════
// HOME TAB — signal overview + capture control
// ════════════════════════════════════════════════════════════════════════════
@Composable
private fun HomeTab(
    isCapturing: Boolean,
    yamnet: AudioPipeline.SharedYamnetResult,
    audioState: AudioState,
    dsp: DSPState,
    routeState: PipelineState,
    onRequestCapture: () -> Unit
) {
    LazyColumn(
        modifier = Modifier.fillMaxSize().background(ObsidianVoid),
        contentPadding = PaddingValues(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        // ── Capture ring hero ───────────────────────────────────────────
        item {
            CaptureHero(isCapturing = isCapturing, onToggle = onRequestCapture)
        }
        // ── YAMNet ─────────────────────────────────────────────────────
        item {
            SectionCard(title = "CLASIFICACIÓN EN TIEMPO REAL") {
                YamnetBarsRow(yamnet = yamnet, isCapturing = isCapturing)
            }
        }
        // ── Engine status ───────────────────────────────────────────────
        item {
            SectionCard(title = "MOTORES ACTIVOS") {
                EngineStatusRow(dsp = dsp)
            }
        }
        // ── Route + Magisk ──────────────────────────────────────────────
        item { MasterControlPanel() }
        item {
            SectionCard(title = "SEÑAL Y DAEMON") {
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    RouteRow(route = routeState)
                    MagiskRow()
                }
            }
        }
    }
}

@Composable
private fun CaptureHero(isCapturing: Boolean, onToggle: () -> Unit) {
    val infinite = rememberInfiniteTransition(label = "capture")
    val pulse by infinite.animateFloat(0.9f, 1f,
        infiniteRepeatable(tween(1800, easing = LinearEasing), RepeatMode.Reverse), "pulse")
    val ringColor by animateColorAsState(
        if (isCapturing) AuroraCyan else ObsidianEdge, label = "ring")

    Box(Modifier.fillMaxWidth(), contentAlignment = Alignment.Center) {
        Canvas(Modifier.size(140.dp)) {
            val r = size.minDimension / 2f * pulse
            drawCircle(color = ringColor.copy(alpha = 0.12f), radius = r)
            drawCircle(color = ringColor, radius = r, style = Stroke(width = 2.dp.toPx()))
            if (isCapturing) {
                drawCircle(color = AuroraCyan.copy(alpha = 0.06f), radius = r * 1.25f)
            }
        }
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Text("Ω", color = if (isCapturing) AuroraCyan else TextMuted,
                fontSize = 36.sp, fontWeight = FontWeight.Thin, fontFamily = Mono)
            Spacer(Modifier.height(4.dp))
            Text(if (isCapturing) "CAPTURANDO" else "EN ESPERA",
                color = if (isCapturing) AuroraCyan else TextMuted,
                fontSize = 10.sp, fontFamily = Mono, letterSpacing = 2.sp)
            Spacer(Modifier.height(12.dp))
            Button(
                onClick = onToggle,
                colors = ButtonDefaults.buttonColors(
                    containerColor = if (isCapturing) NeonMagenta.copy(alpha = 0.15f) else AuroraCyan.copy(alpha = 0.15f),
                    contentColor   = if (isCapturing) NeonMagenta else AuroraCyan
                ),
                border = BorderStroke(1.dp, if (isCapturing) NeonMagenta else AuroraCyan),
                shape  = RoundedCornerShape(8.dp)
            ) {
                Text(if (isCapturing) "DETENER CAPTURA" else "INICIAR CAPTURA",
                    fontFamily = Mono, fontSize = 11.sp, letterSpacing = 1.sp)
            }
        }
    }
}

@Composable
private fun YamnetBarsRow(yamnet: AudioPipeline.SharedYamnetResult, isCapturing: Boolean) {
    if (!yamnet.valid) {
        Row(verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            if (isCapturing) {
                CircularProgressIndicator(Modifier.size(12.dp), strokeWidth = 1.5.dp, color = AuroraCyan)
                Text("Clasificando…", fontSize = 11.sp, color = AuroraCyan, fontFamily = Mono)
            } else {
                Text("Sin señal", fontSize = 11.sp, color = TextMuted, fontFamily = Mono)
            }
        }
        return
    }
    Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        listOf("SPEECH" to yamnet.speech, "MUSIC" to yamnet.music, "BASS" to yamnet.bass)
            .forEach { (label, v) ->
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    Text(label, fontSize = 8.sp, color = TextMuted, fontFamily = Mono, letterSpacing = 1.sp)
                    Spacer(Modifier.height(4.dp))
                    Box(Modifier.width(48.dp).height(4.dp)
                        .background(ObsidianEdge, RoundedCornerShape(2.dp))) {
                        Box(Modifier.fillMaxHeight()
                            .fillMaxWidth(v.coerceIn(0f, 1f))
                            .background(AuroraCyan, RoundedCornerShape(2.dp)))
                    }
                    Spacer(Modifier.height(2.dp))
                    Text("${(v * 100).roundToInt()}%",
                        fontSize = 9.sp, color = TextSecondary, fontFamily = Mono)
                }
            }
    }
}

@Composable
private fun EngineStatusRow(dsp: DSPState) {
    val eqActive = dsp.low != 0f || dsp.mid != 0f || dsp.high != 0f
    Row(horizontalArrangement = Arrangement.spacedBy(12.dp), modifier = Modifier.fillMaxWidth()) {
        listOf(
            "DSP"  to DSPBridge.isLoaded,
            "EQ"   to eqActive,
            "NPE"  to IvannaNpeEngine.isReady,
            "LSTM" to PiLstmBridge.isReady,
            "EVO"  to IvannaNativeLib.isLoaded
        ).forEach { (lbl, active) ->
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                Box(Modifier.size(8.dp).background(
                    if (active) PhosphorGreen else ObsidianEdge, CircleShape))
                Spacer(Modifier.height(4.dp))
                Text(lbl, fontSize = 9.sp, color = if (active) TextSecondary else TextMuted,
                    fontFamily = Mono, letterSpacing = 1.sp)
            }
        }
    }
}

// FIX(compile 2026-08-18): la anotación @Composable estaba duplicada
// literalmente en dos líneas consecutivas. El compilador Kotlin lanza
// "This annotation is not repeatable" en RouteRow y aborta con exit 1
// el step de :app:compileDebugKotlin. La anotación debe aparecer una
// sola vez encima de la función Composable.
@Composable
private fun RouteRow(route: PipelineState) {
    // FIX "Ruta A desconectada": mostrar LED de estado + ruta activa.
    // Antes solo mostraba texto plano — sin indicador visual de conexión.
    // Ahora: LED verde=ROUTE_A/B activa, ámbar=sin señal, con texto.
    val isActive = route.activeRoute != com.ivanna.omega.audio.ActiveRoute.NONE
    val ledColor = when (route.activeRoute) {
        com.ivanna.omega.audio.ActiveRoute.ROUTE_A -> PhosphorGreen
        com.ivanna.omega.audio.ActiveRoute.ROUTE_B -> AuroraCyan
        com.ivanna.omega.audio.ActiveRoute.NONE   -> CoralWarn
    }
    val routeText = when (route.activeRoute) {
        com.ivanna.omega.audio.ActiveRoute.ROUTE_A -> "Ruta A  (IvannaBridgePlayer)"
        com.ivanna.omega.audio.ActiveRoute.ROUTE_B -> "Ruta B  (omega_effect)"
        com.ivanna.omega.audio.ActiveRoute.NONE   -> "Sin señal"
    }
    Row(
        Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text("Ruta de audio", fontSize = 11.sp, color = TextSecondary)
        Row(
            horizontalArrangement = Arrangement.spacedBy(6.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            // LED de estado (idéntico al de MagiskRow para consistencia visual)
            Box(
                Modifier
                    .size(6.dp)
                    .background(ledColor, androidx.compose.foundation.shape.CircleShape)
            )
            Text(
                routeText,
                fontSize = 11.sp,
                color = ledColor,
                fontFamily = Mono
            )
        }
    }
    // Sub-fila de telemetría cuando Ruta A está activa
    if (route.activeRoute == com.ivanna.omega.audio.ActiveRoute.ROUTE_A) {
        Row(
            Modifier.fillMaxWidth().padding(start = 4.dp, top = 2.dp),
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Text(
                "RMS ${"%.2f".format(route.rms)}  Peak ${"%.2f".format(route.peak)}",
                fontSize = 9.sp,
                color = TextMuted,
                fontFamily = Mono
            )
            if (route.adaptiveActive) {
                Text("ADE ✓", fontSize = 9.sp, color = PhosphorGreen, fontFamily = Mono)
            }
        }
    }
}

@Composable
private fun MagiskRow() {
    // FIX Bug-6: remember{} capturaba isConnected UNA vez (siempre false en cold-start)
    // LaunchedEffect + IO polling actualiza el LED cada 3 s
    var connected by remember { mutableStateOf(OmegaEngineBridge.isConnected) }
    LaunchedEffect(Unit) {
        while (true) {
            connected = withContext(Dispatchers.IO) { OmegaEngineBridge.connect() }
            delay(3_000L)
        }
    }
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically) {
        Text("Daemon Magisk", fontSize = 11.sp, color = TextSecondary)
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp),
            verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.size(6.dp).background(
                if (connected) PhosphorGreen else CoralWarn, CircleShape))
            Text(if (connected) "Conectado" else "Sin conexión",
                fontSize = 11.sp, color = if (connected) PhosphorGreen else CoralWarn,
                fontFamily = Mono)
        }
    }
}

// ════════════════════════════════════════════════════════════════════════════
// SOUND TAB — EQ, compressor, exciter, width, NPE
// ════════════════════════════════════════════════════════════════════════════
@Composable
private fun SoundTab(
    dsp: MutableState<DSPState>,
    npeBypassState: Boolean,
    onNpeBypassChange: (Boolean) -> Unit
) {
    val audioState by AudioStateManager.audioState.collectAsState()

    fun pushEq(low: Float, mid: Float, high: Float) {
        dsp.value = dsp.value.copy(low = low, mid = mid, high = high)
        dsp.value.pushToNative()
        if (IvannaNativeLib.isLoaded)
            IvannaNativeLib.nativeSetEQParams(low, mid, high, dsp.value.master)
    }

    LazyColumn(
        Modifier.fillMaxSize().background(ObsidianVoid),
        contentPadding = PaddingValues(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        // ── 3-band EQ ──────────────────────────────────────────────────
        item {
            SectionCard("ECUALIZADOR") {
                Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                    DspSlider("Graves",   dsp.value.low,    -18f, 18f, "dB") { pushEq(it, dsp.value.mid, dsp.value.high) }
                    DspSlider("Medios",   dsp.value.mid,    -18f, 18f, "dB") { pushEq(dsp.value.low, it, dsp.value.high) }
                    DspSlider("Agudos",   dsp.value.high,   -18f, 18f, "dB") { pushEq(dsp.value.low, dsp.value.mid, it) }
                    DspSlider("Presencia",dsp.value.presence,-12f, 12f, "dB") {
                        dsp.value = dsp.value.copy(presence = it); dsp.value.pushToNative()
                    }
                }
            }
        }
        // ── Compressor ─────────────────────────────────────────────────
        item {
            SectionCard("COMPRESOR") {
                Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                    DspSlider("Umbral",  audioState.compressorThreshold, -60f, 0f, "dB") {
                        AudioStateManager.updateState { s -> s.copy(compressorThreshold = it) }
                        if (IvannaNativeLib.isLoaded) IvannaNativeLib.nativeSetCompressorParams(
                            it, audioState.compressorRatio, audioState.compressorAttack, audioState.compressorRelease)
                    }
                    DspSlider("Ratio",   audioState.compressorRatio, 1f, 20f, ":1") {
                        AudioStateManager.updateState { s -> s.copy(compressorRatio = it) }
                        if (IvannaNativeLib.isLoaded) IvannaNativeLib.nativeSetCompressorParams(
                            audioState.compressorThreshold, it, audioState.compressorAttack, audioState.compressorRelease)
                    }
                    DspSlider("Ataque",  audioState.compressorAttack, 0.1f, 200f, "ms") {
                        AudioStateManager.updateState { s -> s.copy(compressorAttack = it) }
                    }
                    DspSlider("Release", audioState.compressorRelease, 10f, 2000f, "ms") {
                        AudioStateManager.updateState { s -> s.copy(compressorRelease = it) }
                    }
                }
            }
        }
        // ── Exciter + Width ────────────────────────────────────────────
        item {
            SectionCard("COLOR ARMÓNICO + ANCHO") {
                Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                    DspSlider("Exciter",     dsp.value.wet, 0f, 1f, "") {
                        dsp.value = dsp.value.copy(wet = it); dsp.value.pushToNative()
                    }
                    DspSlider("Ancho estéreo", dsp.value.stereoWidth, 0f, 2f, "×") {
                        dsp.value = dsp.value.copy(stereoWidth = it); dsp.value.pushToNative()
                        if (IvannaNativeLib.isLoaded) IvannaNativeLib.nativeSetSpatialWidthDirect(it)
                    }
                    DspSlider("Ganancia", dsp.value.master, 0.1f, 2f, "×") {
                        dsp.value = dsp.value.copy(master = it); dsp.value.pushToNative()
                    }
                }
            }
        }
        // ── NPE ────────────────────────────────────────────────────────
        item {
            SectionCard("MOTOR NEUROMORFO (NPE)") {
                Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically) {
                        Column {
                            Text("Bypass NPE", fontSize = 12.sp, color = TextPrimary)
                            Text(if (npeBypassState) "Desactivado" else "Activo",
                                fontSize = 10.sp, color = if (npeBypassState) CoralWarn else PhosphorGreen,
                                fontFamily = Mono)
                        }
                        Switch(checked = !npeBypassState,
                            onCheckedChange = { onNpeBypassChange(!it) },
                            colors = SwitchDefaults.colors(checkedThumbColor = AuroraCyan,
                                checkedTrackColor = AuroraCyan.copy(alpha = 0.3f)))
                    }
                    if (!npeBypassState && PiLstmBridge.isReady) {
                        DspSlider("Ganancia armónica", 0.5f, 0f, 1f, "") {
                            PiLstmBridge.setHarmonicGain(it)
                        }
                        DspSlider("Compresión coclear", 0.5f, 0f, 1f, "") {
                            PiLstmBridge.setAlpha(it)
                        }
                    } else {
                        Text(if (PiLstmBridge.isReady) "NPE en bypass" else "NPE no disponible",
                            fontSize = 10.sp, color = TextMuted, fontFamily = Mono)
                    }
                }
            }
        }
    }
}

// ════════════════════════════════════════════════════════════════════════════
// SPATIAL TAB — HRTF, azimuth, cinematic
// ════════════════════════════════════════════════════════════════════════════
@Composable
private fun SpatialTab(
    dsp: MutableState<DSPState>,
    antiDolbyEnabled: Boolean,
    onAntiDolbyChange: (Boolean) -> Unit
) {
    var showSaFCalib by remember { mutableStateOf(false) }
    val safState by com.ivanna.omega.spatial.SaFOptimizer.state.collectAsState()
    val rendererHandle = com.ivanna.omega.spatial.IvannaSpatialManager.rendererHandle

    // Overlay fullscreen de calibración — se superpone sobre el tab cuando está activo
    if (showSaFCalib) {
        SaFCalibrationScreen(
            rendererHandle = rendererHandle,
            onDismiss = { showSaFCalib = false }
        )
        return
    }

    var azimuthNorm by remember { mutableStateOf(0.5f) }  // 0=izq, 0.5=centro, 1=der
    var spatialWidth by remember { mutableStateOf(1.0f) }
    var spatialEnabled by remember { mutableStateOf(IvannaSpatialEngine.enabled) }
    val hrtfSubject = IvannaSpatialManager.activeSubject

    LazyColumn(
        Modifier.fillMaxSize().background(ObsidianVoid),
        contentPadding = PaddingValues(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        // ── HRTF status ─────────────────────────────────────────────────
        item {
            SectionCard("PERFIL HRTF ACTIVO") {
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically) {
                    Column {
                        Text("Sujeto CIPIC", fontSize = 11.sp, color = TextSecondary)
                        Text(if (hrtfSubject == "none") "Sin cargar" else hrtfSubject.uppercase(),
                            fontSize = 16.sp, color = AuroraCyan, fontFamily = Mono,
                            fontWeight = FontWeight.Bold)
                        Text("1250 dir · 512 taps · 48 kHz", fontSize = 9.sp, color = TextMuted, fontFamily = Mono)
                    }
                    Box(Modifier.size(10.dp).background(
                        if (IvannaSpatialManager.ready) PhosphorGreen else ObsidianEdge, CircleShape))
                }
            }
        }
        // ── Spatial engine toggle + azimuth ─────────────────────────────
        item {
            SectionCard("MOTOR ESPACIAL 3D") {
                Column(verticalArrangement = Arrangement.spacedBy(14.dp)) {
                    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically) {
                        Text("Motor espacial", fontSize = 12.sp, color = TextPrimary)
                        Switch(checked = spatialEnabled,
                            onCheckedChange = { spatialEnabled = it; IvannaSpatialEngine.enabled = it },
                            colors = SwitchDefaults.colors(checkedThumbColor = AuroraCyan,
                                checkedTrackColor = AuroraCyan.copy(alpha = 0.3f)))
                    }
                    // Azimuth joystick horizontal
                    Column {
                        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                            Text("Azimut", fontSize = 11.sp, color = TextSecondary)
                            val az = ((azimuthNorm - 0.5f) * 360f).roundToInt()
                            Text("${if (az >= 0) "+" else ""}$az°",
                                fontSize = 11.sp, color = AuroraCyan, fontFamily = Mono)
                        }
                        Slider(value = azimuthNorm, onValueChange = {
                            azimuthNorm = it
                            val rad = (it - 0.5f) * 2f * PI.toFloat()
                            IvannaSpatialEngine.setAzimuth(rad)
                            if (IvannaNativeLib.isLoaded) IvannaNativeLib.nativeSetSpatialAngleRad(rad)
                        }, valueRange = 0f..1f,
                            colors = SliderDefaults.colors(thumbColor = AuroraCyan,
                                activeTrackColor = AuroraCyan))
                    }
                    DspSlider("Ancho espacial", spatialWidth, 0f, 1.5f, "×") {
                        spatialWidth = it
                        IvannaSpatialEngine.setWidth(it)
                        if (IvannaNativeLib.isLoaded) IvannaNativeLib.nativeSetSpatialWidthDirect(it)
                    }
                }
            }
        }
        // ── Φ_SAF^∞ Calibración HRTF ────────────────────────────────────
        item {
            SectionCard("CALIBRACIÓN Φ_SAF^∞ · RIEMANNIANO") {
                Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    Row(Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically) {
                        Column {
                            Text("Optimizador Riemanniano", fontSize = 12.sp, color = TextPrimary)
                            Text("214 sujetos CIPIC · MIT · ARI · TU-Berlin",
                                fontSize = 9.sp, color = TextMuted)
                        }
                        Text(if (safState.iteration > 0)
                                 "It.${safState.iteration} · ${safState.selectedSubject}"
                             else "No calibrado",
                            fontSize = 9.sp, color = AuroraCyan, fontFamily = Mono)
                    }
                    if (safState.iteration > 0) {
                        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                            Text("‖p_t‖ ${"%.2f".format(safState.paramNorm)}",
                                fontSize = 9.sp, color = TextMuted, fontFamily = Mono)
                            Text("E ${"%.3f".format(safState.errorEnergy)}",
                                fontSize = 9.sp, color = TextMuted, fontFamily = Mono)
                        }
                    }
                    Button(
                        onClick = { showSaFCalib = true },
                        modifier = Modifier.fillMaxWidth().height(46.dp),
                        colors = ButtonDefaults.buttonColors(
                            containerColor = NeonMagenta.copy(alpha = 0.12f),
                            contentColor   = NeonMagenta),
                        border = BorderStroke(1.dp, NeonMagenta),
                        shape  = RoundedCornerShape(10.dp)
                    ) {
                        Text(
                            if (safState.iteration == 0) "CALIBRAR HRTF (5 DIR.)"
                            else "RE-CALIBRAR",
                            fontFamily = Mono, fontSize = 11.sp, letterSpacing = 1.5.sp
                        )
                    }
                }
            }
        }

        // ── Anti-Dolby / Cinematic ───────────────────────────────────────
        item {
            SectionCard("MOTOR CINEMÁTICO (ANTI-DOLBY)") {
                Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically) {
                        Column {
                            Text("Anti-Dolby CRNN", fontSize = 12.sp, color = TextPrimary)
                            Text("Clasificación en tiempo real + adaptación de modo",
                                fontSize = 9.sp, color = TextMuted)
                        }
                        Switch(checked = antiDolbyEnabled, onCheckedChange = onAntiDolbyChange,
                            colors = SwitchDefaults.colors(checkedThumbColor = NeonMagenta,
                                checkedTrackColor = NeonMagenta.copy(alpha = 0.3f)))
                    }
                    if (antiDolbyEnabled) {
                        InfoChip("Motor cinemático activo — adaptando modo según contenido", NeonMagenta)
                    }
                }
            }
        }
    }
}

// ════════════════════════════════════════════════════════════════════════════
// BRAIN TAB — perceptual engine + adaptive
// ════════════════════════════════════════════════════════════════════════════
@Composable
private fun BrainTab(
    adaptiveTelemetryRaw: com.ivanna.omega.audio.AdaptiveTelemetry,
    yamnet: AudioPipeline.SharedYamnetResult,
    audioState: AudioState
) {
    val perceptualEngine = remember { com.ivanna.omega.ai.PerceptualBrainEngine() }
    val snapshot by perceptualEngine.snapshot.collectAsState()
    var evoBestFitness by remember { mutableStateOf<Float?>(null) }
    var evoGeneration  by remember { mutableStateOf<Int?>(null) }

    // FIX (diagnóstico exacto): la sección ENERGÍA ESPECTRAL existía en el
    // nav viejo (IvannaNavigation.kt) pero nunca se trasladó al BrainTab del
    // shell nuevo. Los atomics g_lastBandLow/Mid/High se escriben en
    // nativeProcess (JNI vivo), simplemente nadie los leía desde el tab
    // activo. Aquí se restablece el polling a 5 Hz (el mismo ritmo que usaba
    // AdaptiveDashboard antes) y se alimenta la card nueva de abajo.
    var bandEnergies by remember { mutableStateOf<FloatArray?>(null) }

    // Arrancar el motor perceptual (polling 10 Hz) y limpiarlo al salir
    DisposableEffect(perceptualEngine) {
        perceptualEngine.start()
        onDispose { perceptualEngine.stop() }
    }

    LaunchedEffect(Unit) {
        while (true) {
            if (IvannaNativeLib.isLoaded) {
                evoBestFitness = runCatching { IvannaNativeLib.nativeGetEvoBestFitness() }.getOrNull()
                // FIX (build): nativeGetEvoGeneration no existe — el nombre real
                // en IvannaNativeLib es nativeGetGeneration (ver línea 68 del
                // JNI wrapper). Devuelve Int; getOrNull() lo mantiene Int?.
                evoGeneration  = runCatching { IvannaNativeLib.nativeGetGeneration() }.getOrNull()
            }
            delay(2000)
        }
    }

    // Polling dedicado de energía espectral (5 Hz) — separado del ciclo
    // evolutivo (2 s) para que las barras LOW/MID/HIGH se sientan vivas
    // sin bloquear el sondeo del kernel. Lee los atomics g_lastBandLow/
    // Mid/High vía nativeGetBandEnergies() → FloatArray[3].
    LaunchedEffect(Unit) {
        while (true) {
            if (IvannaNativeLib.isLoaded) {
                bandEnergies = runCatching { IvannaNativeLib.nativeGetBandEnergies() }.getOrNull()
            }
            delay(200)
        }
    }

    LazyColumn(
        Modifier.fillMaxSize().background(ObsidianVoid),
        contentPadding = PaddingValues(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        // ── Perceptual Brain ───────────────────────────────────────────
        item {
            SectionCard("CEREBRO PERCEPTUAL") {
                Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    PerceptualBar("Fatiga auditiva",  snapshot.fatigue,    CoralWarn)
                    PerceptualBar("Inmersión",        snapshot.immersion,   AuroraCyan)
                    PerceptualBar("Confianza CRNN",   snapshot.convNextConfidence, PhosphorGreen)
                    if (snapshot.dominantClassLabel.isNotBlank()) {
                        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                            Text("Contenido detectado", fontSize = 11.sp, color = TextSecondary)
                            Text(snapshot.dominantClassLabel, fontSize = 11.sp,
                                color = AmberSignal, fontFamily = Mono)
                        }
                    }
                }
            }
        }
        // ── Band energy (read-only, ADE inputs) ────────────────────────
        // Band energy — datos reales del ADE (IIR bandpass en PDEngine + omega_daemon)
        //   Ruta A: BiquadEnvelopeBank de PDEngine (bandas 0-1 low, 2-4 mid, 5-7 high)
        //   Ruta B: 3 filtros IIR en omega_daemon::processLoop (ai_band_low/mid/high)
        // Backend real: nativeGetBandEnergies() → FloatArray[3] alimentado por
        // los atomics g_lastBandLow/Mid/High que se escriben en nativeProcess.
        item {
            SectionCard("ENERGÍA ESPECTRAL") {
                Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    val bandLow  = bandEnergies?.getOrElse(0) { 0f } ?: 0f
                    val bandMid  = bandEnergies?.getOrElse(1) { 0f } ?: 0f
                    val bandHigh = bandEnergies?.getOrElse(2) { 0f } ?: 0f
                    val total    = (bandLow + bandMid + bandHigh).coerceAtLeast(1e-6f)
                    val lowPct   = if (bandEnergies != null) (bandLow  / total * 100).roundToInt() else 0
                    val midPct   = if (bandEnergies != null) (bandMid  / total * 100).roundToInt() else 0
                    val highPct  = if (bandEnergies != null) (bandHigh / total * 100).roundToInt() else 0

                    // Normalizamos cada barra respecto al total (no al máx
                    // absoluto): así la card muestra el reparto espectral
                    // real y no depende del volumen.
                    PerceptualBar("LOW  \u2264 250 Hz",     bandLow  / total, AmberSignal)
                    PerceptualBar("MID  250\u20134 kHz",     bandMid  / total, AuroraCyan)
                    PerceptualBar("HIGH \u2265 4 kHz",        bandHigh / total, NeonMagenta)

                    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                        Text("Reparto", fontSize = 11.sp, color = TextSecondary)
                        Text("L $lowPct%  \u2022  M $midPct%  \u2022  H $highPct%",
                            fontSize = 11.sp, color = TextPrimary, fontFamily = Mono)
                    }

                    if (bandEnergies == null) {
                        Text("Sin señal de audio activa",
                            fontSize = 10.sp, color = TextMuted, fontFamily = Mono)
                    }
                }
            }
        }
        // ── Adaptive engine ───────────────────────────────────
        item {
            SectionCard("MOTOR ADAPTATIVO") {
                Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    val mode = audioState.adaptiveMode
                    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                        Text("Modo activo", fontSize = 11.sp, color = TextSecondary)
                        Text(mode.name, fontSize = 11.sp, color = AuroraCyan, fontFamily = Mono)
                    }
                    val telSnap = adaptiveTelemetryRaw.toSnapshot()
                    listOf(
                        "Ganancia objetivo" to "%.2f".format(telSnap.targetGain),
                        // FIX (build): AdaptiveTelemetrySnapshot expone
                        // compressorAmount y rms (no compAmount/inputRms) —
                        // ver data class en AdaptiveEngineCard.kt:58.
                        "Comp aplicado"     to "%.2f".format(telSnap.compressorAmount),
                        "RMS entrada"       to "%.1f dB".format(telSnap.rms)
                    ).forEach { (label, value) ->
                        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                            Text(label, fontSize = 11.sp, color = TextSecondary)
                            Text(value, fontSize = 11.sp, color = TextPrimary, fontFamily = Mono)
                        }
                    }
                }
            }
        }
        // ── Evolutionary kernel ─────────────────────────────────────────
        item {
            SectionCard("KERNEL EVOLUTIVO") {
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                        Text("Generación", fontSize = 11.sp, color = TextSecondary)
                        Text(evoGeneration?.toString() ?: "—",
                            fontSize = 11.sp, color = AuroraCyan, fontFamily = Mono)
                    }
                    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                        Text("Mejor fitness", fontSize = 11.sp, color = TextSecondary)
                        Text(evoBestFitness?.let { "%.4f".format(it) } ?: "—",
                            fontSize = 11.sp, color = PhosphorGreen, fontFamily = Mono)
                    }
                    if (!IvannaNativeLib.isLoaded) {
                        InfoChip("Librería nativa no disponible", CoralWarn)
                    }
                }
            }
        }
        // ── PiLSTM ─────────────────────────────────────────────────────
        item {
            SectionCard("PI-LSTM NEURO-COGNITIVO") {
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically) {
                    Text("Estado", fontSize = 11.sp, color = TextSecondary)
                    Row(horizontalArrangement = Arrangement.spacedBy(6.dp),
                        verticalAlignment = Alignment.CenterVertically) {
                        Box(Modifier.size(6.dp).background(
                            if (PiLstmBridge.isReady) PhosphorGreen else TextMuted, CircleShape))
                        Text(if (PiLstmBridge.isReady) "Listo" else "No inicializado",
                            fontSize = 11.sp, color = if (PiLstmBridge.isReady) PhosphorGreen else TextMuted,
                            fontFamily = Mono)
                    }
                }
            }
        }
    }
}

@Composable
private fun PerceptualBar(label: String, value: Float, color: Color) {
    Column {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            Text(label, fontSize = 11.sp, color = TextSecondary)
            Text("${(value * 100).roundToInt()}%", fontSize = 11.sp, color = color, fontFamily = Mono)
        }
        Spacer(Modifier.height(4.dp))
        Box(Modifier.fillMaxWidth().height(3.dp).background(ObsidianEdge, RoundedCornerShape(1.5.dp))) {
            Box(Modifier.fillMaxHeight().fillMaxWidth(value.coerceIn(0f, 1f))
                .background(color, RoundedCornerShape(1.5.dp)))
        }
    }
}

// ════════════════════════════════════════════════════════════════════════════
// PLAYER TAB — local file playback
// ════════════════════════════════════════════════════════════════════════════
@Composable
private fun PlayerTab(
    player: IvannaBridgePlayer,
    playerState: IvannaBridgePlayer.State,
    currentUri: Uri?,
    queue: List<Uri>,
    queueIdx: Int,
    playerPositionMs: Long,
    playerDurationMs: Long,
    onPlayPause: () -> Unit,
    onPickFile: () -> Unit,
    onPickQueue: () -> Unit
) {
    LazyColumn(
        Modifier.fillMaxSize().background(ObsidianVoid),
        contentPadding = PaddingValues(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        item {
            SectionCard("REPRODUCTOR LOCAL") {
                BridgePlayerCard(
                    playerState      = playerState,
                    currentUri       = currentUri,
                    onPickFile       = onPickFile,
                    onPlay           = { currentUri?.let { player.play(it) } },
                    onPause          = { player.pause() },
                    onResume         = { player.resume() },
                    onStop           = { player.stop() },
                    currentPositionMs= playerPositionMs,
                    durationMs       = playerDurationMs,
                    onSeek           = { player.seekTo(it) },
                    queue            = queue,
                    queueIndex       = queueIdx,
                    onPickQueue      = onPickQueue
                )
            }
        }
        if (queue.isNotEmpty()) {
            item {
                SectionCard("COLA (${queue.size})") {
                    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                        queue.forEachIndexed { i, uri ->
                            val name = uri.lastPathSegment?.substringAfterLast("/") ?: uri.toString()
                            Row(Modifier.fillMaxWidth()
                                .background(if (i == queueIdx) AuroraCyan.copy(alpha = 0.08f) else Color.Transparent,
                                    RoundedCornerShape(6.dp))
                                .padding(horizontal = 8.dp, vertical = 6.dp),
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                Text("${i + 1}", fontSize = 10.sp, color = TextMuted, fontFamily = Mono,
                                    modifier = Modifier.width(20.dp))
                                Text(name, fontSize = 11.sp, color = if (i == queueIdx) AuroraCyan else TextSecondary,
                                    maxLines = 1, modifier = Modifier.weight(1f))
                            }
                        }
                    }
                }
            }
        }
    }
}

// ════════════════════════════════════════════════════════════════════════════
// SHARED COMPONENTS
// ════════════════════════════════════════════════════════════════════════════
@Composable
private fun SectionCard(title: String, content: @Composable ColumnScope.() -> Unit) {
    Column(
        Modifier.fillMaxWidth()
            .background(CardBg, RoundedCornerShape(12.dp))
            .border(1.dp, Divider, RoundedCornerShape(12.dp))
            .padding(16.dp)
    ) {
        Text(title, fontSize = 10.sp, color = TextMuted, fontFamily = Mono,
            letterSpacing = 2.sp, fontWeight = FontWeight.Medium)
        Spacer(Modifier.height(12.dp))
        content()
    }
}

@Composable
private fun DspSlider(
    label: String, value: Float, min: Float, max: Float, unit: String,
    onValueChange: (Float) -> Unit
) {
    Column {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            Text(label, fontSize = 11.sp, color = TextSecondary)
            val display = if (unit == "dB" || unit == "") {
                "${if (value >= 0 && unit == "dB") "+" else ""}${"%.1f".format(value)}$unit"
            } else { "${"%.2f".format(value)} $unit" }
            Text(display, fontSize = 11.sp, color = TextPrimary, fontFamily = Mono)
        }
        Slider(value = value, onValueChange = onValueChange,
            valueRange = min..max,
            colors = SliderDefaults.colors(thumbColor = AuroraCyan, activeTrackColor = AuroraCyan,
                inactiveTrackColor = ObsidianEdge))
    }
}

@Composable
private fun InfoChip(text: String, color: Color) {
    Box(Modifier.fillMaxWidth()
        .background(color.copy(alpha = 0.1f), RoundedCornerShape(6.dp))
        .border(1.dp, color.copy(alpha = 0.3f), RoundedCornerShape(6.dp))
        .padding(horizontal = 10.dp, vertical = 6.dp)) {
        Text(text, fontSize = 10.sp, color = color, fontFamily = Mono)
    }
}
