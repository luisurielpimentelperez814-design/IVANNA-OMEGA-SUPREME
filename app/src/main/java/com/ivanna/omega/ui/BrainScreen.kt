package com.ivanna.omega.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.platform.LocalContext
import com.ivanna.omega.audio.AdaptiveMode
import com.ivanna.omega.audio.AudioStateManager
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.neuromorphic.PiLstmBridge
import com.ivanna.omega.ui.theme.*

/**
 * BrainScreen — Sección CEREBRO unificada.
 *
 * Reemplaza y unifica:
 *   · AdaptiveEngineScreen   → tab ADAPTATIVO
 *   · PerceptualBrainDashboard → tab PERCEPTUAL
 *   · AdaptiveDashboard      → tab TELEMETRÍA
 *   · IvannaLabScreen        → tab LAB
 *
 * Conecta funciones nativas antes sin UI:
 *   · nativeInitializeEvolution(popSize, generations)
 *   · nativeEvolveStep()
 *   · nativeSetMutationRate(rate)
 *   · nativeLabReset / nativeLabFeed / nativeLabMeasure / nativeLabReport
 */
@Composable
fun BrainScreen(modifier: Modifier = Modifier) {
    val context = LocalContext.current
    val audioState by AudioStateManager.audioState.collectAsState()
    var selectedTab by remember { mutableIntStateOf(0) }
    val tabs = listOf("ADAPTATIVO", "PERCEPTUAL", "EVOLUTIVO", "LAB", "PROFILER")

    // Bug F fix — estado evolutivo levantado para sobrevivir cambios de tab
    var prefs by remember { mutableStateOf(AdaptiveControlsPrefs.load(context)) }
    fun updatePrefs(update: (AdaptiveControlsState) -> AdaptiveControlsState) {
        prefs = update(prefs)
        AdaptiveControlsPrefs.save(context, prefs)
    }

    Column(modifier = modifier.background(ObsidianDeep)) {
        Text(
            "CEREBRO",
            color = NeonMagenta,
            fontSize = 11.sp,
            fontWeight = FontWeight.ExtraBold,
            letterSpacing = 3.sp,
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 12.dp)
        )
        ScrollableTabRow(
            selectedTabIndex = selectedTab,
            containerColor = Color.Transparent,
            contentColor = NeonMagenta,
            edgePadding = 16.dp
        ) {
            tabs.forEachIndexed { i, title ->
                Tab(
                    selected = selectedTab == i,
                    onClick = { selectedTab = i },
                    text = {
                        Text(
                            title,
                            fontSize = 11.sp,
                            fontWeight = if (selectedTab == i) FontWeight.Bold else FontWeight.Normal,
                            color = if (selectedTab == i) NeonMagenta else TextMuted
                        )
                    }
                )
            }
        }
        HorizontalDivider(color = ObsidianEdge, thickness = 0.5.dp)
        Column(
            modifier = Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 16.dp, vertical = 12.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            when (selectedTab) {
                0 -> AdaptiveTab()
                1 -> Column(verticalArrangement = androidx.compose.foundation.layout.Arrangement.spacedBy(12.dp)) { PerceptualTab(); TinyMlClassifierPanel() }
                2 -> Column(verticalArrangement = androidx.compose.foundation.layout.Arrangement.spacedBy(12.dp)) { EvolutionTab(prefs, ::updatePrefs); CmaEsFitnessPanel() }
                3 -> LabTab()
                4 -> NeonProfilerPanel()
            }
        }
    }
}

// ── Tab ADAPTATIVO ────────────────────────────────────────────────────────────
@Composable
private fun AdaptiveTab() {
    val audioState by AudioStateManager.audioState.collectAsState()

    GlassCard("MODO ADAPTATIVO", NeonMagenta, "Motor A · Decisión en tiempo real") {
        Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Row(
                horizontalArrangement = Arrangement.SpaceEvenly,
                modifier = Modifier.fillMaxWidth()
            ) {
                AdaptiveMode.values().forEach { mode ->
                    val sel = audioState.adaptiveMode == mode
                    FilledTonalButton(
                        onClick = {
                            AudioStateManager.updateState { it.copy(adaptiveMode = mode) }
                            if (IvannaNativeLib.isLoaded)
                                runCatching { IvannaNativeLib.nativeSetAdaptiveControls(mode.ordinal, audioState.adaptiveIntensity) }
                        },
                        colors = ButtonDefaults.filledTonalButtonColors(
                            containerColor = if (sel) NeonMagenta.copy(alpha = 0.25f) else ObsidianEdge,
                            contentColor   = if (sel) NeonMagenta else TextMuted
                        )
                    ) { Text(mode.label, fontSize = 11.sp) }
                }
            }
            IvannaSliderRowBrain("INTENSIDAD", audioState.adaptiveIntensity, 0f, 1f, "%") { v ->
                AudioStateManager.updateState { it.copy(adaptiveIntensity = v) }
                if (IvannaNativeLib.isLoaded)
                    runCatching { IvannaNativeLib.nativeSetAdaptiveControls(audioState.adaptiveMode.ordinal, v) }
            }
            IvannaSliderRowBrain("SAFETY MARGIN", audioState.safetyMargin, 0.5f, 1f, "") { v ->
                AudioStateManager.updateState { it.copy(safetyMargin = v) }
            }
        }
    }

    Spacer(Modifier.height(4.dp))

    GlassCard("MODO MANUAL", AuroraCyan, "Parámetros directos · Bypass del motor A") {
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("MODO MANUAL", color = TextSecondary, fontSize = 12.sp, modifier = Modifier.weight(1f))
                Switch(
                    checked = audioState.manualModeEnabled,
                    onCheckedChange = { AudioStateManager.updateState { s -> s.copy(manualModeEnabled = it) } }
                )
            }
            if (audioState.manualModeEnabled) {
                IvannaSliderRowBrain("COMPRESOR", audioState.compressorThreshold, -60f, 0f, "dB") { v ->
                    AudioStateManager.updateState { it.copy(compressorThreshold = v) }
                }
                IvannaSliderRowBrain("EXCITER", audioState.exciterAmount, 0f, 1f, "") { v ->
                    AudioStateManager.updateState { it.copy(exciterAmount = v) }
                }
            }
        }
    }
}

// ── Tab PERCEPTUAL ────────────────────────────────────────────────────────────
@Composable
private fun PerceptualTab() {
    var snapshot by remember { mutableStateOf<Map<String,Float>>(emptyMap()) }

    LaunchedEffect(Unit) {
        while (true) {
            if (IvannaNativeLib.isLoaded) {
                val tele = runCatching { IvannaNativeLib.nativeGetAdaptiveTelemetry() }.getOrNull()
                if (tele != null) {
                    snapshot = mapOf(
                        "RMS"         to (tele.getOrElse(0) { 0f }),
                        "Peak"        to (tele.getOrElse(1) { 0f }),
                        "GR"          to (tele.getOrElse(2) { 0f }),
                        "CPU"         to (tele.getOrElse(3) { 0f }),
                        "Voice Prot." to (tele.getOrElse(8) { 0f })
                    )
                }
            }
            kotlinx.coroutines.delay(100)
        }
    }

    GlassCard("TELEMETRÍA PERCEPTUAL", NeonMagenta, "ISO 226 · Bark/Mel · 10Hz") {
        Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
            snapshot.forEach { (label, value) ->
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text(label, color = TextSecondary, fontSize = 11.sp)
                    Text("%.3f".format(value), color = NeonMagenta, fontSize = 11.sp,
                        fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace)
                }
                LinearProgressIndicator(
                    progress = { value.coerceIn(0f, 1f) },
                    modifier = Modifier.fillMaxWidth().height(3.dp),
                    color = NeonMagenta,
                    trackColor = ObsidianEdge
                )
            }
        }
    }
}

// ── Tab EVOLUTIVO ─────────────────────────────────────────────────────────────
// Bug F fix — popSize/generations/mutationRate levantados a prefs; isRunning/statusText
// son estado de sesión y pueden permanecer locales.
@Composable
private fun EvolutionTab(
    prefs: AdaptiveControlsState,
    updatePrefs: ((AdaptiveControlsState) -> AdaptiveControlsState) -> Unit
) {
    var isRunning by remember { mutableStateOf(false) }
    var statusText by remember { mutableStateOf("Listo") }

    GlassCard("KERNEL EVOLUTIVO", AuroraCyan, "LM-CMA-ES · 512 bandas · Genoma DSP") {
        Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {

            IvannaSliderRowBrain("TASA MUTACIÓN", prefs.evoMutationRate, 0.001f, 0.3f, "") { v ->
                updatePrefs { it.copy(evoMutationRate = v) }
                if (IvannaNativeLib.isLoaded) runCatching { IvannaNativeLib.nativeSetMutationRate(v) }
            }

            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Column(Modifier.weight(1f)) {
                    Text("POBLACIÓN", color = TextSecondary, fontSize = 10.sp)
                    Slider(
                        value = prefs.evoPopSize.toFloat(),
                        onValueChange = { updatePrefs { s -> s.copy(evoPopSize = it.toInt()) } },
                        valueRange = 10f..200f,
                        colors = SliderDefaults.colors(thumbColor = AuroraCyan, activeTrackColor = AuroraCyan, inactiveTrackColor = ObsidianEdge)
                    )
                    Text("${prefs.evoPopSize}", color = AuroraCyan, fontSize = 10.sp,
                        fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace)
                }
                Column(Modifier.weight(1f)) {
                    Text("GENERACIONES", color = TextSecondary, fontSize = 10.sp)
                    Slider(
                        value = prefs.evoGenerations.toFloat(),
                        onValueChange = { updatePrefs { s -> s.copy(evoGenerations = it.toInt()) } },
                        valueRange = 10f..500f,
                        colors = SliderDefaults.colors(thumbColor = AuroraCyan, activeTrackColor = AuroraCyan, inactiveTrackColor = ObsidianEdge)
                    )
                    Text("${prefs.evoGenerations}", color = AuroraCyan, fontSize = 10.sp,
                        fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace)
                }
            }

            Text(statusText, color = TextMuted, fontSize = 10.sp,
                fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace)

            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(
                    onClick = {
                        if (IvannaNativeLib.isLoaded) {
                            runCatching {
                                val ok = IvannaNativeLib.nativeInitializeEvolution(prefs.evoPopSize, prefs.evoGenerations)
                                statusText = if (ok) "Evolución inicializada" else "Error al inicializar"
                                isRunning = ok
                            }.onFailure { statusText = "Error: ${it.message}" }
                        }
                    },
                    colors = ButtonDefaults.buttonColors(containerColor = AuroraCyan.copy(alpha = 0.2f), contentColor = AuroraCyan),
                    modifier = Modifier.weight(1f)
                ) { Text("INICIAR", fontSize = 11.sp) }

                Button(
                    onClick = {
                        if (IvannaNativeLib.isLoaded && isRunning) {
                            runCatching {
                                val cont = IvannaNativeLib.nativeEvolveStep()
                                statusText = if (cont) "Evolucionando..." else "Convergido"
                                isRunning = cont
                            }
                        }
                    },
                    enabled = isRunning,
                    colors = ButtonDefaults.buttonColors(containerColor = NeonMagenta.copy(alpha = 0.2f), contentColor = NeonMagenta),
                    modifier = Modifier.weight(1f)
                ) { Text("PASO", fontSize = 11.sp) }
            }
        }
    }
}

// ── Tab LAB ───────────────────────────────────────────────────────────────────
@Composable
private fun LabTab() {
    var reportText by remember { mutableStateOf("Presiona MEDIR para iniciar") }
    var measureResult by remember { mutableStateOf<FloatArray?>(null) }

    GlassCard("IVANNA LAB", NeonMagenta, "Medición · Análisis · Reporte") {
        Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Text(reportText, color = TextMuted, fontSize = 10.sp,
                fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace,
                modifier = Modifier.fillMaxWidth())

            measureResult?.let { m ->
                Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    listOf("THD", "SNR", "Latencia", "Flatness").forEachIndexed { i, lbl ->
                        if (i < m.size) {
                            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                                Text(lbl, color = TextSecondary, fontSize = 10.sp)
                                Text("%.4f".format(m[i]), color = NeonMagenta, fontSize = 10.sp,
                                    fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace)
                            }
                        }
                    }
                }
            }

            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(
                    onClick = {
                        if (IvannaNativeLib.isLoaded) {
                            // nativeLabReset — antes sin UI
                            runCatching { IvannaNativeLib.nativeLabReset() }
                            reportText = "Lab reiniciado"
                            measureResult = null
                        }
                    },
                    colors = ButtonDefaults.buttonColors(containerColor = ObsidianEdge, contentColor = TextSecondary),
                    modifier = Modifier.weight(1f)
                ) { Text("RESET", fontSize = 11.sp) }

                Button(
                    onClick = {
                        if (IvannaNativeLib.isLoaded) {
                            // nativeLabMeasure — antes sin UI
                            runCatching {
                                measureResult = IvannaNativeLib.nativeLabMeasure()
                                // nativeLabReport — antes sin UI
                                reportText = IvannaNativeLib.nativeLabReport()
                            }.onFailure { reportText = "Error: ${it.message}" }
                        }
                    },
                    colors = ButtonDefaults.buttonColors(containerColor = NeonMagenta.copy(alpha = 0.2f), contentColor = NeonMagenta),
                    modifier = Modifier.weight(1f)
                ) { Text("MEDIR", fontSize = 11.sp) }
            }
        }
    }
}

// ── Slider helper ─────────────────────────────────────────────────────────────
@Composable
private fun IvannaSliderRowBrain(
    label: String, value: Float, min: Float, max: Float, unit: String,
    onValueChange: (Float) -> Unit
) {
    Column {
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            Text(label, color = TextSecondary, fontSize = 11.sp)
            Text("${"%.2f".format(value)} $unit", color = NeonMagenta, fontSize = 11.sp,
                fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace)
        }
        Slider(
            value = value, onValueChange = onValueChange, valueRange = min..max,
            colors = SliderDefaults.colors(thumbColor = NeonMagenta, activeTrackColor = NeonMagenta, inactiveTrackColor = ObsidianEdge)
        )
    }
}
