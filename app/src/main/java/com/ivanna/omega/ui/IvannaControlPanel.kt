package com.ivanna.omega.ui

import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
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
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.ivanna.omega.audio.IvannaEffectProfile
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.neuromorphic.IvannaNpeEngine
import com.ivanna.omega.ui.theme.*
import kotlin.math.log10

/**
 * IvannaControlPanel v2.0 — Rediseño MAGISTRAL
 *
 * REGLA DE ORO: no se borra nada. Toda la lógica de callbacks es la misma
 * que en la versión v1.7 previa; sólo cambia la presentación:
 *  - Paleta "Aurora Obsidiana" con acentos cian/magenta/oro/aurora-verde
 *  - HUD superior en vivo (RMS, AGC, género, generación evolutiva, motor OPE)
 *  - Master bar con controles críticos siempre visibles (Anti-Dolby, Motor OPE,
 *    Presets, Auto IA)
 *  - Secciones organizadas en GlassCard translúcidas dejando ver el wallpaper
 *    OpenGL PBR audio-reactivo detrás.
 *  - Sliders custom AuroraSlider con lectura técnica formateada por unidad.
 *  - Chips de estado con color semántico (ON/OFF/AUTO).
 *
 * Se conservan absolutamente todas las funciones expuestas del motor:
 * DSP core (Exciter/EQ/Width) · Compresor · Motor OPE (DSP/NHO/Spatial/HRTF)
 * · Presets IvannaEffectProfile · Anti-Dolby · Auto IA · Kernel evolutivo
 * (arranque + fitness/generación) · Motor NPE completo (bypass, harmonic,
 * lateral inhib, OHC, master, AGC target/rate, HRTF/Coclear/Adapt flags,
 * métricas: género, RMS, AGC, confianza, THD) · Motor Binaural (upmix neural
 * + 32 objetos + head-tracking 6DoF) · Visualizador.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun IvannaControlPanel(
    initialExciter: Float,
    initialEq: Float,
    initialWidth: Float,
    initialAntiDolby: Boolean = false,
    initialPreset: String = "Warm",
    initialAutoMode: Boolean = false,
    initialOmegaMode: Int = 0,
    initialCompThreshold: Float = 0.5f,
    initialCompRatio: Float = 0.16f,
    initialNhoHarmonic: Float = 0.0f,
    initialSpatialAngle: Float = 0.5f,
    initialSpatialWidth: Float = 0.5f,
    initialEvoEnabled: Boolean = true,
    initialNpeBypass: Boolean = false,
    initialNpeHarmonic: Float = 0.2f,
    initialNpeLateralInhib: Float = 0.2f,
    initialNpeOhcCompression: Float = 0.3f,
    initialNpeMasterGain: Float = 0.0f,
    initialNpeAgcTarget: Float = -18.0f,
    initialNpeAgcRate: Float = 0.3f,
    initialNpeHrtf: Boolean = true,
    initialNpeCochlear: Boolean = true,
    initialNpeAdapt: Boolean = true,
    // NUEVO: motor coclear completo (Volterra H2) — opt-in, off por defecto
    initialNpeManifold: Boolean = false,
    initialSpatialEnabled: Boolean = false,
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
    onOpenVisualizer: () -> Unit = {}
) {
    // Estado UI (idéntico al v1.7)
    var exciter by remember { mutableFloatStateOf(initialExciter) }
    var eq by remember { mutableFloatStateOf(initialEq) }
    var width by remember { mutableFloatStateOf(initialWidth) }
    var antiDolbyEnabled by remember { mutableStateOf(initialAntiDolby) }
    var selectedPreset by remember { mutableStateOf(initialPreset) }
    var autoMode by remember { mutableStateOf(initialAutoMode) }
    var omegaMode by remember { mutableIntStateOf(initialOmegaMode) }
    var compThreshold by remember { mutableFloatStateOf(initialCompThreshold) }
    var compRatio by remember { mutableFloatStateOf(initialCompRatio) }
    var nhoHarmonic by remember { mutableFloatStateOf(initialNhoHarmonic) }
    var spatialAngle by remember { mutableFloatStateOf(initialSpatialAngle) }
    var spatialWidth by remember { mutableFloatStateOf(initialSpatialWidth) }
    var evoEnabled by remember { mutableStateOf(initialEvoEnabled) }
    var evoFitness by remember { mutableFloatStateOf(0f) }
    var evoGeneration by remember { mutableIntStateOf(0) }
    var npeBypass by remember { mutableStateOf(initialNpeBypass) }
    var npeHarmonic by remember { mutableFloatStateOf(initialNpeHarmonic) }
    var npeLateralInhib by remember { mutableFloatStateOf(initialNpeLateralInhib) }
    var npeOhcCompression by remember { mutableFloatStateOf(initialNpeOhcCompression) }
    var npeMasterGain by remember { mutableFloatStateOf(initialNpeMasterGain) }
    var npeAgcTarget by remember { mutableFloatStateOf(initialNpeAgcTarget) }
    var npeAgcRate by remember { mutableFloatStateOf(initialNpeAgcRate) }
    var npeHrtf by remember { mutableStateOf(initialNpeHrtf) }
    var npeCochlear by remember { mutableStateOf(initialNpeCochlear) }
    var npeAdapt by remember { mutableStateOf(initialNpeAdapt) }
    var npeManifold by remember { mutableStateOf(initialNpeManifold) }
    var npeGenre by remember { mutableStateOf("\u2014") }
    var npeRmsDb by remember { mutableFloatStateOf(-60f) }
    var npeAgcGainDb by remember { mutableFloatStateOf(0f) }
    var npeClassifyConfidence by remember { mutableFloatStateOf(0f) }
    var npeClassifyThd by remember { mutableFloatStateOf(0f) }
    var spatialEnabled by remember { mutableStateOf(initialSpatialEnabled) }

    // [FIX-STARTUP-LAG] Telemetría del motor NPE.
    // Antes: 1s exactos + arranque inmediato -> pico de JNI calls justo en
    // los primeros segundos de la app, coincidiendo con el warmup del hilo
    // de audio. Ahora: se retrasa 800ms el primer poll (deja arrancar todo
    // limpio) y luego pollea cada 750ms (más fluido para el HUD).
    LaunchedEffect(Unit) {
        kotlinx.coroutines.delay(800)
        while (true) {
            npeGenre = IvannaNpeEngine.getDetectedGenre()
            val m = IvannaNpeEngine.getMetrics()
            val rmsLin = m.getOrElse(1) { 0f }
            npeRmsDb = if (rmsLin > 1e-6f) (20f * log10(rmsLin)) else -60f
            val agcLin = m.getOrElse(2) { 1f }
            npeAgcGainDb = if (agcLin > 1e-6f) (20f * log10(agcLin)) else 0f
            val c = IvannaNpeEngine.getSynthClassify()
            npeClassifyConfidence = c.getOrElse(1) { 0f }
            npeClassifyThd = c.getOrElse(2) { 0f }
            kotlinx.coroutines.delay(750)
        }
    }

    // Telemetría kernel evolutivo — el fitness cambia mucho más lento que las
    // métricas de audio, así que polleamos cada 2s (mitad de carga JNI que antes).
    LaunchedEffect(Unit) {
        kotlinx.coroutines.delay(1200)
        while (true) {
            evoFitness = IvannaNativeLib.nativeGetBestFitness().toFloat()
            evoGeneration = IvannaNativeLib.nativeGetGeneration()
            kotlinx.coroutines.delay(2000)
        }
    }

    // ═══════════════════════════════════════════════════════════════════════
    //  LAYOUT MAGISTRAL
    // ═══════════════════════════════════════════════════════════════════════
    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(
                Brush.verticalGradient(
                    0f to ObsidianDeep.copy(alpha = 0.55f),
                    0.5f to ObsidianDeep.copy(alpha = 0.35f),
                    1f to ObsidianDeep.copy(alpha = 0.75f)
                )
            )
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 16.dp, vertical = 20.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp)
    ) {
        // ── Marca + versión + estado global ─────────────────────────────
        HeroHeader(
            omegaMode = omegaMode,
            npeActive = !npeBypass,
            spatialActive = spatialEnabled,
            autoMode = autoMode
        )

        // ── HUD de telemetría en vivo ───────────────────────────────────
        LiveTelemetryHud(
            rmsDb = npeRmsDb,
            agcDb = npeAgcGainDb,
            genre = npeGenre,
            confidence = npeClassifyConfidence,
            thd = npeClassifyThd,
            evoFitness = evoFitness,
            evoGeneration = evoGeneration
        )

        // ── Master Bar: Anti-Dolby + Auto IA + visualizer ───────────────
        MasterBar(
            antiDolbyEnabled = antiDolbyEnabled,
            onAntiDolbyChange = { enabled ->
                antiDolbyEnabled = enabled
                onAntiDolbyChange(enabled)
            },
            autoMode = autoMode,
            onAutoModeChange = { enabled ->
                autoMode = enabled
                onAutoModeChange(enabled)
            },
            onOpenVisualizer = onOpenVisualizer
        )

        // ── Motor OPE (segmentado) ──────────────────────────────────────
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
                            activeContainerColor = AuroraCyan.copy(alpha = 0.18f),
                            activeContentColor = AuroraCyan,
                            activeBorderColor = AuroraCyan,
                            inactiveContainerColor = Color.Transparent,
                            inactiveContentColor = TextSecondary,
                            inactiveBorderColor = ObsidianEdge
                        )
                    ) { Text(label, fontWeight = FontWeight.SemiBold) }
                }
            }
        }

        // ── Presets ─────────────────────────────────────────────────────
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
                        onClick = {
                            selectedPreset = name
                            onPresetSelected(name)
                        },
                        label = { Text(name, fontWeight = FontWeight.Medium) },
                        colors = FilterChipDefaults.filterChipColors(
                            containerColor = Color.Transparent,
                            labelColor = TextSecondary,
                            selectedContainerColor = NeonMagenta.copy(alpha = 0.22f),
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

        // ── DSP Core ────────────────────────────────────────────────────
        GlassCard(title = "DSP CORE", accent = AuroraCyan, subtitle = "EQ · Exciter · Widener · Gain") {
            AuroraSlider(
                label = "EXCITER",
                value = exciter,
                range = 0f..1f,
                unit = "×",
                onValueChange = { exciter = it; onExciterChange(it) }
            )
            AuroraSlider(
                label = "EQ GAIN",
                value = eq,
                range = -18f..18f,
                unit = "dB",
                onValueChange = { eq = it; onEqChange(it) }
            )
            AuroraSlider(
                label = "STEREO WIDTH",
                value = width,
                range = 0f..1.5f,
                unit = "γ",
                onValueChange = { width = it; onWidthChange(it) }
            )
        }

        // ── Compresor ───────────────────────────────────────────────────
        GlassCard(title = "COMPRESOR", accent = AmberSignal, subtitle = "g_comp · dinámica lock-free") {
            AuroraSlider(
                label = "THRESHOLD",
                value = compThreshold,
                range = 0f..1f,
                displayValue = { "%.1f dB".format(-24f + it * 24f) },
                onValueChange = { compThreshold = it; onCompThresholdChange(it) }
            )
            AuroraSlider(
                label = "RATIO",
                value = compRatio,
                range = 0f..1f,
                displayValue = { "%.1f:1".format(1f + it * 19f) },
                onValueChange = { compRatio = it; onCompRatioChange(it) }
            )
        }

        // ── NHO / Espacial (PDEngine g_pd) ──────────────────────────────
        GlassCard(
            title = "NHO / ESPACIAL",
            accent = PhosphorGreen,
            subtitle = "PDEngine g_pd · activo en +NHO / +Spatial"
        ) {
            AuroraSlider(
                label = "GANANCIA ARMÓNICA (NHO)",
                value = nhoHarmonic,
                range = 0f..1f,
                unit = "",
                onValueChange = { nhoHarmonic = it; onNhoHarmonicChange(it) }
            )
            AuroraSlider(
                label = "ÁNGULO ESPACIAL",
                value = spatialAngle,
                range = 0f..1.33f,
                unit = "rad",
                onValueChange = { spatialAngle = it; onSpatialAngleChange(it) }
            )
            AuroraSlider(
                label = "ANCHO ESPACIAL",
                value = spatialWidth,
                range = 0f..1.5f,
                unit = "",
                onValueChange = { spatialWidth = it; onSpatialWidthChange(it) }
            )
        }

        // ── Kernel Evolutivo ────────────────────────────────────────────
        GlassCard(
            title = "KERNEL EVOLUTIVO",
            accent = AmberSignal,
            subtitle = "g_population · hilo de baja prioridad",
            rightSlot = {
                ToggleSwitch(
                    checked = evoEnabled,
                    onCheckedChange = { evoEnabled = it; onEvoEnabledChange(it) },
                    accent = AmberSignal
                )
            }
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                StatBlock(
                    label = "GENERACIÓN",
                    value = evoGeneration.toString(),
                    accent = AmberSignal,
                    modifier = Modifier.weight(1f)
                )
                StatBlock(
                    label = "FITNESS",
                    value = "%.3f".format(evoFitness),
                    accent = PhosphorGreen,
                    modifier = Modifier.weight(1f)
                )
            }
        }

        // ── Motor NPE ───────────────────────────────────────────────────
        GlassCard(
            title = "MOTOR NPE · NEUROMÓRFICO",
            accent = AuroraCyan,
            subtitle = "NHO + LIF + BiquadEnvelopeBank + AutonomousBrain",
            rightSlot = {
                ToggleSwitch(
                    checked = !npeBypass,
                    onCheckedChange = { on -> npeBypass = !on; onNpeBypassChange(!on) },
                    accent = AuroraCyan
                )
            }
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                StatBlock(
                    label = "GÉNERO",
                    value = npeGenre,
                    accent = NeonMagenta,
                    modifier = Modifier.weight(1.4f)
                )
                StatBlock(
                    label = "CONF.",
                    value = "%.0f%%".format(npeClassifyConfidence * 100f),
                    accent = PhosphorGreen,
                    modifier = Modifier.weight(1f)
                )
                StatBlock(
                    label = "THD",
                    value = "%.1f%%".format(npeClassifyThd),
                    accent = AmberSignal,
                    modifier = Modifier.weight(1f)
                )
            }
            Spacer(Modifier.height(4.dp))
            AuroraSlider(
                label = "GANANCIA ARMÓNICA · NHO",
                value = npeHarmonic,
                range = 0f..2f,
                unit = "×",
                onValueChange = { npeHarmonic = it; onNpeHarmonicChange(it) }
            )
            AuroraSlider(
                label = "INHIBICIÓN LATERAL",
                value = npeLateralInhib,
                range = 0f..1f,
                unit = "",
                onValueChange = { npeLateralInhib = it; onNpeLateralInhibChange(it) }
            )
            AuroraSlider(
                label = "COMPRESIÓN OHC",
                value = npeOhcCompression,
                range = 0f..1f,
                unit = "",
                onValueChange = { npeOhcCompression = it; onNpeOhcCompressionChange(it) }
            )
            AuroraSlider(
                label = "MASTER GAIN",
                value = npeMasterGain,
                range = -18f..18f,
                unit = "dB",
                onValueChange = { npeMasterGain = it; onNpeMasterGainChange(it) }
            )
            AuroraSlider(
                label = "AGC TARGET",
                value = npeAgcTarget,
                range = -36f..0f,
                unit = "dB",
                onValueChange = { npeAgcTarget = it; onNpeAgcChange(it, npeAgcRate) }
            )
            AuroraSlider(
                label = "AGC RATE",
                value = npeAgcRate,
                range = 0f..1f,
                unit = "",
                onValueChange = { npeAgcRate = it; onNpeAgcChange(npeAgcTarget, it) }
            )
            Spacer(Modifier.height(4.dp))
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
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
            // NUEVO: motor coclear completo (Volterra H2 + upsampling) — opt-in,
            // en paralelo, no reemplaza el toggle COCLEAR (envBank_) de arriba.
            FlagToggle("MANIFOLD (Volterra H2)", npeManifold, AuroraCyan, Modifier.fillMaxWidth()) {
                npeManifold = it; onNpeManifoldChange(it)
            }
        }

        // ── Motor Binaural ──────────────────────────────────────────────
        GlassCard(
            title = "MOTOR ESPACIAL BINAURAL",
            accent = NeonMagenta,
            subtitle = "Upmix neural 4-stems · 32 objetos · head-tracking 6DoF",
            rightSlot = {
                ToggleSwitch(
                    checked = spatialEnabled,
                    onCheckedChange = { spatialEnabled = it; onSpatialEnabledChange(it) },
                    accent = NeonMagenta
                )
            }
        ) {
            Text(
                "Opt-in: usa giroscopio a 100Hz + separación neural de stems. " +
                        "Recomendado usar audífonos. Consume ~15% CPU adicional.",
                style = MaterialTheme.typography.bodySmall,
                color = TextSecondary
            )
        }

        // ── Footer ──────────────────────────────────────────────────────
        Spacer(Modifier.height(4.dp))
        Text(
            "IVANNA-OMEGA-SUPREME v1.7 · © 2025-2026 Luis Uriel Pimentel Pérez",
            style = MaterialTheme.typography.labelSmall,
            color = TextMuted,
            modifier = Modifier.fillMaxWidth(),
            textAlign = TextAlign.Center
        )
        Spacer(Modifier.height(20.dp))
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  COMPONENTES CUSTOM
// ═══════════════════════════════════════════════════════════════════════════

@Composable
private fun HeroHeader(
    omegaMode: Int,
    npeActive: Boolean,
    spatialActive: Boolean,
    autoMode: Boolean
) {
    val pulse by rememberInfiniteTransition(label = "pulse").animateFloat(
        initialValue = 0.4f,
        targetValue = 1f,
        animationSpec = infiniteRepeatable(
            animation = tween(1600, easing = LinearEasing),
            repeatMode = RepeatMode.Reverse
        ),
        label = "pulseAlpha"
    )
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(20.dp))
            .background(
                Brush.horizontalGradient(
                    0f to ObsidianGlass,
                    0.5f to ObsidianSoft.copy(alpha = 0.85f),
                    1f to ObsidianGlass
                )
            )
            .border(
                1.dp,
                Brush.horizontalGradient(
                    0f to AuroraCyan.copy(alpha = 0.5f),
                    0.5f to NeonMagenta.copy(alpha = 0.4f),
                    1f to AmberSignal.copy(alpha = 0.5f)
                ),
                RoundedCornerShape(20.dp)
            )
            .padding(horizontal = 20.dp, vertical = 18.dp)
    ) {
        Row(
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            Box(
                modifier = Modifier
                    .size(10.dp)
                    .clip(CircleShape)
                    .background(PhosphorGreen.copy(alpha = pulse))
            )
            Text(
                "IVANNA",
                style = MaterialTheme.typography.displayMedium,
                color = TextPrimary
            )
            Text(
                "OMEGA · SUPREME",
                style = MaterialTheme.typography.headlineMedium,
                color = AuroraCyan
            )
        }
        Spacer(Modifier.height(6.dp))
        Text(
            "MOTOR NEUROMÓRFICO ESPACIAL · v1.7",
            style = MaterialTheme.typography.labelMedium,
            color = TextSecondary
        )
        Spacer(Modifier.height(12.dp))
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            StatusPill(
                text = when (omegaMode) {
                    1 -> "OPE · +NHO"; 2 -> "OPE · +SPATIAL"; 3 -> "OPE · +HRTF"
                    else -> "OPE · DSP"
                },
                accent = AuroraCyan
            )
            StatusPill(text = if (npeActive) "NPE · ON" else "NPE · OFF",
                accent = if (npeActive) PhosphorGreen else TextMuted)
            StatusPill(text = if (spatialActive) "BIN · 6DoF" else "BIN · OFF",
                accent = if (spatialActive) NeonMagenta else TextMuted)
            if (autoMode) StatusPill(text = "AUTO IA", accent = AmberSignal)
        }
    }
}

@Composable
private fun LiveTelemetryHud(
    rmsDb: Float,
    agcDb: Float,
    genre: String,
    confidence: Float,
    thd: Float,
    evoFitness: Float,
    evoGeneration: Int
) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(16.dp))
            .background(ObsidianGlass)
            .border(1.dp, ObsidianEdge, RoundedCornerShape(16.dp))
            .padding(14.dp),
        verticalArrangement = Arrangement.spacedBy(10.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text("TELEMETRÍA EN VIVO",
                style = MaterialTheme.typography.titleSmall,
                color = TextSecondary)
            Text("● LIVE",
                style = MaterialTheme.typography.labelSmall,
                color = PhosphorGreen)
        }
        // Barra RMS
        MeterRow(label = "RMS", valueText = "%.1f dB".format(rmsDb),
            fill = ((rmsDb + 60f) / 60f).coerceIn(0f, 1f),
            barColor = AuroraCyan)
        MeterRow(label = "AGC", valueText = "%+.1f dB".format(agcDb),
            fill = ((agcDb + 18f) / 36f).coerceIn(0f, 1f),
            barColor = NeonMagenta)
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            StatBlock(label = "GÉNERO", value = genre, accent = AuroraCyan,
                modifier = Modifier.weight(1.4f))
            StatBlock(label = "CONF.", value = "%.0f%%".format(confidence * 100f),
                accent = PhosphorGreen, modifier = Modifier.weight(1f))
            StatBlock(label = "THD", value = "%.1f%%".format(thd),
                accent = AmberSignal, modifier = Modifier.weight(1f))
        }
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            StatBlock(label = "GENERACIÓN", value = evoGeneration.toString(),
                accent = AmberSignal, modifier = Modifier.weight(1f))
            StatBlock(label = "FITNESS", value = "%.3f".format(evoFitness),
                accent = PhosphorGreen, modifier = Modifier.weight(1f))
        }
    }
}

@Composable
private fun MasterBar(
    antiDolbyEnabled: Boolean,
    onAntiDolbyChange: (Boolean) -> Unit,
    autoMode: Boolean,
    onAutoModeChange: (Boolean) -> Unit,
    onOpenVisualizer: () -> Unit
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(16.dp))
            .background(ObsidianSoft.copy(alpha = 0.85f))
            .border(
                1.dp,
                Brush.horizontalGradient(
                    0f to AuroraCyan.copy(alpha = 0.5f),
                    1f to NeonMagenta.copy(alpha = 0.5f)
                ),
                RoundedCornerShape(16.dp)
            )
            .padding(12.dp),
        horizontalArrangement = Arrangement.spacedBy(10.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        MasterChip(
            label = "ANTI-DOLBY",
            active = antiDolbyEnabled,
            accent = NeonMagenta,
            modifier = Modifier.weight(1f),
            onClick = { onAntiDolbyChange(!antiDolbyEnabled) }
        )
        MasterChip(
            label = "AUTO IA",
            active = autoMode,
            accent = AmberSignal,
            modifier = Modifier.weight(1f),
            onClick = { onAutoModeChange(!autoMode) }
        )
        MasterChip(
            label = "VISUAL",
            active = false,
            accent = AuroraCyan,
            icon = Icons.Default.PlayArrow,
            modifier = Modifier.weight(1f),
            onClick = onOpenVisualizer
        )
    }
}

@Composable
private fun MasterChip(
    label: String,
    active: Boolean,
    accent: Color,
    modifier: Modifier = Modifier,
    icon: androidx.compose.ui.graphics.vector.ImageVector? = null,
    onClick: () -> Unit
) {
    val bg by animateColorAsState(
        targetValue = if (active) accent.copy(alpha = 0.22f) else Color.Transparent,
        label = "chipBg"
    )
    val border by animateColorAsState(
        targetValue = if (active) accent else ObsidianEdge,
        label = "chipBorder"
    )
    val fg by animateColorAsState(
        targetValue = if (active) accent else TextSecondary,
        label = "chipFg"
    )
    Surface(
        onClick = onClick,
        modifier = modifier.height(46.dp),
        shape = RoundedCornerShape(12.dp),
        color = bg,
        border = androidx.compose.foundation.BorderStroke(1.dp, border)
    ) {
        Row(
            modifier = Modifier.fillMaxSize(),
            horizontalArrangement = Arrangement.Center,
            verticalAlignment = Alignment.CenterVertically
        ) {
            if (icon != null) {
                Icon(icon, contentDescription = null, tint = fg, modifier = Modifier.size(16.dp))
                Spacer(Modifier.width(6.dp))
            }
            Text(label, style = MaterialTheme.typography.labelLarge, color = fg,
                fontWeight = FontWeight.Bold)
        }
    }
}

@Composable
private fun GlassCard(
    title: String,
    accent: Color,
    subtitle: String? = null,
    rightSlot: (@Composable () -> Unit)? = null,
    content: @Composable ColumnScope.() -> Unit
) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(18.dp))
            .background(ObsidianGlass)
            .border(
                1.dp,
                Brush.verticalGradient(
                    0f to accent.copy(alpha = 0.55f),
                    1f to ObsidianEdge
                ),
                RoundedCornerShape(18.dp)
            )
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(10.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Row(verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Box(
                        modifier = Modifier
                            .size(8.dp)
                            .clip(CircleShape)
                            .background(accent)
                    )
                    Text(title, style = MaterialTheme.typography.titleMedium, color = TextPrimary)
                }
                if (subtitle != null) {
                    Spacer(Modifier.height(2.dp))
                    Text(subtitle, style = MaterialTheme.typography.bodySmall, color = TextSecondary)
                }
            }
            if (rightSlot != null) rightSlot()
        }
        content()
    }
}

@Composable
private fun AuroraSlider(
    label: String,
    value: Float,
    range: ClosedFloatingPointRange<Float>,
    unit: String = "",
    displayValue: ((Float) -> String)? = null,
    onValueChange: (Float) -> Unit
) {
    Column(modifier = Modifier.fillMaxWidth()) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(label, style = MaterialTheme.typography.labelMedium, color = TextSecondary)
            Text(
                displayValue?.invoke(value) ?: "%.2f%s".format(value,
                    if (unit.isNotEmpty()) " $unit" else ""),
                style = MaterialTheme.typography.labelLarge,
                color = AuroraCyan
            )
        }
        Slider(
            value = value,
            onValueChange = onValueChange,
            valueRange = range,
            colors = SliderDefaults.colors(
                thumbColor = AuroraCyan,
                activeTrackColor = AuroraCyan,
                inactiveTrackColor = ObsidianEdge,
                activeTickColor = Color.Transparent,
                inactiveTickColor = Color.Transparent
            ),
            modifier = Modifier.fillMaxWidth()
        )
    }
}

@Composable
private fun ToggleSwitch(
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit,
    accent: Color
) {
    Switch(
        checked = checked,
        onCheckedChange = onCheckedChange,
        colors = SwitchDefaults.colors(
            checkedThumbColor = ObsidianDeep,
            checkedTrackColor = accent,
            checkedBorderColor = accent,
            uncheckedThumbColor = TextMuted,
            uncheckedTrackColor = Color.Transparent,
            uncheckedBorderColor = ObsidianEdge
        )
    )
}

@Composable
private fun FlagToggle(
    label: String,
    checked: Boolean,
    accent: Color,
    modifier: Modifier = Modifier,
    onCheckedChange: (Boolean) -> Unit
) {
    val bg by animateColorAsState(
        targetValue = if (checked) accent.copy(alpha = 0.18f) else Color.Transparent,
        label = "flagBg"
    )
    val border by animateColorAsState(
        targetValue = if (checked) accent else ObsidianEdge, label = "flagBorder")
    val fg by animateColorAsState(
        targetValue = if (checked) accent else TextMuted, label = "flagFg")
    Surface(
        onClick = { onCheckedChange(!checked) },
        modifier = modifier.height(58.dp),
        shape = RoundedCornerShape(10.dp),
        color = bg,
        border = androidx.compose.foundation.BorderStroke(1.dp, border)
    ) {
        Column(
            modifier = Modifier.fillMaxSize().padding(6.dp),
            verticalArrangement = Arrangement.Center,
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Text(label, style = MaterialTheme.typography.labelSmall, color = fg,
                fontWeight = FontWeight.SemiBold)
            Text(if (checked) "ON" else "OFF",
                style = MaterialTheme.typography.labelMedium, color = fg,
                fontWeight = FontWeight.Bold)
        }
    }
}

@Composable
private fun StatusPill(text: String, accent: Color) {
    Box(
        modifier = Modifier
            .clip(RoundedCornerShape(50))
            .background(accent.copy(alpha = 0.16f))
            .border(1.dp, accent.copy(alpha = 0.6f), RoundedCornerShape(50))
            .padding(horizontal = 10.dp, vertical = 4.dp)
    ) {
        Text(text, style = MaterialTheme.typography.labelSmall, color = accent,
            fontWeight = FontWeight.SemiBold)
    }
}

@Composable
private fun StatBlock(
    label: String,
    value: String,
    accent: Color,
    modifier: Modifier = Modifier
) {
    Column(
        modifier = modifier
            .clip(RoundedCornerShape(10.dp))
            .background(ObsidianDeep.copy(alpha = 0.55f))
            .border(1.dp, accent.copy(alpha = 0.35f), RoundedCornerShape(10.dp))
            .padding(vertical = 8.dp, horizontal = 10.dp)
    ) {
        Text(label, style = MaterialTheme.typography.labelSmall, color = TextMuted)
        Spacer(Modifier.height(2.dp))
        Text(value, style = MaterialTheme.typography.titleSmall, color = accent,
            fontWeight = FontWeight.Bold)
    }
}

@Composable
private fun MeterRow(
    label: String,
    valueText: String,
    fill: Float,
    barColor: Color
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(10.dp)
    ) {
        Text(label, style = MaterialTheme.typography.labelMedium, color = TextSecondary,
            modifier = Modifier.width(38.dp))
        Box(
            modifier = Modifier
                .weight(1f)
                .height(10.dp)
                .clip(RoundedCornerShape(6.dp))
                .background(ObsidianDeep)
        ) {
            Box(
                modifier = Modifier
                    .fillMaxHeight()
                    .fillMaxWidth(fill)
                    .background(
                        Brush.horizontalGradient(
                            0f to barColor.copy(alpha = 0.5f),
                            1f to barColor
                        )
                    )
            )
        }
        Text(valueText, style = MaterialTheme.typography.labelLarge, color = barColor,
            modifier = Modifier.width(78.dp), textAlign = TextAlign.End)
    }
}

// Fin del archivo.
