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
import com.ivanna.omega.core.NativeBridge
import com.ivanna.omega.neuromorphic.PiLstmBridge
import com.ivanna.omega.ui.theme.*
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

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
fun BrainScreen(
    modifier: Modifier = Modifier,
    adaptiveBack: com.ivanna.omega.audio.AdaptiveBackend? = null
) {
    val context = LocalContext.current
    val audioState by AudioStateManager.audioState.collectAsState()
    var selectedTab by remember { mutableIntStateOf(0) }
    val tabs = listOf("ADAPTATIVO", "PERCEPTUAL", "COGNITIVO 9-15", "EVOLUTIVO", "LAB", "PROFILER")

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
                0 -> AdaptiveTab(adaptiveBack)
                1 -> Column(verticalArrangement = androidx.compose.foundation.layout.Arrangement.spacedBy(12.dp)) { PerceptualTab(); TinyMlClassifierPanel() }
                2 -> CognitiveEvolutionTab()
                3 -> Column(verticalArrangement = androidx.compose.foundation.layout.Arrangement.spacedBy(12.dp)) { EvolutionTab(prefs, ::updatePrefs); CmaEsFitnessPanel() }
                4 -> LabTab()
                5 -> NeonProfilerPanel()
            }
        }
    }
}

// ── Tab ADAPTATIVO ────────────────────────────────────────────────────────────
@Composable
private fun AdaptiveTab(backend: com.ivanna.omega.audio.AdaptiveBackend? = null) {
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
                                runCatching { IvannaNativeLib.nativeSetAdaptiveControls(mode.ordinal, audioState.adaptiveIntensity * 100f) }
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
                    runCatching { IvannaNativeLib.nativeSetAdaptiveControls(audioState.adaptiveMode.ordinal, v * 100f) }
            }
            IvannaSliderRowBrain("SAFETY MARGIN", audioState.safetyMargin, 0.5f, 1f, "") { v ->
                AudioStateManager.updateState { it.copy(safetyMargin = v) }
                backend?.applyManualState(AudioStateManager.audioState.value)
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
                    onCheckedChange = { enabled ->
                        AudioStateManager.updateState { s -> s.copy(manualModeEnabled = enabled) }
                        // Motor A y manual nunca escriben a la vez (mismo contrato que AdaptiveEngineScreen).
                        if (IvannaNativeLib.isLoaded)
                            runCatching { IvannaNativeLib.guardedNative(Unit) { IvannaNativeLib.nativeSetAdaptiveEngineEnabled(!enabled) } }
                        val st = AudioStateManager.audioState.value
                        if (enabled) { backend?.resetModulator(); backend?.forceManualState(st) }
                        else backend?.persist(st)
                    }
                )
            }
            if (audioState.manualModeEnabled) {
                IvannaSliderRowBrain("COMPRESOR", audioState.compressorThreshold, -60f, 0f, "dB") { v ->
                    AudioStateManager.updateState { it.copy(compressorThreshold = v) }
                    backend?.applyManualState(AudioStateManager.audioState.value)
                }
                IvannaSliderRowBrain("EXCITER", audioState.exciterAmount, 0f, 1f, "") { v ->
                    AudioStateManager.updateState { it.copy(exciterAmount = v) }
                    backend?.applyManualState(AudioStateManager.audioState.value)
                }
            }
        }
    }
}

// ── Tab PERCEPTUAL ────────────────────────────────────────────────────────────
@Composable
private fun PerceptualTab() {
    // Fila = (etiqueta, texto con unidad, progreso 0..1). null = motor nativo sin datos.
    var rows by remember { mutableStateOf<List<Triple<String, String, Float>>?>(null) }

    LaunchedEffect(Unit) {
        while (true) {
            val tele = if (IvannaNativeLib.isLoaded)
                runCatching { IvannaNativeLib.nativeGetAdaptiveTelemetry() }.getOrNull() else null
            rows = if (tele == null || tele.size < 10) null else {
                fun dbfs(lin: Float) = if (lin > 1e-6f) 20f * kotlin.math.log10(lin) else -120f
                fun norm01(v: Float) = if (v.isFinite()) v.coerceIn(0f, 1f) else 0f
                fun pct(v: Float) = "%.0f %%".format(norm01(v) * 100f)
                val rmsDb = dbfs(tele[0]); val peakDb = dbfs(tele[1])
                listOf(
                    Triple("RMS", "%.1f dBFS".format(rmsDb), norm01((rmsDb + 60f) / 60f)),
                    Triple("Pico", "%.1f dBFS".format(peakDb), norm01((peakDb + 60f) / 60f)),
                    Triple("Reducción de ganancia", "%.1f dB".format(tele[2]), norm01(kotlin.math.abs(tele[2]) / 12f)),
                    Triple("Ganancia objetivo", "%.2f ×".format(tele[3]), norm01(tele[3] / 2f)),
                    Triple("Compresión aplicada", pct(tele[4]), norm01(tele[4])),
                    Triple("Reducción de exciter", pct(tele[5]), norm01(tele[5])),
                    Triple("Ancho espacial", pct(tele[6]), norm01(tele[6] / 1.5f)),
                    Triple("Margen de seguridad", pct(tele[7]), norm01(tele[7])),
                    Triple("Protección de voz", pct(tele[8]), norm01(tele[8]))
                )
            }
            kotlinx.coroutines.delay(100)
        }
    }

    GlassCard("TELEMETRÍA PERCEPTUAL", NeonMagenta, "Motor adaptativo · 10 Hz") {
        val data = rows
        if (data == null) {
            Text("Motor nativo sin datos — activa el procesamiento para ver la telemetría.",
                color = TextSecondary, fontSize = 11.sp)
        } else Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
            data.forEach { (label, text, progress) ->
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text(label, color = TextSecondary, fontSize = 11.sp)
                    Text(text, color = NeonMagenta, fontSize = 11.sp,
                        fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace)
                }
                LinearProgressIndicator(
                    progress = { progress },
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
                        onValueChangeFinished = {
                            // Soltar el slider aplica al motor si la evolución ya corre
                            // (antes sólo tenía efecto tras volver a pulsar INICIAR).
                            if (isRunning && IvannaNativeLib.isLoaded)
                                runCatching { IvannaNativeLib.nativeInitializeEvolution(prefs.evoPopSize, prefs.evoGenerations) }
                        },
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
                        onValueChangeFinished = {
                            if (isRunning && IvannaNativeLib.isLoaded)
                                runCatching { IvannaNativeLib.nativeInitializeEvolution(prefs.evoPopSize, prefs.evoGenerations) }
                        },
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
    val scope = rememberCoroutineScope()
    var reportText by remember { mutableStateOf("Presiona MEDIR para iniciar") }
    var measureResult by remember { mutableStateOf<FloatArray?>(null) }
    var perceptualCues by remember { mutableStateOf<FloatArray?>(null) }
    var audioSpectrum by remember { mutableStateOf<FloatArray?>(null) }
    var phaseEnergy by remember { mutableFloatStateOf(0f) }
    var labAutoFrames by remember { mutableIntStateOf(0) }
    var labAutoEnabled by remember {
        mutableStateOf(
            if (IvannaNativeLib.isLoaded)
                IvannaNativeLib.guardedNative(true) { IvannaNativeLib.nativeIsLabAutoEnabled() }
            else true
        )
    }
    var neuralBenchUs by remember { mutableStateOf<FloatArray?>(null) }

    LaunchedEffect(Unit) {
        while (true) {
            if (IvannaNativeLib.isLoaded) {
                perceptualCues = IvannaNativeLib.guardedNative(null) { IvannaNativeLib.nativeGetPerceptualCues() }
                audioSpectrum = IvannaNativeLib.guardedNative(null) { IvannaNativeLib.nativeGetAudioSpectrum() }
                phaseEnergy = IvannaNativeLib.guardedNative(0f) { IvannaNativeLib.nativeGetPhaseEnergy() }
                labAutoFrames = IvannaNativeLib.guardedNative(0) { IvannaNativeLib.nativeGetLabAutoFrameCount() }
                labAutoEnabled = IvannaNativeLib.guardedNative(labAutoEnabled) { IvannaNativeLib.nativeIsLabAutoEnabled() }
            }
            kotlinx.coroutines.delay(500L)
        }
    }

    GlassCard("IVANNA LAB", NeonMagenta, "Medición · Análisis · Reporte") {
        Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    "AUTO-FEED LAB (${if (labAutoEnabled) "ON" else "OFF"})",
                    color = if (labAutoEnabled) PhosphorGreen else TextMuted,
                    fontSize = 10.sp,
                    fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace
                )
                Switch(
                    checked = labAutoEnabled,
                    onCheckedChange = { en ->
                        labAutoEnabled = en
                        if (IvannaNativeLib.isLoaded) {
                            IvannaNativeLib.guardedNative(Unit) { IvannaNativeLib.nativeSetLabAutoEnabled(en) }
                        }
                    }
                )
            }
            Text(reportText, color = TextMuted, fontSize = 10.sp,
                fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace,
                modifier = Modifier.fillMaxWidth())

            Text(
                "AUTO-FEED FRAMES: $labAutoFrames · PHASE ENERGY: ${"%.4f".format(phaseEnergy)}",
                color = AuroraCyan,
                fontSize = 10.sp,
                fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace
            )
            perceptualCues?.let { cues ->
                if (cues.size >= 4) {
                    Text(
                        "CUES [L=${"%.2f".format(cues[0])} T=${"%.2f".format(cues[1])} S=${"%.2f".format(cues[2])} R=${"%.2f".format(cues[3])}]",
                        color = TextSecondary,
                        fontSize = 10.sp,
                        fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace
                    )
                }
            }
            audioSpectrum?.let { spec ->
                if (spec.isNotEmpty()) {
                    val avgEnv = spec.average().toFloat()
                    Text(
                        "ESPECTRO BEB (${spec.size} bandas) · ENV MEDIA: ${"%.4f".format(avgEnv)}",
                        color = TextSecondary,
                        fontSize = 10.sp,
                        fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace
                    )
                }
            }
            neuralBenchUs?.let { b ->
                if (b.size >= 4) {
                    Text(
                        "BENCH NEURAL (µs/256f): NHO=${"%.1f".format(b[0])} BEB=${"%.1f".format(b[1])} SP=${"%.1f".format(b[2])} TOT=${"%.1f".format(b[3])}",
                        color = PhosphorGreen,
                        fontSize = 10.sp,
                        fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace
                    )
                }
            }

            measureResult?.let { m ->
                val labLabels = listOf(
                    "THD (%)",
                    "IMD SMPTE (%)",
                    "LUFS Integrado (LUFS)",
                    "Rango Dinámico LRA (LU)",
                    "SNR Estadístico (dB)",
                    "Peak (dBFS)",
                    "True Peak (dBTP)"
                )
                Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    labLabels.forEachIndexed { i, lbl ->
                        if (i < m.size) {
                            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                                Text(lbl, color = TextSecondary, fontSize = 10.sp)
                                Text(if (m[i] == -1f) "—" else "%.3f".format(m[i]), color = NeonMagenta, fontSize = 10.sp,
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

                Button(
                    onClick = {
                        if (IvannaNativeLib.isLoaded) {
                            scope.launch(kotlinx.coroutines.Dispatchers.Default) {
                                val res = IvannaNativeLib.guardedNative(null) {
                                    IvannaNativeLib.nativeRunNeuralBenchmarks()
                                }
                                kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.Main) {
                                    neuralBenchUs = res
                                }
                            }
                        }
                    },
                    colors = ButtonDefaults.buttonColors(containerColor = AuroraCyan.copy(alpha = 0.2f), contentColor = AuroraCyan),
                    modifier = Modifier.weight(1f)
                ) { Text("BENCH", fontSize = 11.sp) }
            }
        }
    }
}

// ── Tab COGNITIVO 9-15 (Acoustic Reality Cognitive Evolution Engine) ──────────
@Composable
private fun CognitiveEvolutionTab() {
    val context = LocalContext.current
    var axesState by remember { mutableStateOf(SupremeAxesPrefs.load(context)) }
    var realityEnabled by remember { mutableStateOf(axesState.realityReconstructionEnabled) }
    var realityIntensity by remember { mutableFloatStateOf(axesState.realityIntensity) }
    var realityTele by remember { mutableStateOf(NativeBridge.safeGetRealityTelemetryOrNull()) }
    var cogTele by remember { mutableStateOf(NativeBridge.safeGetCognitiveTelemetryOrNull()) }

    LaunchedEffect(Unit) {
        NativeBridge.safeSetRealityReconstructionEnabled(realityEnabled)
        NativeBridge.safeSetRealityIntensity(realityIntensity)
        while (true) {
            realityTele = NativeBridge.safeGetRealityTelemetryOrNull()
            cogTele = NativeBridge.safeGetCognitiveTelemetryOrNull()
            kotlinx.coroutines.delay(250L)
        }
    }

    val priorityNames = listOf(
        "1. PROFUNDIDAD FÍSICA",
        "2. MICRODINÁMICA",
        "3. CLARIDAD VOCAL",
        "4. EXPANSIÓN AMBIENTAL"
    )
    val topAxisIdx = cogTele?.getOrNull(0)?.toInt()?.coerceIn(0, 3)
    val cogPct: (Int) -> Float? = { i -> cogTele?.getOrNull(i)?.takeIf { it.isFinite() } }
    val realPct: (Int) -> Float? = { i -> realityTele?.getOrNull(i)?.takeIf { it.isFinite() } }
    val fmtPct: (Float?) -> String = { v -> v?.let { "%.1f %%".format(it * 100f) } ?: "—" }

    GlassCard(
        "SISTEMA NERVIOSO SUPERIOR · FASES 9–15",
        AuroraCyan,
        "AcousticCognitiveCore · Specialist Network · ExecutiveBrain · Memory · Homeostasis · DigitalTwin"
    ) {
        Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(
                    "RECONSTRUCCIÓN COGNITIVA DE REALIDAD ACÚSTICA",
                    color = TextSecondary,
                    fontSize = 11.sp,
                    modifier = Modifier.weight(1f)
                )
                Switch(
                    checked = realityEnabled,
                    onCheckedChange = { en ->
                        realityEnabled = en
                        val next = axesState.copy(realityReconstructionEnabled = en)
                        axesState = next
                        SupremeAxesPrefs.save(context, next)
                        NativeBridge.safeSetRealityReconstructionEnabled(en)
                    }
                )
            }
            IvannaSliderRowBrain("INTENSIDAD REALIDAD", realityIntensity, 0f, 1f, "") { v ->
                realityIntensity = v
                val next = axesState.copy(realityIntensity = v)
                axesState = next
                SupremeAxesPrefs.save(context, next)
                NativeBridge.safeSetRealityIntensity(v)
            }
            Text(
                "EJE LÍDER ACTUAL: ${topAxisIdx?.let { priorityNames[it] } ?: "— (motor nativo no disponible)"}",
                color = PhosphorGreen,
                fontSize = 11.sp,
                fontWeight = FontWeight.Bold,
                fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace
            )
        }
    }

    GlassCard(
        "FASE 9 & 10 · PRIORIDADES COGNITIVAS Y RED DE ESPECIALISTAS",
        NeonMagenta,
        "Spatial · Room · MicroReality · HumanPerception Judge"
    ) {
        val metrics: List<Pair<String, Float?>> = listOf(
            "Prioridad Profundidad" to cogPct(1),
            "Prioridad Microdinámica" to cogPct(2),
            "Prioridad Claridad Vocal" to cogPct(3),
            "Prioridad Expansión" to cogPct(4),
            "Spatial Agent (Localización)" to cogPct(5),
            "Room Agent (Realismo Físico)" to cogPct(6),
            "MicroReality Agent (Vitalidad)" to cogPct(7),
            "HumanPerception Judge (Veredicto)" to cogPct(8),
            "Índice Anti-Espectacularidad" to cogPct(9)
        )
        Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
            metrics.forEach { (label, value) ->
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Text(label, color = TextSecondary, fontSize = 11.sp)
                    Text(
                        fmtPct(value),
                        color = NeonMagenta,
                        fontSize = 11.sp,
                        fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace
                    )
                }
                LinearProgressIndicator(
                    progress = { (value ?: 0f).coerceIn(0f, 1f) },
                    modifier = Modifier.fillMaxWidth().height(3.dp),
                    color = NeonMagenta,
                    trackColor = ObsidianEdge
                )
            }
        }
    }

    GlassCard(
        "FASES 11–15 · EXECUTIVE BRAIN, MEMORIA, HOMEOSTASIS Y GEMELO DIGITAL",
        PhosphorGreen,
        "Arbitraje de Conflictos · Rush Xanadu Memory · PID Homeostático · CMA-ES + Q-Learning"
    ) {
        val r6 = realPct(6)
        val r7 = realPct(7)
        val r8 = realPct(8)
        val roomDimensions = if (r6 != null && r7 != null && r8 != null) {
            "%.1f×%.1f×%.1f m".format(r6, r7, r8)
        } else {
            "—"
        }
        val execMetrics: List<Pair<String, String>> = listOf(
            "Coherencia Executive Brain" to fmtPct(cogPct(10)),
            "Conflictos Arbitrados (Flags)" to (cogPct(11)?.let { "0x%02X".format(it.toInt()) } ?: "—"),
            "Estabilidad Homeostática (Fase 13)" to fmtPct(cogPct(12)),
            "Coherencia Digital Twin (Fase 14)" to fmtPct(cogPct(13)),
            "Fitness CMA-ES / Q-Learning (Fase 15)" to (cogPct(14)?.let { "%.3f".format(it) } ?: "—"),
            "Consolidaciones Memoria (Fase 12)" to (cogPct(15)?.let { "${it.toInt()} estados" } ?: "—"),
            "Sala Inferida (W×D×H)" to roomDimensions,
            "Realismo Perceptual Compuesto" to fmtPct(realPct(5))
        )
        Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
            execMetrics.forEach { (k, v) ->
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Text(k, color = TextSecondary, fontSize = 11.sp)
                    Text(
                        v,
                        color = PhosphorGreen,
                        fontSize = 11.sp,
                        fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace
                    )
                }
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
