package com.ivanna.omega.ui

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.navigation.NavGraph.Companion.findStartDestination
import androidx.navigation.NavHostController
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import com.ivanna.omega.audio.AdaptiveBackend
import com.ivanna.omega.audio.AdaptiveMode
import com.ivanna.omega.audio.OmegaMetrics
import com.ivanna.omega.audio.PipelineState
import com.ivanna.omega.audio.VoiceProtectionManager
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.dsp.DSPState
import com.ivanna.omega.ui.theme.*

// ── Tab descriptors ──────────────────────────────────────────────────────────
private data class NavTab(val route: String, val label: String, val icon: androidx.compose.ui.graphics.vector.ImageVector)

private val TABS = listOf(
    NavTab("tab_control",  "CONTROL",  Icons.Filled.Tune),
    NavTab("tab_brain",    "BRAIN",    Icons.Filled.Memory),
    NavTab("tab_adaptive", "ADAPTIVE", Icons.Filled.GraphicEq),
    NavTab("tab_spatial",  "SPATIAL",  Icons.Filled.SurroundSound),
    NavTab("tab_system",   "SYSTEM",   Icons.Filled.Settings)
)

/**
 * MainScaffold — BottomNavigation de 5 tabs.
 * Reemplaza DashboardScreen() en el NavHost de MainActivity.
 * outerNav → push de sub-pantallas (magisk, profiles, lab, etc.)
 */
@Composable
fun MainScaffold(
    outerNav   : NavHostController,
    dsp        : MutableState<DSPState>,
    adaptiveBack : AdaptiveBackend,
    voiceMgr   : VoiceProtectionManager,
    metrics    : OmegaMetrics      = OmegaMetrics(),
    adaptiveMode : AdaptiveMode    = AdaptiveMode.NATURAL,
    onAdaptiveModeChange : (AdaptiveMode) -> Unit = {},
    adaptiveIntensity    : Float   = 50f,
    onAdaptiveIntensityChange : (Float) -> Unit = {},
    routeState : PipelineState     = PipelineState()
) {
    val tabNav  = rememberNavController()
    val entry   by tabNav.currentBackStackEntryAsState()
    val current  = entry?.destination?.route

    Scaffold(
        containerColor = ObsidianVoid,
        bottomBar = {
            NavigationBar(containerColor = ObsidianSoft, tonalElevation = 0.dp) {
                TABS.forEach { tab ->
                    NavigationBarItem(
                        selected = current == tab.route,
                        onClick  = {
                            tabNav.navigate(tab.route) {
                                popUpTo(tabNav.graph.findStartDestination().id) { saveState = true }
                                launchSingleTop = true
                                restoreState    = true
                            }
                        },
                        icon   = { Icon(tab.icon, contentDescription = tab.label) },
                        label  = { Text(tab.label, style = MaterialTheme.typography.labelSmall) },
                        colors = NavigationBarItemDefaults.colors(
                            selectedIconColor   = AuroraCyan,
                            selectedTextColor   = AuroraCyan,
                            indicatorColor      = AuroraCyan.copy(alpha = 0.12f),
                            unselectedIconColor = TextMuted,
                            unselectedTextColor = TextMuted
                        )
                    )
                }
            }
        }
    ) { padding ->
        NavHost(
            navController    = tabNav,
            startDestination = TABS[0].route,
            modifier         = Modifier.padding(padding)
        ) {

            // ── CONTROL ─────────────────────────────────────────────────
            composable(TABS[0].route) {
                // FIX C (crítico): antes se pasaban sólo 9 parámetros al panel y los
                // otros 19 callbacks quedaban en su default `{}` → todos los knobs DSP
                // (anti-Dolby, presets, compresor, NHO, spatial, EVO, NPE, Phase Oracle,
                // omega/auto mode) no producían audio alguno. ControlTabScreen concentra
                // el cableado real de punta a punta.
                ControlTabScreen(
                    outerNav          = outerNav,
                    dsp               = dsp,
                    adaptiveBack      = adaptiveBack,
                    voiceMgr          = voiceMgr,
                    metrics           = metrics,
                    onOpenAdaptiveTab = { tabNav.navigate(TABS[2].route) { launchSingleTop = true } },
                    onOpenSpatialTab  = { tabNav.navigate(TABS[3].route) { launchSingleTop = true } },
                    onOpenBrainTab    = { tabNav.navigate(TABS[1].route) { launchSingleTop = true } }
                )
            }

            // ── BRAIN ────────────────────────────────────────────────────
            composable(TABS[1].route) {
                BrainScreen(
                    modifier = androidx.compose.ui.Modifier.fillMaxSize()
                )
            }

            // ── ADAPTIVE ─────────────────────────────────────────────────
            composable(TABS[2].route) {
                val telemetry = remember { mutableStateOf<FloatArray?>(null) }
                val bands     = remember { mutableStateOf<FloatArray?>(null) }
                val audioChar = remember { mutableStateOf<FloatArray?>(null) }  // [rms,peak,percussiveness,tonality,reverb,dynRange,centroid,spread]
                LaunchedEffect(Unit) {
                    while (true) {
                        if (IvannaNativeLib.isLoaded) {
                            runCatching {
                                telemetry.value  = IvannaNativeLib.nativeGetAdaptiveTelemetry()
                                bands.value      = IvannaNativeLib.nativeGetBandEnergies()
                                audioChar.value  = IvannaNativeLib.nativeGetAudioCharacteristics()
                            }
                        }
                        kotlinx.coroutines.delay(200)
                    }
                }
                AdaptiveDashboard(
                    telemetry    = telemetry.value,
                    bandEnergies = bands.value,
                    audioChar    = audioChar.value
                )
            }

            // ── SPATIAL ──────────────────────────────────────────────────
            composable(TABS[3].route) {
                SpatialHubScreen(
                    onOpenSupremeAxesHub  = { outerNav.navigate(IvannaRoute.SUPREME_AXES_HUB) },
                    onOpenAxis1Lattice    = { outerNav.navigate(IvannaRoute.SUPREME_AXIS_1_LATTICE) },
                    onOpenAxis2Cvnn       = { outerNav.navigate(IvannaRoute.SUPREME_AXIS_2_CVNN) },
                    onOpenAxis3SnnHoa     = { outerNav.navigate(IvannaRoute.SUPREME_AXIS_3_SNN_HOA) },
                    onOpenAxis4PinnaInr   = { outerNav.navigate(IvannaRoute.SUPREME_AXIS_4_PINNA_INR) },
                    onOpenAxis5ShmFarrow  = { outerNav.navigate(IvannaRoute.SUPREME_AXIS_5_SHM_FARROW) },
                    onOpenCochlearInverse = { outerNav.navigate("cochlear_inverse") },
                    onOpenSaF        = { outerNav.navigate("calibracion_saf") },
                    onOpenAudioControlHub = { outerNav.navigate(IvannaRoute.AUDIO_CONTROL_HUB) },
                    onOpenVisualizer = { outerNav.navigate("visualizer") },
                    onOpenOpe        = { outerNav.navigate("ope") },
                    onOpenBinaural   = { outerNav.navigate("spatial_audio") },
                    onOpenAuditory   = { outerNav.navigate(IvannaRoute.SPACE) },
                    onOpenAbxTest    = { outerNav.navigate(IvannaRoute.ABX_TEST) },
                    onOpenBenchmark  = { outerNav.navigate("benchmark") },
                    onOpenPhase7     = { outerNav.navigate("phase7") },
                    onOpenCognitiveDash = { outerNav.navigate("cognitive_dash") },
                    onOpenSpatialControl = { outerNav.navigate("spatial_control") }
                )
            }

            // ── SYSTEM ───────────────────────────────────────────────────
            composable(TABS[4].route) {
                SystemHubScreen(
                    onOpenSupremeAxesHub = { outerNav.navigate(IvannaRoute.SUPREME_AXES_HUB) },
                    onOpenAxis5ShmFarrow = { outerNav.navigate(IvannaRoute.SUPREME_AXIS_5_SHM_FARROW) },
                    onOpenMagisk    = { outerNav.navigate("magisk") },
                    onOpenProfiles  = { outerNav.navigate("profiles") },
                    onOpenLab       = { outerNav.navigate("lab") },
                    onOpenEngines   = { outerNav.navigate("engines_status") },
                    onOpenOemDash   = { outerNav.navigate("oem_dashboard") },
                    onOpenAssistant = { outerNav.navigate(IvannaRoute.IVANNA_ASSISTANT) },
                    onOpenNetwork   = { outerNav.navigate(IvannaRoute.NETWORK) },
                    onOpenAdaptiveProfiles = { outerNav.navigate("adaptive_profiles") },
                    onOpenHiRes     = { outerNav.navigate("hires") }
                )
            }
        }
    }
}

// ── SpatialHubScreen ─────────────────────────────────────────────────────────
@Composable
fun SpatialHubScreen(
    onOpenSupremeAxesHub  : () -> Unit = {},
    onOpenAxis1Lattice    : () -> Unit = {},
    onOpenAxis2Cvnn       : () -> Unit = {},
    onOpenAxis3SnnHoa     : () -> Unit = {},
    onOpenAxis4PinnaInr   : () -> Unit = {},
    onOpenAxis5ShmFarrow  : () -> Unit = {},
    onOpenCochlearInverse : () -> Unit = {},
    onOpenSaF        : () -> Unit = {},
    onOpenVisualizer : () -> Unit,
    onOpenOpe        : () -> Unit,
    onOpenBinaural   : () -> Unit,
    onOpenAuditory   : () -> Unit,
    onOpenAbxTest    : () -> Unit,
    onOpenBenchmark  : () -> Unit = {},
    onOpenPhase7     : () -> Unit = {},
    onOpenAudioControlHub : () -> Unit = {},
    onOpenCognitiveDash : () -> Unit = {},
    onOpenSpatialControl : () -> Unit = {}
) {
    Column(
        modifier = Modifier.fillMaxSize().background(ObsidianVoid)
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 16.dp, vertical = 20.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        HubHeader("SPATIAL ENGINE", "Binaural · HRTF · Object Renderer · 5 Ejes de Supremacía", NeonMagenta)
        HubCard("✦ 5 EJES DE SUPREMACÍA NEUROACÚSTICA", "Centro Maestro C++23 · Celosía · CVNN · SNN-HOA4 · Pinna INR · Farrow 5º", PhosphorGreen, onOpenSupremeAxesHub)
        HubCard("EJE 1 · CELOSÍA DEFORMADA (ANTI-DIRAC)", "Warped Lattice Z(ω) · Excursión Bl(x) · Micro-Chirp 17.5–19 kHz", AuroraCyan, onOpenAxis1Lattice)
        HubCard("EJE 2 · TRANSARMÓNICO CVNN+DDSP (ANTI-DSEE)", "Analítica Hilbert >16 kHz · Derivada de Fase · Supresión IMD H2", NeonMagenta, onOpenAxis2Cvnn)
        HubCard("EJE 3 · SNN INT8 + NMF → HOA 4º (ANTI-DOLBY)", "4 Flujos Ortogonales · 16 Canales Esféricos · UPOLA · SCHED_FIFO", PhosphorGreen, onOpenAxis3SnnHoa)
        HubCard("EJE 4 · PINNA MANIFOLD INR-SDF (ANTI-APPLE)", "MLP SIREN 2 Capas · Descenso Gradiente 3 Pasos · FIR 32-Tap Fase Mínima", AmberSignal, onOpenAxis4PinnaInr)
        HubCard("EJE 5 · ARBITRAJE SHM + FARROW 5º MSO", "Bypass AudioFlinger eBPF/XDP · Lockless CAS owner_pid · Retardo Sub-ns", AuroraCyan, onOpenAxis5ShmFarrow)
        HubCard("INVERSIÓN BIOMECÁNICA COCLEAR", "Anti-Dolby PINN · Descompresión OHC Activa · Latencia 0.00 ms", AuroraCyan, onOpenCochlearInverse)
        HubCard("CALIBRACIÓN Φ_SAF^∞",    "HRTF personalizado · 7-D Riemanniano · 214 HRTFs", AuroraCyan,   onOpenSaF)
        HubCard("CONTROL ESPACIAL 3D",       "Azimuth · Elevación · Ancho Estéreo Directo",         AuroraCyan,   onOpenSpatialControl)
        HubCard("VISUALIZADOR DE ESPECTRO",  "FFT 64-Band · Bark Perceptual",       AuroraCyan,   onOpenVisualizer)
        HubCard("EQ / COMPRESOR · OPE",      "IIR 10-Band · Brickwall Limiter",      AuroraCyan,   onOpenOpe)
        HubCard("MOTOR BINAURAL",            "HRTF + VBAP + 32 Objetos + 6DoF",     NeonMagenta,  onOpenBinaural)
        HubCard("EXPERIENCIA AUDITIVA",      "Calibración perceptual + ISO 226",     NeonMagenta,  onOpenAuditory)
        HubCard("PRUEBA ABX",                "Validación perceptual espacial",       NeonMagenta,  onOpenAbxTest)
        HubCard("BENCHMARK & EVIDENCE",      "Telemetría y validación acústica",     NeonMagenta,  onOpenBenchmark)
        HubCard("FASE 7: HEGEMONIA",         "Computer Vision & AutoEQ",             NeonMagenta,  onOpenPhase7)
        HubCard("SOFA · AF · RIR · SAF",     "Panel de control de motores avanzados · HRTF real · Sala · Adaptativo", AuroraCyan, onOpenAudioControlHub)
        // FIX (sub-entorno muerto): CognitiveDashboardActivity estaba en el
        // Manifest pero sin entrada en la UI — pantalla inalcanzable. Ahora
        // tiene su HubCard que navega a la ruta "cognitive_dash".
        HubCard("CEREBRO COGNITIVO",         "Perceptual Brain · Fatiga · Emoción · Q-Learning", AuroraCyan,   onOpenCognitiveDash)
    }
}

// ── SystemHubScreen ───────────────────────────────────────────────────────────
@Composable
fun SystemHubScreen(
    onOpenSupremeAxesHub : () -> Unit = {},
    onOpenAxis5ShmFarrow : () -> Unit = {},
    onOpenMagisk    : () -> Unit,
    onOpenProfiles  : () -> Unit,
    onOpenLab       : () -> Unit,
    onOpenEngines   : () -> Unit = {},
    onOpenOemDash   : () -> Unit = {},
    onOpenAssistant : () -> Unit = {},
    onOpenNetwork   : () -> Unit = {},
    onOpenAdaptiveProfiles : () -> Unit = {},
    onOpenHiRes     : () -> Unit = {}
) {
    Column(
        modifier = Modifier.fillMaxSize().background(ObsidianVoid)
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 16.dp, vertical = 20.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        HubHeader("SISTEMA", "IVANNA · Control OEM · Magisk · Perfiles · Lab · Motores", AmberSignal)
        // IVANNA primero — es la interfaz conversacional principal del producto
        HubCard(
            "✦ IVANNA ASSISTANT",
            "Inteligencia acústica conversacional · Voz · Lenguaje · Agentes · Memoria",
            AuroraCyan, onOpenAssistant
        )
        HubCard(
            "✦ 5 EJES DE SUPREMACÍA NEUROACÚSTICA",
            "Warped Lattice · CVNN+DDSP · SNN-HOA4 · Pinna INR-SDF · Arbitraje SHM Farrow 5º",
            PhosphorGreen, onOpenSupremeAxesHub
        )
        HubCard(
            "EJE 5 · ARBITRAJE KERNEL SHM + FARROW 5º",
            "Bypass eBPF/XDP · Lockless CAS owner_pid · Alineación MSO Sub-Nanosegundo",
            AuroraCyan, onOpenAxis5ShmFarrow
        )
        HubCard("CENTRO DE CONTROL OEM++",
            "Dashboard · HRTF · SAF/RIR · IA · Térmico · Telemetría",
            AuroraCyan, onOpenOemDash)
        HubCard("MAGISK MODULE STATUS", "Daemon RT · Shared Memory · SEPolicy",         AmberSignal,   onOpenMagisk)
        HubCard("PERFILES DE USUARIO",  "Bandas auditivas · EQ precalibrado · Presets", AmberSignal,   onOpenProfiles)
        HubCard("LABORATORIO DSP",      "Sweep · LUFS · THD+N · SNR",                  PhosphorGreen, onOpenLab)
        HubCard("ESTADO DE MOTORES",
            "RouteDspCalibrator · USB Pro · HRTF · Backend · Daemon · Control Loop",
            AuroraCyan, onOpenEngines)
        HubCard("ESTADO DE RED / TELEMETRÍA",
            "Latencia · Socket IPC · Conectividad y Puertos",
            AuroraCyan, onOpenNetwork)
        HubCard("PERFILES ADAPTATIVOS",
            "Catálogo dinámico de curvas psicoacústicas",
            AmberSignal, onOpenAdaptiveProfiles)
        HubCard("AUDIO HI-RES",
            "Selector de frecuencia 16..384 kHz y profundidad de bits",
            AuroraCyan, onOpenHiRes)
    }
}

@Composable
private fun HubHeader(title: String, subtitle: String, accent: Color) {
    Spacer(Modifier.height(4.dp))
    Text(title,    color = accent,       style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold)
    Text(subtitle, color = TextMuted,    style = MaterialTheme.typography.labelSmall)
    Spacer(Modifier.height(4.dp))
}

@Composable
private fun HubCard(title: String, subtitle: String, accent: Color, onClick: () -> Unit) {
    Surface(
        onClick  = onClick,
        modifier = Modifier.fillMaxWidth(),
        color    = ObsidianSoft,
        shape    = MaterialTheme.shapes.medium,
        border   = BorderStroke(1.dp, accent.copy(alpha = 0.30f))
    ) {
        Row(
            modifier  = Modifier.padding(16.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Box(Modifier.size(4.dp, 36.dp).background(accent, MaterialTheme.shapes.extraSmall))
            Column(Modifier.weight(1f)) {
                Text(title,    color = TextPrimary,   style = MaterialTheme.typography.labelLarge,  fontWeight = FontWeight.SemiBold)
                Text(subtitle, color = TextSecondary, style = MaterialTheme.typography.labelSmall)
            }
            Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, contentDescription = null, tint = accent.copy(alpha = 0.6f))
        }
    }
}
