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
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.ivanna.omega.audio.ActiveRoute
import com.ivanna.omega.audio.IvannaEffectProfile
import com.ivanna.omega.audio.OmegaMetrics
import com.ivanna.omega.audio.PipelineState
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.neuromorphic.IvannaNpeEngine
import com.ivanna.omega.ui.theme.*
import kotlin.math.log10

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
    onOpenVisualizer: () -> Unit = {},
    onOpenAdaptive: () -> Unit = {},
    onOpenAdaptiveEngineManual: () -> Unit = {},
    onOpenProfiles: () -> Unit = {},
    onOpenMagisk: () -> Unit = {},
    metrics: OmegaMetrics = OmegaMetrics(),
    onMetricsUpdate: ((OmegaMetrics) -> Unit)? = null,
    adaptiveTelemetry: com.ivanna.omega.ui.AdaptiveTelemetrySnapshot = com.ivanna.omega.ui.AdaptiveTelemetrySnapshot(),
    adaptiveMode: com.ivanna.omega.audio.AdaptiveMode = com.ivanna.omega.audio.AdaptiveMode.NATURAL,
    onAdaptiveModeChange: (com.ivanna.omega.audio.AdaptiveMode) -> Unit = {},
    adaptiveIntensity: Float = 50f,
    onAdaptiveIntensityChange: (Float) -> Unit = {},
    voiceProtectionEnabled: Boolean = true,
    onVoiceProtectionChange: (Boolean) -> Unit = {},
    routeState: PipelineState = PipelineState(),
    modifier: Modifier = Modifier
) {
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

    val rmsHistory = remember { mutableStateListOf<Float>().apply { repeat(32) { add(-60f) } } }

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
            rmsHistory.removeAt(0)
            rmsHistory.add(npeRmsDb)
            kotlinx.coroutines.delay(750)
        }
    }

    LaunchedEffect(Unit) {
        kotlinx.coroutines.delay(1200)
        while (true) {
            try {
                evoFitness = IvannaNativeLib.nativeGetBestFitness().toFloat()
                evoGeneration = IvannaNativeLib.nativeGetGeneration()
            } catch (e: Throwable) {
                Log.w("IvannaControlPanel", "Kernel evolutivo no disponible todavía", e)
            }
            kotlinx.coroutines.delay(2000)
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

        SectionLabel("ESPACIAL Y NEUROMÓRFICO", NeonMagenta)

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
                StatBlock("CONF.", "%.0f%%".format(npeClassifyConfidence * 100f), PhosphorGreen, Modifier.weight(1f))
                StatBlock("ASPEREZA", "%.1f%%".format(npeClassifyThd), AmberSignal, Modifier.weight(1f))
            }
            Spacer(Modifier.height(4.dp))
            AuroraSlider("GANANCIA ARMÓNICA · NHO", npeHarmonic, 0f..2f, unit = "×") {
                npeHarmonic = it; onNpeHarmonicChange(it)
            }
            AuroraSlider("INHIBICIÓN LATERAL", npeLateralInhib, 0f..1f) {
                npeLateralInhib = it; onNpeLateralInhibChange(it)
            }
            AuroraSlider("COMPRESIÓN OHC", npeOhcCompression, 0f..1f) {
                npeOhcCompression = it; onNpeOhcCompressionChange(it)
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
            subtitle = "g_population · hilo de baja prioridad",
            rightSlot = {
                ToggleSwitch(evoEnabled, { evoEnabled = it; onEvoEnabledChange(it) }, AmberSignal)
            }
        ) {
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                StatBlock("GENERACIÓN", evoGeneration.toString(), AmberSignal, Modifier.weight(1f))
                StatBlock("FITNESS", "%.3f".format(evoFitness), PhosphorGreen, Modifier.weight(1f))
            }
        }

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
