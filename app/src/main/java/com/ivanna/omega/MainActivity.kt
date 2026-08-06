package com.ivanna.omega

import com.ivanna.omega.spatial.IvannaSpatialEngine
import android.media.AudioManager
import android.util.Log
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.livedata.observeAsState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import com.ivanna.omega.audio.AdaptiveBackend
import com.ivanna.omega.audio.AudioState
import com.ivanna.omega.audio.AudioStateManager
import com.ivanna.omega.audio.AdaptiveMode
import com.ivanna.omega.neuromorphic.IvannaDspManager
import com.ivanna.omega.ui.AdaptiveTelemetrySnapshot
import com.ivanna.omega.audio.toSnapshot
import com.ivanna.omega.audio.ProfilesLoader
import com.ivanna.omega.neuromorphic.PiLstmBridge
import com.ivanna.omega.magisk.OmegaEngineBridge
import com.ivanna.omega.ui.MagiskStatusPanel
import com.ivanna.omega.ui.ProfileSelectorScreen
import com.ivanna.omega.audio.VoiceProtectionManager
import com.ivanna.omega.core.ParameterStore
import com.ivanna.omega.ui.AdaptiveEngineScreen
import com.ivanna.omega.ui.IvannaControlPanel
import com.ivanna.omega.audio.AntiDolbyController
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.core.OmegaEngine
import com.ivanna.omega.dsp.DSPBridge
import com.ivanna.omega.dsp.DSPState
import android.app.Activity
import android.content.Intent
import android.media.projection.MediaProjectionManager
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import com.ivanna.omega.audio.PlaybackCaptureService
import kotlin.math.PI
import android.net.Uri
import com.ivanna.omega.audio.IvannaBridgePlayer
import com.ivanna.omega.audio.VolterraSwitch
import com.ivanna.omega.ui.IvannaRoute
import com.ivanna.omega.ui.SoundScreen
import com.ivanna.omega.ui.BrainScreen
import com.ivanna.omega.ui.SystemScreen
import com.ivanna.omega.ui.BridgePlayerCard
import kotlin.math.log10
import kotlinx.coroutines.delay
import com.ivanna.omega.magisk.ShmManager
import com.ivanna.omega.core.PresetManager
import com.ivanna.omega.audio.AudioRoutingManager
import com.ivanna.omega.audio.ParameterValidator
import com.ivanna.omega.ai.RealtimeLearningController
import com.ivanna.omega.audio.AudioPipeline
import com.ivanna.omega.ai.PerceptualBrainEngine
import com.ivanna.omega.ai.PerceptualSnapshot
import com.ivanna.omega.ui.PerceptualBrainDashboard
import com.ivanna.omega.ui.SaFCalibrationScreen
import com.ivanna.omega.ui.MainScaffold

// ── Palette (FUSION-PRO dark theme) ──────────────────────────────────────────
private val Carbon = Color(0xFF0A0A0A)
private val Surface1 = Color(0xFF111111)
private val Surface2 = Color(0xFF181818)
private val Border1 = Color(0xFF222222)
private val CyanGlow = Color(0xFF00F5FF)
private val CyanDim = Color(0x3300F5FF)
private val GoldGlow = Color(0xFFFFD700)
private val MagentaGlow = Color(0xFFFF00FF)
private val NeonMagenta  = Color(0xFFFF00FF)
private val MagentaDim = Color(0x33FF00FF)
private val TextPri = Color(0xFFFFFFFF)
private val TextSec = Color(0xFF888888)
private val TextMid = Color(0xFFCCCCCC)

/**
 * PerceptualDspRecommendations - Computed DSP output parameters from Perceptual Brain evaluation.
 */
data class PerceptualDspRecommendations(
    val compressorAmount: Float = 0.35f,
    val exciterReduction: Float = 0.15f,
    val eqHighCut: Float = 18000.0f,
    val spatialWidth: Float = 1.20f,
    val loudnessTarget: Float = -14.0f
)

/**
 * PerceptualDecisionEngine - Real-time Psychoacoustic & TinyML decision controller.
 * Evaluates fatigue, immersion, ISO 226 loudness, masking efficiency, dynamic range,
 * and ConvNeXt INT8 confidence to produce real-time native DSP adjustments.
 */
class PerceptualDecisionEngine {
    fun evaluate(snapshot: PerceptualSnapshot): PerceptualDspRecommendations {
        val fatigueFactor = snapshot.fatigue.coerceIn(0f, 1f)
        val immersionFactor = snapshot.immersion.coerceIn(0f, 1f)
        val loudnessIso226 = snapshot.iso226LoudnessDb
        val maskingEff = snapshot.maskingEfficiency
        val dynRange = snapshot.dynamicRangeDb
        val confidence = snapshot.convNextConfidence

        // Psychoacoustic rules calculation
        val exciterRed = (fatigueFactor * 0.5f + (1.0f - confidence) * 0.2f).coerceIn(0f, 0.8f)
        val highCut = if (fatigueFactor > 0.5f) (18000f - fatigueFactor * 4000f) else 20000f
        val compAmount = (0.2f + (1.0f - (dynRange / 24f).coerceIn(0f, 1f)) * 0.5f + fatigueFactor * 0.2f).coerceIn(0.1f, 0.9f)
        val width = (immersionFactor * 1.4f * (1.0f - fatigueFactor * 0.3f)).coerceIn(0.5f, 1.8f)
        val targetLoudness = (-14.0f + (loudnessIso226 - 84f) * 0.1f + maskingEff * 2.0f).coerceIn(-24.0f, -8.0f)

        return PerceptualDspRecommendations(
            compressorAmount = compAmount,
            exciterReduction = exciterRed,
            eqHighCut = highCut,
            spatialWidth = width,
            loudnessTarget = targetLoudness
        )
    }

    fun dispatchRecommendations(recommendations: PerceptualDspRecommendations) {
        if (IvannaNativeLib.isLoaded) {
            runCatching {
                IvannaNativeLib.nativeSetSpatialWidthDirect(recommendations.spatialWidth)
                IvannaNativeLib.nativeSetHarmonicGain(1.0f - recommendations.exciterReduction)
                IvannaNativeLib.nativeSetAntiDolbyIntensity(recommendations.compressorAmount)
            }
        }
    }
}

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        val am = getSystemService(AUDIO_SERVICE) as AudioManager
        val sr = am.getProperty(AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE)?.toIntOrNull() ?: 48000
        DSPBridge.init(sr)
        if (IvannaNativeLib.isLoaded) {
            runCatching { IvannaNativeLib.nativeInitDSP(sr) }
            IvannaNativeLib.nativeStartEvoThread()
        }
        setContent { OmegaApp() }
    }

    override fun onDestroy() {
        super.onDestroy()
        if (IvannaNativeLib.isLoaded) {
            runCatching { IvannaNativeLib.nativeSaveEvoState() }
            runCatching { IvannaNativeLib.nativeDestroyAdaptiveEngine() }
        }
    }
}

@Composable
fun OmegaApp() {
    val nav = rememberNavController()
    val dsp = remember { mutableStateOf(DSPState()) }
    MaterialTheme(colorScheme = darkColorScheme(background = Carbon, surface = Surface1)) {
        val context = LocalContext.current
        var captureRequested by remember { mutableStateOf(false) }
        val captureActive by PlaybackCaptureService.isCapturing.collectAsState()
        val projectionManager = context.getSystemService(MediaProjectionManager::class.java)
        val projectionLauncher = rememberLauncherForActivityResult(
            ActivityResultContracts.StartActivityForResult()
        ) { result ->
            if (result.resultCode == Activity.RESULT_OK && result.data != null) {
                val intent = Intent(context, PlaybackCaptureService::class.java).apply {
                    putExtra("resultCode", result.resultCode)
                    putExtra("data", result.data)
                }
                context.startForegroundService(intent)
                captureRequested = true
            }
        }

        val adaptiveBackend = remember { AdaptiveBackend(context) }
        val voiceProtectionManager = remember {
            com.ivanna.omega.audio.VoiceProtectionManager(
                com.ivanna.omega.audio.ParameterStore(context)
            )
        }
        var pendingBandProfileId by remember { mutableStateOf<String?>(null) }
        NavHost(nav, startDestination = "splash") {
            composable("splash") { SplashScreen { nav.navigate("intro") } }
            composable("intro") {
                IntroScreen { profileId ->
                    pendingBandProfileId = profileId
                    nav.navigate("dashboard")
                }
            }
            composable("dashboard") {
                LaunchedEffect(Unit) {
                    if (!captureActive) {
                        projectionLauncher.launch(projectionManager.createScreenCaptureIntent())
                    }
                }
                LaunchedEffect(pendingBandProfileId) {
                    val profileId = pendingBandProfileId ?: return@LaunchedEffect
                    val profile = ProfilesLoader.load(context).find { it.id == profileId }
                    if (profile != null && !profile.audioEngine.bypass) {
                        dsp.value = dsp.value.copy(
                            master = profile.audioEngine.gain,
                            wet = profile.audioEngine.exciterAmount,
                            low = profile.audioEngine.eqGain,
                            mid = profile.audioEngine.eqGain,
                            high = profile.audioEngine.eqGain,
                            presence = profile.audioEngine.eqGain,
                            stereoWidth = profile.audioEngine.widthAmount
                        )
                        dsp.value.pushToNative()
                    }
                    pendingBandProfileId = null
                }
                MainScaffold(
                    outerNav     = nav,
                    dsp          = dsp,
                    adaptiveBack = adaptiveBackend,
                    voiceMgr     = voiceProtectionManager
                )
            }
            composable("magisk") {
                MagiskStatusPanel(
                    omegaBridge = OmegaEngineBridge,
                    onBack = { nav.popBackStack() }
                )
            }
            composable("profiles") {
                val context = LocalContext.current
                val profiles = remember { ProfilesLoader.load(context) }
                val metadata = remember { ProfilesLoader.loadMetadata(context) }
                val profileBridge = remember { ProfileManagerBridge(context) }
                val profileStore = remember { com.ivanna.omega.core.ParameterStore(context) }
                var activeProfileId by remember {
                    mutableStateOf(profileStore.getCurrentAudioProfileId())
                }
                LaunchedEffect(Unit) {
                    val saved = profileStore.getCurrentAudioProfileId() ?: return@LaunchedEffect
                    val profile = profiles.find { it.id == saved } ?: return@LaunchedEffect
                    profileBridge.applyProfile(profile.id, dsp.value) { updatedDsp ->
                        dsp.value = updatedDsp
                    }
                }
                ProfileSelectorScreen(
                    profiles = profiles,
                    metadata = metadata,
                    currentId = activeProfileId,
                    onApply = { profile ->
                        profileBridge.applyProfile(profile.id, dsp.value) { updatedDsp ->
                            dsp.value = updatedDsp
                        }
                        activeProfileId = profile.id
                        profileStore.setCurrentAudioProfileId(profile.id)
                    },
                    onClose = { nav.popBackStack() }
                )
            }
            composable("visualizer") {
                LaunchedEffect(Unit) {
                    projectionLauncher.launch(
                        projectionManager.createScreenCaptureIntent()
                    )
                }
            }
            composable("adaptive") {
                DisposableEffect(Unit) {
                    if (IvannaNativeLib.isLoaded)
                        runCatching { IvannaNativeLib.nativeSetAdaptiveEngineEnabled(false) }
                    onDispose {
                        if (IvannaNativeLib.isLoaded)
                            runCatching { IvannaNativeLib.nativeSetAdaptiveEngineEnabled(true) }
                    }
                }
                com.ivanna.omega.ui.AdaptiveEngineScreen(
                    voiceProtectionManager = voiceProtectionManager,
                    backend = adaptiveBackend,
                    modifier = Modifier
                        .fillMaxSize()
                        .background(Carbon)
                        .windowInsetsPadding(WindowInsets.systemBars)
                )
            }
            composable("telemetry") {
                com.ivanna.omega.ui.TelemetryDashboard(
                    modifier = Modifier.fillMaxSize().background(Carbon)
                        .windowInsetsPadding(WindowInsets.systemBars)
                )
            }
            composable("ope") {
                com.ivanna.omega.ui.OpeEngineScreen(
                    modifier = Modifier.fillMaxSize().background(Carbon)
                        .windowInsetsPadding(WindowInsets.systemBars)
                )
            }
            composable("binaural") {
                com.ivanna.omega.ui.BinauralScreen(
                    modifier = Modifier.fillMaxSize().background(Carbon)
                        .windowInsetsPadding(WindowInsets.systemBars)
                )
            }
            composable("auditory") {
                com.ivanna.omega.ui.AuditoryExperienceScreen(
                    modifier = Modifier.fillMaxSize().background(Carbon)
                        .windowInsetsPadding(WindowInsets.systemBars),
                    onEnterMotorClick = { nav.navigate("adaptive") }
                )
            }
            composable("lab") {
                com.ivanna.omega.ui.IvannaLabScreen(
                    modifier = Modifier.fillMaxSize().background(Carbon)
                        .windowInsetsPadding(WindowInsets.systemBars)
                )
            }
            composable("perceptual_brain") { nav.navigate(IvannaRoute.BRAIN) { popUpTo("dashboard") } }
            composable("calibracion_saf") {
                SaFCalibrationScreen(onBack = { nav.popBackStack() })
            }
            composable("adaptive_dash")    { nav.navigate(IvannaRoute.BRAIN) { popUpTo("dashboard") } }

            // ── Sección SONIDO ────────────────────────────────────────────
            composable(IvannaRoute.SOUND) {
                SoundScreen(
                    modifier = Modifier.fillMaxSize().background(Carbon)
                        .windowInsetsPadding(WindowInsets.systemBars)
                )
            }
            // Alias legacy
            composable("ope")      { nav.navigate(IvannaRoute.SOUND) { popUpTo("dashboard") } }
            composable("binaural") { nav.navigate(IvannaRoute.SOUND) { popUpTo("dashboard") } }

            // ── Sección CEREBRO ───────────────────────────────────────────
            composable(IvannaRoute.BRAIN) {
                BrainScreen(
                    modifier = Modifier.fillMaxSize().background(Carbon)
                        .windowInsetsPadding(WindowInsets.systemBars)
                )
            }
            composable(IvannaRoute.ADAPTIVE) { nav.navigate(IvannaRoute.BRAIN) { popUpTo("dashboard") } }
            composable(IvannaRoute.PERCEPTUAL){ nav.navigate(IvannaRoute.BRAIN) { popUpTo("dashboard") } }
            composable(IvannaRoute.LAB)       { nav.navigate(IvannaRoute.BRAIN) { popUpTo("dashboard") } }

            // ── Sección ESPACIO ───────────────────────────────────────────
            composable(IvannaRoute.SPACE) {
                com.ivanna.omega.ui.AuditoryExperienceScreen(
                    onEnterMotorClick = { nav.navigate(IvannaRoute.BRAIN) },
                    modifier = Modifier.fillMaxSize().background(Carbon)
                        .windowInsetsPadding(WindowInsets.systemBars)
                )
            }
            composable("auditory")   { nav.navigate(IvannaRoute.SPACE) { popUpTo("dashboard") } }

            // ── Sección SISTEMA ───────────────────────────────────────────
            composable(IvannaRoute.SYSTEM) {
                SystemScreen(
                    onOpenMagisk   = { nav.navigate(IvannaRoute.MAGISK) },
                    onOpenProfiles = { nav.navigate(IvannaRoute.PROFILES) },
                    modifier = Modifier.fillMaxSize().background(Carbon)
                        .windowInsetsPadding(WindowInsets.systemBars)
                )
            }
            composable(IvannaRoute.TELEMETRY) { nav.navigate(IvannaRoute.SYSTEM) { popUpTo("dashboard") } }
            composable("telemetry")           { nav.navigate(IvannaRoute.SYSTEM) { popUpTo("dashboard") } }
            composable("adaptive_profiles")   { nav.navigate(IvannaRoute.SYSTEM) { popUpTo("dashboard") } }
        }
    }
}

@Composable
fun SplashScreen(onAccept: () -> Unit) {
    Box(Modifier.fillMaxSize().background(Carbon).windowInsetsPadding(WindowInsets.systemBars),
        contentAlignment = Alignment.Center) {
        Column(horizontalAlignment = Alignment.CenterHorizontally,
            modifier = Modifier.padding(horizontal = 28.dp)) {
            Text("IVANNA-OMEGA-SUPREME", color = TextPri, fontSize = 22.sp,
                fontWeight = FontWeight.ExtraBold, letterSpacing = 2.sp)
            Spacer(Modifier.height(4.dp))
            Text("GORE TNS · LUPP-OR9 · DSP ENGINE v1.0", color = CyanGlow,
                fontSize = 10.sp, letterSpacing = 1.5.sp)
            Spacer(Modifier.height(6.dp))
            Text("PI-LSTM Milenio · Neuro-Cochlear · Volterra H2 · Ω-Atlas",
                color = GoldGlow, fontSize = 9.sp, letterSpacing = 1.sp,
                textAlign = TextAlign.Center)
            Spacer(Modifier.height(8.dp))
            Box(Modifier.height(1.dp).width(260.dp).background(
                Brush.horizontalGradient(listOf(Color.Transparent, CyanGlow, Color.Transparent))))
            Spacer(Modifier.height(36.dp))
            Column(Modifier.fillMaxWidth().border(1.dp, Border1, RoundedCornerShape(12.dp))
                .background(Surface2, RoundedCornerShape(12.dp)).padding(16.dp)) {
                Text("AVISO LEGAL", color = CyanGlow, fontSize = 13.sp,
                    fontWeight = FontWeight.Bold, letterSpacing = 1.sp)
                Spacer(Modifier.height(10.dp))
                Text("© 2025–2026 Luis Uriel Pimentel Pérez · GORE TNS. " +
                    "Software propietario y confidencial. " +
                    "Uso no autorizado prohibido. " +
                    "Este software modifica el pipeline de audio del sistema. " +
                    "El usuario asume plena responsabilidad.",
                    color = TextMid, fontSize = 12.sp, lineHeight = 18.sp,
                    textAlign = TextAlign.Justify)
            }
            Spacer(Modifier.height(32.dp))
            Button(onClick = onAccept,
                colors = ButtonDefaults.buttonColors(containerColor = Color.Transparent),
                border = BorderStroke(2.dp, CyanGlow),
                shape = RoundedCornerShape(24.dp),
                modifier = Modifier.fillMaxWidth().height(52.dp)) {
                Text("ACEPTAR E INICIAR", color = TextPri,
                    fontWeight = FontWeight.Bold, letterSpacing = 1.5.sp)
            }
        }
    }
}

@Composable
fun IntroScreen(onEnter: (String?) -> Unit) {
    val bands = listOf("Grand Funk Railroad", "Led Zeppelin", "Rush",
        "Budgie", "Edgar Winter", "Steve Miller Band", "Bachman-Turner Overdrive")
    val bandProfileIds = mapOf(
        "Grand Funk Railroad" to "grand_funk",
        "Rush" to "rush",
        "Budgie" to "budgie",
        "Steve Miller Band" to "steve_miller"
    )
    var selectedBand by remember { mutableStateOf<String?>(null) }
    Column(Modifier.fillMaxSize().background(Carbon)
        .windowInsetsPadding(WindowInsets.systemBars).padding(horizontal = 16.dp),
        horizontalAlignment = Alignment.CenterHorizontally) {
        Spacer(Modifier.height(28.dp))
        Text("EXPERIENCIA AUDITIVA", color = TextPri, fontSize = 18.sp,
            fontWeight = FontWeight.ExtraBold, letterSpacing = 1.5.sp)
        Spacer(Modifier.height(4.dp))
        Text("Hard Rock 70s · DSP + HRTF + Neuromorphic", color = TextSec, fontSize = 10.sp)
        Spacer(Modifier.height(16.dp))
        LazyVerticalGrid(columns = GridCells.Fixed(3), modifier = Modifier.weight(1f),
            verticalArrangement = Arrangement.spacedBy(8.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            items(bands) { band ->
                val hasProfile = bandProfileIds.containsKey(band)
                val isSelected = band == selectedBand
                Box(Modifier.aspectRatio(16f/9f).shadow(6.dp, RoundedCornerShape(8.dp))
                    .background(Surface2, RoundedCornerShape(8.dp))
                    .border(if (isSelected) 2.dp else 1.dp,
                        if (isSelected) NeonMagenta else CyanDim, RoundedCornerShape(8.dp))
                    .then(
                        if (hasProfile) Modifier.clickable {
                            selectedBand = if (isSelected) null else band
                        } else Modifier
                    ),
                    contentAlignment = Alignment.Center) {
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        Box(Modifier.size(8.dp).clip(CircleShape).background(
                            when {
                                isSelected -> NeonMagenta
                                hasProfile -> CyanGlow
                                else -> CyanGlow.copy(alpha = 0.5f)
                            }
                        ))
                        Spacer(Modifier.height(4.dp))
                        Text(band, color = if (isSelected) TextPri else TextSec, fontSize = 8.sp,
                            textAlign = TextAlign.Center,
                            modifier = Modifier.padding(horizontal = 4.dp), lineHeight = 11.sp)
                    }
                }
            }
        }
        Spacer(Modifier.height(16.dp))
        Button(onClick = { onEnter(selectedBand?.let { bandProfileIds[it] }) },
            modifier = Modifier.fillMaxWidth().height(56.dp),
            colors = ButtonDefaults.buttonColors(containerColor = CyanDim),
            border = BorderStroke(2.dp, CyanGlow), shape = RoundedCornerShape(12.dp)) {
            Text(
                if (selectedBand != null) "ENTRAR CON ${selectedBand!!.uppercase()}" else "ENTRAR AL MOTOR",
                color = TextPri, fontSize = 16.sp,
                fontWeight = FontWeight.ExtraBold, letterSpacing = 1.sp)
        }
        Spacer(Modifier.height(16.dp))
    }
}

@Composable
fun DashboardScreen(
    dsp: MutableState<DSPState>,
    nav: androidx.navigation.NavHostController,
    adaptiveBackend: AdaptiveBackend,
    voiceProtectionManager: com.ivanna.omega.audio.VoiceProtectionManager
) {
    val eqActive = dsp.value.low != 0f || dsp.value.mid != 0f || dsp.value.high != 0f || dsp.value.presence != 0f
    val fxActive = dsp.value.wet > 0.01f
    val lstmReady = PiLstmBridge.isReady
    var npeBypassState by remember { mutableStateOf(false) }
    val audioState by com.ivanna.omega.audio.AudioStateManager.audioState.collectAsState()
    val voiceActive by voiceProtectionManager.voiceProtectionActive.observeAsState(false)

    val context = LocalContext.current
    val adaptiveBackend = remember { AdaptiveBackend(context) }
    val antiDolbyController = remember {
        AntiDolbyController(context).also { ctrl ->
            ctrl.initialize()
            ctrl.onDspUpdate = { exciter, width, eqGainDb ->
                dsp.value = dsp.value.copy(wet = exciter, stereoWidth = width)
                dsp.value.pushToNative()
                if (IvannaNativeLib.isLoaded)
                    IvannaNativeLib.nativeSetEQParams(
                        eqGainDb, eqGainDb, eqGainDb, dsp.value.master
                    )
            }
        }
    }
    DisposableEffect(Unit) {
        adaptiveBackend.startTelemetry()
        try {
            if (IvannaDspManager.open()) IvannaDspManager.enable()
        } catch (_: Throwable) {}
        com.ivanna.omega.audio.IvannaControlLoop.start()
        onDispose {
            adaptiveBackend.stopTelemetry()
            try { IvannaDspManager.close() } catch (_: Throwable) {}
            com.ivanna.omega.audio.IvannaControlLoop.stop()
        }
    }
    val adaptiveTelemetryRaw by adaptiveBackend.telemetry.collectAsState()
    val adaptiveTelemetry = adaptiveTelemetryRaw.toSnapshot()

    val telemetrySnapshot = remember(adaptiveTelemetryRaw) {
        AdaptiveTelemetrySnapshot.fromAdaptiveTelemetry(adaptiveTelemetryRaw)
    }

    val paramStore = remember { ParameterStore(context) }

    LaunchedEffect(Unit) { ShmManager.initialize(context) }

    val presetManager = remember { PresetManager(context) }
    var selectedPreset by remember { mutableStateOf(presetManager.getCurrentPreset()) }

    val audioRoute = remember(context) { AudioRoutingManager.detectOutputRoute(context) }

    val learningController = remember { RealtimeLearningController(context) }
    val yamnetForLearning by AudioPipeline.sharedYamnetResult.collectAsState()
    val dominantGenre = remember(yamnetForLearning) {
        when {
            !yamnetForLearning.valid -> null
            yamnetForLearning.speech >= yamnetForLearning.music
                && yamnetForLearning.speech >= yamnetForLearning.bass -> "speech"
            yamnetForLearning.music >= yamnetForLearning.bass -> "music"
            else -> "bass"
        }
    }
    DisposableEffect(Unit) { onDispose { learningController.release() } }

    val player = remember { IvannaBridgePlayer(context) }
    DisposableEffect(player) { onDispose { player.release() } }
    DisposableEffect(player) {
        com.ivanna.omega.audio.MediaSessionManager.init(context, player)
        onDispose { com.ivanna.omega.audio.MediaSessionManager.release() }
    }

    val playerPositionMs by player.currentPositionMs.collectAsState()
    val playerDurationMs by player.durationMs.collectAsState()
    val omegaMetrics by player.omegaMetrics.collectAsState()
    var playerState by remember { mutableStateOf(player.state) }
    var currentUri  by remember { mutableStateOf<Uri?>(null) }
    var queue       by remember { mutableStateOf<List<Uri>>(emptyList()) }
    var queueIdx    by remember { mutableStateOf(-1) }
    LaunchedEffect(player) {
        player.onQueueAdvance = { nextUri ->
            currentUri = nextUri
            val idx = queue.indexOf(nextUri)
            if (idx >= 0) queueIdx = idx
        }
        while (true) { playerState = player.state; delay(100L) }
    }
    val singlePicker = rememberLauncherForActivityResult(
        ActivityResultContracts.GetContent()
    ) { uri -> if (uri != null) { currentUri = uri; queue = listOf(uri); queueIdx = 0 } }
    val queuePicker = rememberLauncherForActivityResult(
        ActivityResultContracts.GetMultipleContents()
    ) { uris -> if (uris.isNotEmpty()) { queue = uris; queueIdx = 0; currentUri = uris.first() } }

    Column(Modifier.fillMaxSize().background(Carbon).windowInsetsPadding(WindowInsets.systemBars)) {
        if (!DSPBridge.isLoaded) {
            Box(Modifier.fillMaxWidth().background(Color(0xFF330000)).padding(8.dp),
                contentAlignment = Alignment.Center) {
                Text("⚠ libivanna_omega.so no disponible",
                    color = Color(0xFFFF4444), fontSize = 11.sp)
            }
        }
        Row(Modifier.fillMaxWidth().background(Surface2)
            .padding(horizontal = 16.dp, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween) {
            Column {
                Text("IVANNA-OMEGA-SUPREME", color = TextPri, fontWeight = FontWeight.ExtraBold,
                    fontSize = 12.sp, letterSpacing = 1.2.sp)
                Text("GORE TNS · v1.0", color = CyanGlow, fontSize = 9.sp, letterSpacing = 1.sp)
            }
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                StatusDot(DSPBridge.isLoaded, "DSP")
                StatusDot(eqActive, "EQ")
                StatusDot(fxActive, "FX")
                StatusDot(lstmReady, "LSTM")
            }
        }

        Row(
            Modifier.fillMaxWidth().horizontalScroll(rememberScrollState())
                .padding(horizontal = 16.dp, vertical = 6.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            listOf(
                "PERCEPTUAL BRAIN CORTEX" to "perceptual_brain",
                "TELEMETRÍA" to "telemetry",
                "OPE" to "ope",
                "BINAURAL" to "binaural",
                "AUDITORY" to "auditory",
                "ADAPTIVE Ω" to "adaptive_dash",
                "LAB" to "lab"
            ).forEach { (label, route) ->
                OutlinedButton(
                    onClick = { nav.navigate(route) },
                    colors = ButtonDefaults.outlinedButtonColors(contentColor = CyanGlow),
                    border = BorderStroke(1.dp, Border1),
                    shape = RoundedCornerShape(8.dp),
                    contentPadding = PaddingValues(horizontal = 10.dp, vertical = 4.dp)
                ) { Text(label, fontSize = 9.sp, fontWeight = FontWeight.Bold) }
            }
        }

        val routeState by com.ivanna.omega.audio.IvannaUnifiedPipeline.state.collectAsState()
        IvannaControlPanel(
            initialExciter = dsp.value.wet,
            initialEq = dsp.value.mid,
            initialWidth = dsp.value.stereoWidth,
            initialCompThreshold = dsp.value.alpha,
            initialCompRatio = dsp.value.beta,
            onExciterChange = { dsp.value = dsp.value.copy(wet = it); dsp.value.pushToNative() },
            onEqChange = {
                dsp.value = dsp.value.copy(low = it, mid = it, high = it, presence = it)
                dsp.value.pushToNative()
            },
            onWidthChange = { dsp.value = dsp.value.copy(stereoWidth = it); dsp.value.pushToNative() },
            onCompThresholdChange = { dsp.value = dsp.value.copy(alpha = it); dsp.value.pushToNative() },
            onCompRatioChange = { dsp.value = dsp.value.copy(beta = it); dsp.value.pushToNative() },
            onNhoHarmonicChange = {
                if (IvannaNativeLib.isLoaded) IvannaNativeLib.nativeSetHarmonicGain(it)
            },
            onEvoEnabledChange = { enabled ->
                if (IvannaNativeLib.isLoaded) {
                    if (enabled) IvannaNativeLib.nativeStartEvoThread()
                    else IvannaNativeLib.nativeStopEvoThread()
                }
            },
            onNpeBypassChange = { on ->
                npeBypassState = on
                if (PiLstmBridge.isReady) PiLstmBridge.setBypass(on)
            },
            onNpeHarmonicChange = { v ->
                if (PiLstmBridge.isReady && !npeBypassState) PiLstmBridge.setHarmonicGain(v)
            },
            onNpeLateralInhibChange = { v ->
                if (PiLstmBridge.isReady && !npeBypassState) PiLstmBridge.setBeta(v)
            },
            onNpeOhcCompressionChange = { v ->
                if (PiLstmBridge.isReady && !npeBypassState) PiLstmBridge.setAlpha(v)
            },
            onNpeMasterGainChange = { v ->
                if (PiLstmBridge.isReady && !npeBypassState) PiLstmBridge.setMasterGain(v)
            },
            onNpeAgcChange = { targetDb, rate ->
                if (PiLstmBridge.isReady && !npeBypassState) PiLstmBridge.setAgc(targetDb, rate)
            },
            onNpeFlagsChange = { hrtf, cochlear, adapt ->
                if (PiLstmBridge.isReady) {
                    PiLstmBridge.setHrtfEnabled(hrtf)
                    PiLstmBridge.setCochlearEnabled(cochlear)
                    PiLstmBridge.setAdaptEnabled(adapt)
                }
            },
            onSpatialAngleChange = {
                if (IvannaNativeLib.isLoaded)
                    IvannaNativeLib.nativeSetSpatialAngleRad((it - 0.5f) * 2f * PI.toFloat())
                IvannaSpatialEngine.setAzimuth((it - 0.5f) * 2f * PI.toFloat())
            },
            onSpatialWidthChange = {
                if (IvannaNativeLib.isLoaded)
                    IvannaNativeLib.nativeSetSpatialWidthDirect(it)
                IvannaSpatialEngine.setWidth(it)
            },
            onOpenVisualizer = { nav.navigate("visualizer") },
            onAntiDolbyChange = { enabled ->
                if (enabled) antiDolbyController.enableAntiDolby()
                else antiDolbyController.disableAntiDolby()
            },
            adaptiveTelemetry = adaptiveTelemetry,
            onOpenAdaptive = { nav.navigate(IvannaRoute.BRAIN) },
            onOpenAdaptiveEngineManual = { nav.navigate(IvannaRoute.BRAIN) },
            onOpenMagisk = { nav.navigate(IvannaRoute.MAGISK) },
            onOpenProfiles = { nav.navigate(IvannaRoute.PROFILES) },
            adaptiveMode = com.ivanna.omega.audio.AdaptiveMode.valueOf(audioState.adaptiveMode.name),
            onAdaptiveModeChange = { uiMode ->
                val backendMode = com.ivanna.omega.audio.AdaptiveMode.valueOf(uiMode.name)
                com.ivanna.omega.audio.AudioStateManager.updateState { it.copy(adaptiveMode = backendMode) }
                if (audioState.manualModeEnabled) adaptiveBackend.applyManualState(
                    com.ivanna.omega.audio.AudioStateManager.audioState.value
                )
            },
            adaptiveIntensity = audioState.adaptiveIntensity * 100f,
            onAdaptiveIntensityChange = { percent ->
                val v = percent / 100f
                com.ivanna.omega.audio.AudioStateManager.updateState { it.copy(adaptiveIntensity = v) }
                if (audioState.manualModeEnabled) adaptiveBackend.applyManualState(
                    com.ivanna.omega.audio.AudioStateManager.audioState.value
                )
            },
            voiceProtectionEnabled = voiceActive,
            onVoiceProtectionChange = { voiceProtectionManager.toggle() },
            initialSpatialEnabled = com.ivanna.omega.spatial.IvannaSpatialEngine.enabled,
            onSpatialEnabledChange = { on ->
                com.ivanna.omega.spatial.IvannaSpatialEngine.enabled = on
            },
            onNpeManifoldChange = { enabled ->
                com.ivanna.omega.audio.VolterraSwitch.enabled = enabled
            },
            routeState = routeState,
            initialAutoMode  = paramStore.isAutoModeEnabled(),
            initialOmegaMode = paramStore.getOmegaMode(),
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
                com.ivanna.omega.audio.AudioStateManager.updateState {
                    it.copy(manualModeEnabled = !enabled)
                }
            },
            onOmegaModeChange = { mode ->
                paramStore.setOmegaMode(mode)
                OmegaEngineBridge.setIntensity(mode / 2f)
            },
            onPhaseOracleChange = { intensity ->
                val i = intensity.coerceIn(0f, 1f)
                com.ivanna.omega.audio.AudioStateManager.updateState {
                    it.copy(phaseOracleIntensity = i)
                }
                if (IvannaNativeLib.isLoaded) {
                    runCatching {
                        IvannaNativeLib.nativeSetPhaseParameters(
                            i,
                            i * 0.7f,
                            i * 0.5f
                        )
                    }
                }
            }
        )

        BridgePlayerCard(
            playerState = playerState,
            currentUri  = currentUri,
            onPickFile  = { singlePicker.launch("audio/*") },
            onPlay = {
                val uri = currentUri
                if (uri != null) {
                    if (queue.size > 1) player.playQueue(queue, queueIdx.coerceAtLeast(0))
                    else player.play(uri)
                }
            },
            onPause  = { player.pause() },
            onResume = { player.resume() },
            onStop   = { player.stop() },
            queue    = queue,
            queueIndex = queueIdx,
            onPickQueue = { queuePicker.launch("audio/*") },
            onNext = {
                val nextIdx = (queueIdx + 1).coerceAtMost(queue.lastIndex)
                if (nextIdx != queueIdx && queue.isNotEmpty()) {
                    queueIdx = nextIdx; currentUri = queue[nextIdx]; player.play(queue[nextIdx])
                }
            },
            onPrev = {
                val prevIdx = (queueIdx - 1).coerceAtLeast(0)
                if (prevIdx != queueIdx && queue.isNotEmpty()) {
                    queueIdx = prevIdx; currentUri = queue[prevIdx]; player.play(queue[prevIdx])
                }
            }
        )
    }
}

@Composable
fun StatusDot(active: Boolean, label: String) {
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        Box(Modifier.size(8.dp).clip(CircleShape)
            .background(if (active) CyanGlow else Color(0xFF333333)))
        Text(label, color = if (active) CyanGlow else Color(0xFF444444), fontSize = 7.sp)
    }
}

@Composable
fun DspSection(title: String, content: @Composable RowScope.() -> Unit) {
    Column(Modifier.fillMaxWidth().border(1.dp, Border1, RoundedCornerShape(10.dp))
        .background(Surface1, RoundedCornerShape(10.dp)).padding(10.dp)) {
        Text(title, color = CyanGlow, fontSize = 10.sp,
            fontWeight = FontWeight.Bold, letterSpacing = 1.5.sp)
        Spacer(Modifier.height(8.dp))
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceEvenly,
            content = content)
    }
}

@Composable
fun FaderControl(name: String, value: Float, desc: String, onValueChange: (Float) -> Unit) {
    Column(horizontalAlignment = Alignment.CenterHorizontally, modifier = Modifier.width(54.dp)) {
        Text("%.2f".format(value), color = CyanGlow, fontSize = 9.sp, fontWeight = FontWeight.Bold)
        Spacer(Modifier.height(2.dp))
        Box(Modifier.width(34.dp).height(88.dp), contentAlignment = Alignment.Center) {
            Slider(value = value, onValueChange = onValueChange,
                modifier = Modifier.width(88.dp).rotate(-90f),
                colors = SliderDefaults.colors(thumbColor = CyanGlow,
                    activeTrackColor = CyanGlow, inactiveTrackColor = Border1))
        }
        Spacer(Modifier.height(2.dp))
        Text(name, color = TextPri, fontSize = 10.sp, fontWeight = FontWeight.Bold,
            textAlign = TextAlign.Center)
        Text(desc, color = TextSec, fontSize = 7.sp,
            textAlign = TextAlign.Center, lineHeight = 9.sp)
    }
}

@Composable
fun EqFader(name: String, db: Float, onDbChange: (Float) -> Unit) {
    val sliderVal = DSPState.dbToSlider(db)
    Column(horizontalAlignment = Alignment.CenterHorizontally, modifier = Modifier.width(54.dp)) {
        Text(if (db >= 0) "+%.1f".format(db) else "%.1f".format(db),
            color = CyanGlow, fontSize = 9.sp, fontWeight = FontWeight.Bold)
        Spacer(Modifier.height(2.dp))
        Box(Modifier.width(34.dp).height(88.dp), contentAlignment = Alignment.Center) {
            Slider(value = sliderVal, onValueChange = { onDbChange(DSPState.sliderToDb(it)) },
                modifier = Modifier.width(88.dp).rotate(-90f),
                colors = SliderDefaults.colors(
                    thumbColor = if (db > 0f) CyanGlow else Color(0xFF00AACC),
                    activeTrackColor = CyanGlow, inactiveTrackColor = Border1))
        }
        Spacer(Modifier.height(2.dp))
        Text(name, color = TextPri, fontSize = 10.sp, fontWeight = FontWeight.Bold,
            textAlign = TextAlign.Center)
        Text("dB", color = TextSec, fontSize = 7.sp, textAlign = TextAlign.Center)
    }
}
