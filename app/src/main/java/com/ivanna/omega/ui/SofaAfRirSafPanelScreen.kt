package com.ivanna.omega.ui

import androidx.compose.animation.*
import androidx.compose.animation.core.*
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.magisk.OmegaEngineBridge
import com.ivanna.omega.saf.SaFBridge
import com.ivanna.omega.saf.SaFRoomBridge
import com.ivanna.omega.spatial.IvannaSpatialManager
import com.ivanna.omega.ui.theme.*
import kotlinx.coroutines.delay
import kotlin.math.roundToInt

/**
 * SofaAfRirSafPanelScreen — Panel experto de procesamiento espacial.
 *
 * Cubre los cuatro subsistemas de la cadena espacial de IVANNA:
 *   • SOFA  — personalización HRTF con dataset de 214 sujetos vía IvannaSpatialManager
 *   • RIR   — convolución de sala con 200 impulsos reales medidos
 *   • SAF   — optimizador Riemanniano Φ_SAF∞ sobre el manifold de Stiefel
 *   • HRTF  — diagnóstico en tiempo real del renderer binaural
 *
 * Conectada exclusivamente a APIs que existen en el proyecto:
 *   IvannaSpatialManager.{isHrtfDatasetLoaded, currentHrtfSubject, setHrtfSubject, ready, activeSubject}
 *   SaFRoomBridge.{step, setRoomState, setHrtfState, getParams, getDiagnostics, reset}
 *   SaFBridge.{nativeSaFIsConverged, nativeSaFGetError, nativeSaFGetIteration, nativeSaFFeedback}
 *   OmegaEngineBridge.{setRoom, disableRoom, pushSafLatentQ, isConnected}
 *   SpatialAudioPrefs.{load(context), save(context, state)}
 */
@Composable
fun SofaAfRirSafPanelScreen(
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
    voiceMgr: Any? = null
) {
    val ctx = LocalContext.current

    // ── SOFA / HRTF ──────────────────────────────────────────────────────────
    var hrtfReady     by remember { mutableStateOf(IvannaSpatialManager.ready) }
    var hrtfSubject   by remember { mutableStateOf(IvannaSpatialManager.activeSubject) }
    var hrtfLoaded    by remember { mutableStateOf(IvannaSpatialManager.isHrtfDatasetLoaded()) }
    var sofaIntensity by remember { mutableStateOf(0.5f) }

    // Sujetos SOFA disponibles (los 12 deployados en el módulo)
    val sofaSubjects = remember {
        listOf("kemar", "kemar_large", "tu_berlin_kemar",
               "cipic_003", "cipic_008", "cipic_009",
               "cipic_010", "cipic_011", "cipic_012",
               "cipic_165", "pulse", "subject_165")
    }
    var selectedSubjectIdx by remember {
        mutableStateOf(sofaSubjects.indexOf(IvannaSpatialManager.activeSubject).coerceAtLeast(0))
    }

    // ── RIR ──────────────────────────────────────────────────────────────────
    var rirEnabled  by remember { mutableStateOf(false) }
    var roomSize    by remember { mutableStateOf(0.5f) }   // 0..1 → 3..40 m
    var rirWet      by remember { mutableStateOf(0.35f) }
    var rirRt60     by remember { mutableStateOf(0.5f) }   // s
    var rirRoomIdx  by remember { mutableStateOf(-1) }      // -1 = auto (mejor RT60)

    // ── SAF ──────────────────────────────────────────────────────────────────
    var safConverged  by remember { mutableStateOf(false) }
    var safError      by remember { mutableStateOf(0f) }
    var safIteration  by remember { mutableStateOf(0) }
    var safDiag       by remember { mutableStateOf(FloatArray(0)) }
    var safIntensity  by remember { mutableStateOf(0.5f) }
    var safEnabled    by remember { mutableStateOf(false) }
    var safStepResult by remember { mutableStateOf<Float?>(null) }

    // ── Daemon ───────────────────────────────────────────────────────────────
    var daemonConnected by remember { mutableStateOf(OmegaEngineBridge.isConnected) }

    // ── Carga inicial de preferencias ─────────────────────────────────────────
    LaunchedEffect(Unit) {
        val st = SpatialAudioPrefs.load(ctx)
        rirEnabled   = st.rirEnabled
        rirRt60      = st.rirRt60
        roomSize     = ((st.rirRt60 - 0.1f) / 1.4f).coerceIn(0f, 1f)
        rirWet       = st.rirWet
        safIntensity = st.safIntensity
        safEnabled   = st.safEnabled
    }

    // ── Polling de telemetría (500 ms) ────────────────────────────────────────
    LaunchedEffect(Unit) {
        while (true) {
            hrtfReady   = IvannaSpatialManager.ready
            hrtfSubject = IvannaSpatialManager.activeSubject
            hrtfLoaded  = IvannaSpatialManager.isHrtfDatasetLoaded()
            daemonConnected = OmegaEngineBridge.isConnected

            runCatching {
                safConverged  = SaFBridge.nativeSaFIsConverged()
                safError      = SaFBridge.nativeSaFGetError()
                safIteration  = SaFBridge.nativeSaFGetIteration()
                safDiag       = SaFRoomBridge.getDiagnostics()
            }
            delay(500)
        }
    }

    // ── Funciones de aplicación ───────────────────────────────────────────────
    fun applyRir() {
        val rt60 = 0.1f + roomSize * 1.4f
        rirRt60 = rt60
        if (rirEnabled) {
            OmegaEngineBridge.setRoom(rt60, rirWet, rirRoomIdx)
            runCatching { SaFRoomBridge.setRoomState(rt60, rirWet, if (rirRoomIdx >= 0) 1f else 0f) }
        } else {
            OmegaEngineBridge.disableRoom()
            runCatching { SaFRoomBridge.setRoomState(0f, 0f, 0f) }
        }
    }

    fun applySaf() {
        val q = runCatching { SaFRoomBridge.getParams() }.getOrDefault(FloatArray(7))
        OmegaEngineBridge.pushSafLatentQ(FloatArray(7) { q.getOrElse(it) { 0f } * safIntensity }, safIntensity)
        runCatching { SaFRoomBridge.setHrtfState(safError, safIntensity) }
    }

    fun applyHrtf() {
        val subject = sofaSubjects.getOrElse(selectedSubjectIdx) { "kemar" }
        runCatching { IvannaSpatialManager.setHrtfSubject(subject) }
        OmegaEngineBridge.setIntensity(sofaIntensity.toDouble())
    }

    fun persist() {
        SpatialAudioPrefs.save(ctx, SpatialAudioPrefs.load(ctx).copy(
            rirEnabled   = rirEnabled,
            rirRt60      = rirRt60,
            rirWet       = rirWet,
            safEnabled   = safEnabled,
            safIntensity = safIntensity,
            hrtfSubject  = sofaSubjects.getOrElse(selectedSubjectIdx) { "kemar" }
        ))
    }

    // ── UI ────────────────────────────────────────────────────────────────────
    Column(
        modifier = modifier
            .fillMaxSize()
            .background(Brush.verticalGradient(listOf(ObsidianVoid, ObsidianSoft)))
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 16.dp, vertical = 12.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp)
    ) {

        // ── Header ────────────────────────────────────────────────────────────
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            if (onBack != null) {
                IconButton(onClick = onBack, modifier = Modifier.size(36.dp)) {
                    Icon(Icons.Default.ArrowBack, null, tint = TextMuted)
                }
                Spacer(Modifier.width(4.dp))
            }
            Column(Modifier.weight(1f)) {
                Text("MOTOR ESPACIAL", color = AuroraCyan,
                    fontFamily = FontFamily.Monospace,
                    fontWeight = FontWeight.ExtraBold,
                    fontSize = 13.sp, letterSpacing = 2.sp)
                Text("SOFA · RIR · SAF · HRTF",
                    color = TextMuted, fontSize = 10.sp, letterSpacing = 1.sp)
            }
            // Badge de daemon
            StatusBadge(
                text   = if (daemonConnected) "DAEMON OK" else "OFFLINE",
                color  = if (daemonConnected) PhosphorGreen else AmberSignal,
                pulse  = daemonConnected
            )
        }

        HorizontalDivider(color = ObsidianEdge, thickness = 0.5.dp)

        // ════════════════════════════════════════════════════════════════
        // SECCIÓN 1: SOFA / HRTF
        // ════════════════════════════════════════════════════════════════
        SectionCard(
            title  = "SOFA · BINAURAL HRTF",
            subtitle = "Φ_SAF Riemanniano · 214 sujetos · PCA 7D · AES69",
            accent = PhosphorGreen,
            icon   = Icons.Default.Headset,
            statusText = if (hrtfLoaded) "CARGADO" else "SIN DATASET",
            statusOk   = hrtfLoaded
        ) {
            // Indicador de sujeto activo
            InfoRow("Sujeto activo", hrtfSubject, PhosphorGreen, mono = true)
            InfoRow("Renderer listo", if (hrtfReady) "SÍ" else "NO", if (hrtfReady) PhosphorGreen else AmberSignal)

            Spacer(Modifier.height(4.dp))

            // Selector de sujeto SOFA
            Text("DATASET DE SUJETO", color = TextMuted, fontSize = 9.sp,
                letterSpacing = 1.sp, fontWeight = FontWeight.Bold)
            Spacer(Modifier.height(4.dp))

            // Grid de sujetos 3×4
            val rows = sofaSubjects.chunked(3)
            rows.forEach { rowItems ->
                Row(
                    Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    rowItems.forEach { subject ->
                        val idx = sofaSubjects.indexOf(subject)
                        val selected = selectedSubjectIdx == idx
                        FilterChip(
                            selected = selected,
                            onClick  = {
                                selectedSubjectIdx = idx
                                applyHrtf(); persist()
                            },
                            label = {
                                Text(
                                    subject.replace("cipic_", "C").replace("kemar_large", "KEMAR_L")
                                          .replace("tu_berlin_kemar", "TUB").uppercase(),
                                    fontSize = 8.sp,
                                    fontWeight = if (selected) FontWeight.ExtraBold else FontWeight.Normal,
                                    fontFamily = FontFamily.Monospace
                                )
                            },
                            modifier = Modifier.weight(1f),
                            colors = FilterChipDefaults.filterChipColors(
                                selectedContainerColor = PhosphorGreen.copy(0.18f),
                                selectedLabelColor = PhosphorGreen,
                                labelColor = TextMuted,
                                containerColor = ObsidianDeep
                            ),
                            border = FilterChipDefaults.filterChipBorder(
                                enabled = true, selected = selected,
                                borderColor = if (selected) PhosphorGreen else ObsidianEdge.copy(0.4f),
                                selectedBorderColor = PhosphorGreen
                            ),
                            shape = RoundedCornerShape(8.dp)
                        )
                    }
                    // padding si la fila tiene menos de 3
                    repeat(3 - rowItems.size) {
                        Spacer(Modifier.weight(1f))
                    }
                }
                Spacer(Modifier.height(4.dp))
            }

            Spacer(Modifier.height(4.dp))

            // Intensidad de mezcla HRTF
            IvannaSlider(
                label      = "Intensidad de mezcla HRTF",
                value      = sofaIntensity,
                min        = 0f, max = 1f,
                color      = PhosphorGreen,
                valueText  = "${"%.0f".format(sofaIntensity * 100)} %",
                onChanged  = { sofaIntensity = it; applyHrtf(); persist() }
            )

            // Botones de acción
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                ExpertButton(
                    "APLICAR SUJETO",
                    PhosphorGreen,
                    Modifier.weight(1f)
                ) { applyHrtf(); persist() }
                ExpertButton(
                    "RELOAD HRTF",
                    AuroraCyan,
                    Modifier.weight(1f)
                ) { runCatching { IvannaSpatialManager.resumeHeadTracking() } }
            }
        }

        // ════════════════════════════════════════════════════════════════
        // SECCIÓN 2: RIR — Impulso de sala
        // ════════════════════════════════════════════════════════════════
        SectionCard(
            title    = "RIR · RESPUESTA AL IMPULSO DE SALA",
            subtitle = "200 salas medidas · Overlap-Save FFT · OmegaDaemon",
            accent   = AmberSignal,
            icon     = Icons.Default.GraphicEq,
            statusText = if (rirEnabled) "ACTIVO" else "BYPASS",
            statusOk   = rirEnabled
        ) {
            // Toggle principal
            IvannaToggleRow("Convolución de sala", rirEnabled, AmberSignal) {
                rirEnabled = it; applyRir(); persist()
            }

            Spacer(Modifier.height(2.dp))

            val metros = (3f + roomSize * 37f).roundToInt()
            val rt60Calc = 0.1f + roomSize * 1.4f

            // Sliders de sala
            IvannaSlider(
                label     = "Tamaño de sala",
                value     = roomSize,
                min       = 0f, max = 1f,
                color     = AmberSignal,
                valueText = "$metros m"
            ) { roomSize = it; applyRir(); persist() }

            IvannaSlider(
                label     = "Mezcla Dry/Wet",
                value     = rirWet,
                min       = 0f, max = 1f,
                color     = AmberSignal,
                valueText = "${"%.0f".format(rirWet * 100)} %"
            ) { rirWet = it; applyRir(); persist() }

            // Selector de sala específica vs auto
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("Sala", color = TextSecondary, fontSize = 11.sp, modifier = Modifier.weight(1f))
                Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    listOf("AUTO" to -1, "#10" to 10, "#50" to 50, "#100" to 100, "#150" to 150).forEach { (label, idx) ->
                        val sel = rirRoomIdx == idx
                        FilterChip(
                            selected = sel,
                            onClick  = { rirRoomIdx = idx; applyRir(); persist() },
                            label    = { Text(label, fontSize = 8.sp, fontFamily = FontFamily.Monospace,
                                fontWeight = if (sel) FontWeight.ExtraBold else FontWeight.Normal) },
                            colors   = FilterChipDefaults.filterChipColors(
                                selectedContainerColor = AmberSignal.copy(0.18f),
                                selectedLabelColor = AmberSignal,
                                labelColor = TextMuted,
                                containerColor = ObsidianDeep
                            ),
                            border = FilterChipDefaults.filterChipBorder(
                                enabled = true, selected = sel,
                                borderColor = if (sel) AmberSignal else ObsidianEdge.copy(0.4f),
                                selectedBorderColor = AmberSignal
                            ),
                            shape = RoundedCornerShape(8.dp)
                        )
                    }
                }
            }

            // Métricas acústicas calculadas
            val roomVolumeM3 = metros.toFloat().let { it * it * (it * 0.5f) }
            val sabine = (0.161f * roomVolumeM3) / (0.2f * roomVolumeM3) // RT60 Sabine aprox
            HorizontalDivider(color = ObsidianEdge.copy(0.3f), thickness = 0.5.dp)
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                MetricChip("RT60", "${"%.2f".format(rt60Calc)} s", AmberSignal)
                MetricChip("Sala", "$metros m", AmberSignal)
                MetricChip("Wet", "${"%.0f".format(rirWet * 100)} %", AmberSignal)
                MetricChip("Índice", if (rirRoomIdx < 0) "AUTO" else "#$rirRoomIdx", AmberSignal)
            }
        }

        // ════════════════════════════════════════════════════════════════
        // SECCIÓN 3: SAF — Optimizador Riemanniano
        // ════════════════════════════════════════════════════════════════
        SectionCard(
            title    = "SAF · OPTIMIZADOR Φ_SAF∞ RIEMANNIANO",
            subtitle = "Manifold Stiefel · Gradiente Riemanniano · Retracción QR",
            accent   = NeonMagenta,
            icon     = Icons.Default.AutoGraph,
            statusText = when {
                safConverged -> "CONVERGIDO"
                safIteration > 0 -> "ITER $safIteration"
                else -> "EN ESPERA"
            },
            statusOk = safConverged
        ) {
            // Activar SAF
            IvannaToggleRow("Motor SAF Activo", safEnabled, NeonMagenta) {
                safEnabled = it; if (it) applySaf() else persist()
            }

            // Intensidad del parámetro latente q
            IvannaSlider(
                label     = "Intensidad de modificación (q)",
                value     = safIntensity,
                min       = 0f, max = 1f,
                color     = NeonMagenta,
                valueText = "${"%.3f".format(safIntensity)}",
                enabled   = safEnabled
            ) { safIntensity = it; applySaf(); persist() }

            // Diagnóstico en tiempo real
            HorizontalDivider(color = ObsidianEdge.copy(0.3f), thickness = 0.5.dp)
            Text("TELEMETRÍA SAF", color = TextMuted, fontSize = 9.sp,
                letterSpacing = 1.sp, fontWeight = FontWeight.Bold)
            Spacer(Modifier.height(4.dp))

            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                MetricChip("Error E_t", "${"%.4f".format(safError)}", NeonMagenta)
                MetricChip("Iteración", safIteration.toString(), NeonMagenta)
                MetricChip("Convergido", if (safConverged) "SÍ" else "NO",
                    if (safConverged) PhosphorGreen else AmberSignal)
            }

            // Parámetros del vector q si están disponibles
            if (safDiag.size >= 5) {
                Spacer(Modifier.height(4.dp))
                InfoRow("Paso óptimo α*",   "${"%.5f".format(safDiag[0])}", NeonMagenta, mono = true)
                InfoRow("Error Mahal. E_t", "${"%.5f".format(safDiag[1])}", NeonMagenta, mono = true)
                InfoRow("Regulariz. M",     "${"%.5f".format(safDiag[2])}", NeonMagenta, mono = true)
                InfoRow("Gradiente ∇_R",    "${"%.5f".format(safDiag.getOrElse(3) { 0f })}", NeonMagenta, mono = true)
                InfoRow("Iteraciones",       safDiag[4].toInt().toString(), NeonMagenta, mono = true)
            }

            // Botones de feedback y control
            HorizontalDivider(color = ObsidianEdge.copy(0.3f), thickness = 0.5.dp)
            Text("FEEDBACK MANUAL", color = TextMuted, fontSize = 9.sp,
                letterSpacing = 1.sp, fontWeight = FontWeight.Bold)
            Spacer(Modifier.height(4.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                // Feedback positivo (dirección 1)
                Button(
                    onClick = {
                        runCatching {
                            SaFBridge.nativeSaFFeedback(1, true)
                            safStepResult = SaFRoomBridge.step()
                            applySaf()
                        }
                    },
                    modifier = Modifier.weight(1f).height(42.dp),
                    shape = RoundedCornerShape(10.dp),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = PhosphorGreen.copy(0.15f),
                        contentColor   = PhosphorGreen
                    )
                ) {
                    Icon(Icons.Default.ThumbUp, null, Modifier.size(16.dp))
                    Spacer(Modifier.width(6.dp))
                    Text("MEJOR", style = MaterialTheme.typography.labelSmall, fontWeight = FontWeight.ExtraBold)
                }

                // Feedback negativo (dirección -1)
                Button(
                    onClick = {
                        runCatching {
                            SaFBridge.nativeSaFFeedback(-1, false)
                            safStepResult = SaFRoomBridge.step()
                            applySaf()
                        }
                    },
                    modifier = Modifier.weight(1f).height(42.dp),
                    shape = RoundedCornerShape(10.dp),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = NeonMagenta.copy(0.15f),
                        contentColor   = NeonMagenta
                    )
                ) {
                    Icon(Icons.Default.ThumbDown, null, Modifier.size(16.dp))
                    Spacer(Modifier.width(6.dp))
                    Text("PEOR", style = MaterialTheme.typography.labelSmall, fontWeight = FontWeight.ExtraBold)
                }
            }

            // Resultado del último step
            safStepResult?.let { result ->
                Spacer(Modifier.height(4.dp))
                InfoRow("Último step Δ", "${"%.6f".format(result)}", AuroraCyan, mono = true)
            }

            // Iterar / Resetear
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                ExpertButton("ITERAR (STEP)", NeonMagenta, Modifier.weight(1f)) {
                    runCatching { safStepResult = SaFRoomBridge.step(); applySaf() }
                }
                ExpertButton("RESETEAR (q₀)", AmberSignal, Modifier.weight(1f)) {
                    runCatching { SaFRoomBridge.reset(); SaFBridge.nativeSaFReset() }
                    safStepResult = null; safIntensity = 0.5f; applySaf(); persist()
                }
            }
        }

        // ════════════════════════════════════════════════════════════════
        // SECCIÓN 4: Resumen de estado del pipeline completo
        // ════════════════════════════════════════════════════════════════
        SectionCard(
            title    = "ESTADO DEL PIPELINE ESPACIAL",
            subtitle = "Resumen de cadena SOFA → RIR → SAF",
            accent   = AuroraCyan,
            icon     = Icons.Default.AccountTree,
            statusText = "RESUMEN",
            statusOk   = hrtfReady && daemonConnected
        ) {
            val stageColor: (Boolean) -> Color = { ok -> if (ok) PhosphorGreen else AmberSignal }
            val stageText:  (Boolean) -> String = { ok -> if (ok) "✓" else "○" }

            // Pipeline visual
            @Composable
            fun PipelineStage(label: String, ok: Boolean, detail: String) {
                Row(
                    Modifier.fillMaxWidth().padding(vertical = 3.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(10.dp)
                ) {
                    Text(stageText(ok), color = stageColor(ok),
                        fontSize = 14.sp, fontFamily = FontFamily.Monospace,
                        fontWeight = FontWeight.ExtraBold)
                    Column(Modifier.weight(1f)) {
                        Text(label, color = TextPrimary, fontSize = 11.sp, fontWeight = FontWeight.Medium)
                        Text(detail, color = TextMuted, fontSize = 9.sp, fontFamily = FontFamily.Monospace)
                    }
                }
            }

            PipelineStage("HRTF · SOFA", hrtfReady,
                "Sujeto: $hrtfSubject · Intensidad: ${"%.0f".format(sofaIntensity * 100)}%")
            HorizontalDivider(color = ObsidianEdge.copy(0.2f), thickness = 0.5.dp,
                modifier = Modifier.padding(start = 24.dp))
            PipelineStage("RIR · Sala", rirEnabled,
                if (rirEnabled) "RT60 ${"%.2f".format(rirRt60)}s · Wet ${"%.0f".format(rirWet * 100)}%"
                else "Bypass activo")
            HorizontalDivider(color = ObsidianEdge.copy(0.2f), thickness = 0.5.dp,
                modifier = Modifier.padding(start = 24.dp))
            PipelineStage("SAF · Riemanniano", safConverged,
                "Error: ${"%.5f".format(safError)} · Iter: $safIteration")
            HorizontalDivider(color = ObsidianEdge.copy(0.2f), thickness = 0.5.dp,
                modifier = Modifier.padding(start = 24.dp))
            PipelineStage("Daemon OmegaEngine", daemonConnected,
                if (daemonConnected) "Conectado · IPC activo" else "Sin conexión al daemon")
        }

        Spacer(Modifier.height(32.dp))
    }
}

// ── Componentes privados ──────────────────────────────────────────────────────

@Composable
private fun SectionCard(
    title: String,
    subtitle: String,
    accent: Color,
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    statusText: String,
    statusOk: Boolean,
    content: @Composable ColumnScope.() -> Unit
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        color    = ObsidianGlass,
        shape    = RoundedCornerShape(18.dp),
        border   = BorderStroke(1.dp, accent.copy(alpha = 0.25f)),
        tonalElevation = 2.dp
    ) {
        Column(Modifier.padding(18.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            // Header de la sección
            Row(verticalAlignment = Alignment.CenterVertically) {
                Box(
                    modifier = Modifier
                        .size(36.dp)
                        .clip(RoundedCornerShape(10.dp))
                        .background(accent.copy(0.12f)),
                    contentAlignment = Alignment.Center
                ) {
                    Icon(icon, null, tint = accent, modifier = Modifier.size(20.dp))
                }
                Spacer(Modifier.width(10.dp))
                Column(Modifier.weight(1f)) {
                    Text(title, color = accent,
                        fontFamily = FontFamily.Monospace,
                        fontWeight = FontWeight.ExtraBold,
                        fontSize   = 11.sp, letterSpacing = 0.5.sp)
                    Text(subtitle, color = TextMuted, fontSize = 9.sp, letterSpacing = 0.3.sp)
                }
                StatusBadge(statusText, if (statusOk) PhosphorGreen else AmberSignal, pulse = statusOk)
            }
            HorizontalDivider(color = accent.copy(0.15f), thickness = 0.5.dp)
            content()
        }
    }
}

@Composable
private fun StatusBadge(text: String, color: Color, pulse: Boolean = false) {
    val alpha by if (pulse) {
        val inf = rememberInfiniteTransition(label = "pulse")
        inf.animateFloat(0.6f, 1f, infiniteRepeatable(tween(900, easing = EaseInOutSine),
            RepeatMode.Reverse), label = "a")
    } else remember { mutableStateOf(1f) }

    Row(
        modifier = Modifier
            .clip(RoundedCornerShape(20.dp))
            .background(color.copy(alpha * 0.15f))
            .padding(horizontal = 8.dp, vertical = 3.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(4.dp)
    ) {
        Box(Modifier.size(5.dp).clip(CircleShape).background(color.copy(alpha)))
        Text(text, color = color.copy(alpha), fontSize = 8.sp,
            fontWeight = FontWeight.ExtraBold, fontFamily = FontFamily.Monospace,
            letterSpacing = 0.5.sp)
    }
}

@Composable
private fun IvannaToggleRow(label: String, checked: Boolean, accent: Color, onChange: (Boolean) -> Unit) {
    Row(
        Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.SpaceBetween
    ) {
        Text(label, color = TextPrimary,
            style = MaterialTheme.typography.bodySmall,
            fontWeight = FontWeight.Medium,
            modifier = Modifier.weight(1f))
        Switch(
            checked = checked, onCheckedChange = onChange,
            colors = SwitchDefaults.colors(
                checkedThumbColor   = ObsidianVoid,
                checkedTrackColor   = accent,
                uncheckedThumbColor = TextMuted,
                uncheckedTrackColor = ObsidianDeep,
                uncheckedBorderColor = ObsidianEdge
            )
        )
    }
}

@Composable
private fun IvannaSlider(
    label: String, value: Float, min: Float, max: Float,
    color: Color = AuroraCyan, valueText: String? = null,
    steps: Int = 0, enabled: Boolean = true,
    onChanged: (Float) -> Unit
) {
    Column {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            Text(label, color = TextSecondary, style = MaterialTheme.typography.labelSmall)
            Text(
                valueText ?: "${"%.3f".format(value)}",
                color = if (enabled) color else TextMuted,
                style = MaterialTheme.typography.labelSmall,
                fontWeight = FontWeight.Bold,
                fontFamily = FontFamily.Monospace
            )
        }
        Slider(
            value = value, onValueChange = onChanged,
            valueRange = min..max, steps = steps, enabled = enabled,
            colors = SliderDefaults.colors(
                thumbColor          = if (enabled) color else TextMuted,
                activeTrackColor    = if (enabled) color else TextMuted,
                inactiveTrackColor  = ObsidianDeep
            )
        )
    }
}

@Composable
private fun InfoRow(label: String, value: String, color: Color = PhosphorGreen, mono: Boolean = false) {
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically) {
        Text(label, color = TextSecondary, style = MaterialTheme.typography.labelSmall)
        Text(value, color = color,
            style = MaterialTheme.typography.labelSmall,
            fontWeight = FontWeight.Bold,
            fontFamily = if (mono) FontFamily.Monospace else FontFamily.Default)
    }
}

@Composable
private fun MetricChip(label: String, value: String, color: Color) {
    Column(
        modifier = Modifier
            .clip(RoundedCornerShape(8.dp))
            .background(color.copy(0.08f))
            .padding(horizontal = 10.dp, vertical = 6.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Text(value, color = color, fontSize = 12.sp,
            fontWeight = FontWeight.ExtraBold, fontFamily = FontFamily.Monospace)
        Text(label, color = TextMuted, fontSize = 8.sp, letterSpacing = 0.5.sp)
    }
}

@Composable
private fun ExpertButton(label: String, color: Color, modifier: Modifier = Modifier, onClick: () -> Unit) {
    OutlinedButton(
        onClick  = onClick,
        modifier = modifier.height(42.dp),
        shape    = RoundedCornerShape(10.dp),
        colors   = ButtonDefaults.outlinedButtonColors(contentColor = color),
        border   = BorderStroke(1.dp, color.copy(0.4f))
    ) {
        Text(label, style = MaterialTheme.typography.labelSmall,
            fontWeight = FontWeight.ExtraBold, fontFamily = FontFamily.Monospace,
            letterSpacing = 0.5.sp)
    }
}
