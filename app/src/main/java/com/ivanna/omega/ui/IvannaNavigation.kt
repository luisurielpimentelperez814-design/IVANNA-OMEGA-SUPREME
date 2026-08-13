package com.ivanna.omega.ui

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
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

    var voiceProtEnabled by remember { mutableStateOf(true) }

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
                LaunchedEffect(Unit) {
                    while (true) {
                        if (IvannaNativeLib.isLoaded) {
                            runCatching {
                                telemetry.value = IvannaNativeLib.nativeGetAdaptiveTelemetry()
                                bands.value     = IvannaNativeLib.nativeGetBandEnergies()
                            }
                        }
                        kotlinx.coroutines.delay(200)
                    }
                }
                AdaptiveDashboard(telemetry = telemetry.value, bandEnergies = bands.value)
            }

            // ── SPATIAL ──────────────────────────────────────────────────
            composable(TABS[3].route) {
                SpatialHubScreen(
                    onOpenSaF        = { outerNav.navigate("calibracion_saf") },
                    onOpenVisualizer = { outerNav.navigate("visualizer") },
                    onOpenOpe        = { outerNav.navigate("ope") },
                    onOpenBinaural   = { outerNav.navigate("binaural") },
                    onOpenAuditory   = { outerNav.navigate("auditory") },
                    onOpenAbxTest    = { outerNav.navigate(IvannaRoute.ABX_TEST) },
                    onOpenBenchmark  = { outerNav.navigate("benchmark") },
                    onOpenPhase7     = { outerNav.navigate("phase7") }
                )
            }

            // ── SYSTEM ───────────────────────────────────────────────────
            composable(TABS[4].route) {
                SystemHubScreen(
                    onOpenMagisk   = { outerNav.navigate("magisk") },
                    onOpenProfiles = { outerNav.navigate("profiles") },
                    onOpenLab      = { outerNav.navigate("lab") }
                )
            }
        }
    }
}

// ── SpatialHubScreen ─────────────────────────────────────────────────────────
@Composable
fun SpatialHubScreen(
    onOpenSaF        : () -> Unit = {},
    onOpenVisualizer : () -> Unit,
    onOpenOpe        : () -> Unit,
    onOpenBinaural   : () -> Unit,
    onOpenAuditory   : () -> Unit,
    onOpenAbxTest    : () -> Unit,
    onOpenBenchmark  : () -> Unit = {},
    onOpenPhase7     : () -> Unit = {}
) {
    Column(
        modifier = Modifier.fillMaxSize().background(ObsidianVoid)
            .padding(horizontal = 16.dp, vertical = 20.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        HubHeader("SPATIAL ENGINE", "Binaural · HRTF · Object Renderer · Head Tracking", NeonMagenta)
        HubCard("CALIBRACIÓN Φ_SAF^∞",    "HRTF personalizado · 7-D Riemanniano · 214 HRTFs", AuroraCyan,   onOpenSaF)
        HubCard("VISUALIZADOR DE ESPECTRO",  "FFT 64-Band · Bark Perceptual",       AuroraCyan,   onOpenVisualizer)
        HubCard("EQ / COMPRESOR · OPE",      "IIR 10-Band · Brickwall Limiter",      AuroraCyan,   onOpenOpe)
        HubCard("MOTOR BINAURAL",            "HRTF + VBAP + 32 Objetos + 6DoF",     NeonMagenta,  onOpenBinaural)
        HubCard("EXPERIENCIA AUDITIVA",      "Calibración perceptual + ISO 226",     NeonMagenta,  onOpenAuditory)
        HubCard("PRUEBA ABX",                "Validación perceptual espacial",       NeonMagenta,  onOpenAbxTest)
        HubCard("BENCHMARK & EVIDENCE",      "Telemetría y validación acústica",     NeonMagenta,  onOpenBenchmark)
        HubCard("FASE 7: HEGEMONIA",         "Computer Vision & AutoEQ",             NeonMagenta,  onOpenPhase7)
    }
}

// ── SystemHubScreen ───────────────────────────────────────────────────────────
@Composable
fun SystemHubScreen(
    onOpenMagisk   : () -> Unit,
    onOpenProfiles : () -> Unit,
    onOpenLab      : () -> Unit
) {
    Column(
        modifier = Modifier.fillMaxSize().background(ObsidianVoid)
            .padding(horizontal = 16.dp, vertical = 20.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        HubHeader("SISTEMA", "Magisk · Perfiles · Laboratorio DSP", AmberSignal)
        HubCard("MAGISK MODULE STATUS", "Daemon RT · Shared Memory · SEPolicy",         AmberSignal,   onOpenMagisk)
        HubCard("PERFILES DE USUARIO",  "Bandas auditivas · EQ precalibrado · Presets", AmberSignal,   onOpenProfiles)
        HubCard("LABORATORIO DSP",      "Sweep · LUFS · THD+N · SNR",                  PhosphorGreen, onOpenLab)
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
            Icon(Icons.Default.KeyboardArrowRight, contentDescription = null, tint = accent.copy(alpha = 0.6f))
        }
    }
}
