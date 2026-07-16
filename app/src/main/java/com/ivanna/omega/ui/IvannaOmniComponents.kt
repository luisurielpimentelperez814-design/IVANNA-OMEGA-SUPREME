package com.ivanna.omega.ui

import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.tween
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.animateColorAsState
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.ui.theme.*
import kotlin.math.max
import kotlin.math.min

// ═══════════════════════════════════════════════════════════════════════════
//  HERO HEADER con anillo OMNI
// ═══════════════════════════════════════════════════════════════════════════
@Composable
internal fun OmniHeroHeader(
    omegaMode: Int,
    npeActive: Boolean,
    spatialActive: Boolean,
    autoMode: Boolean,
    omniLevel: Float
) {
    val infinite = rememberInfiniteTransition(label = "omniPulse")
    val pulse by infinite.animateFloat(
        initialValue = 0.85f,
        targetValue = 1f,
        animationSpec = infiniteRepeatable(tween(2200, easing = LinearEasing), RepeatMode.Reverse),
        label = "pulse"
    )
    val ringProgress by animateFloatAsState(targetValue = omniLevel.coerceIn(0f, 1f), label = "ringProgress")

    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        Box(contentAlignment = Alignment.Center, modifier = Modifier.size(72.dp)) {
            Canvas(modifier = Modifier.size(72.dp)) {
                val strokeW = 5.dp.toPx()
                drawArc(
                    color = ObsidianEdge,
                    startAngle = -90f, sweepAngle = 360f, useCenter = false,
                    style = Stroke(width = strokeW)
                )
                drawArc(
                    brush = Brush.sweepGradient(listOf(AuroraCyan, NeonMagenta, AmberSignal, AuroraCyan)),
                    startAngle = -90f, sweepAngle = 360f * ringProgress, useCenter = false,
                    style = Stroke(width = strokeW)
                )
            }
            Box(
                modifier = Modifier
                    .size((44 * pulse).dp)
                    .clip(CircleShape)
                    .background(
                        Brush.radialGradient(
                            listOf(OmniGoldCore.copy(alpha = 0.9f), AuroraCyan.copy(alpha = 0.25f), Color.Transparent)
                        )
                    )
            )
            Text("OMNI", style = MaterialTheme.typography.labelSmall, color = ObsidianDeep, fontWeight = FontWeight.Bold)
        }

        Column(modifier = Modifier.weight(1f)) {
            Text(
                "IVANNA",
                style = MaterialTheme.typography.displayMedium,
                color = TextPrimary
            )
            Text(
                "OMEGA · SUPREME",
                style = MaterialTheme.typography.titleMedium,
                color = AuroraCyan
            )
            Spacer(Modifier.height(6.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                StatusPill(
                    text = when (omegaMode) { 1 -> "OPE +NHO"; 2 -> "OPE +SPATIAL"; 3 -> "OPE +HRTF"; else -> "OPE DSP" },
                    accent = AuroraCyan
                )
                if (npeActive) StatusPill("NPE", NeonMagenta)
                if (spatialActive) StatusPill("BINAURAL", PhosphorGreen)
                if (autoMode) StatusPill("AUTO IA", AmberSignal)
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  HUD DE TELEMETRÍA con mini-sparkline
// ═══════════════════════════════════════════════════════════════════════════
@Composable
internal fun LiveTelemetryHud(
    rmsDb: Float,
    rmsHistory: List<Float>,
    agcDb: Float,
    genre: String,
    confidence: Float,
    thd: Float,
    evoFitness: Float,
    evoGeneration: Int
) {
    GlassCard(title = "TELEMETRÍA EN VIVO", accent = PhosphorGreen, subtitle = "NPE + Kernel Evolutivo · tiempo real") {
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            Box(
                modifier = Modifier
                    .weight(1.3f)
                    .height(46.dp)
                    .clip(RoundedCornerShape(10.dp))
                    .background(ObsidianDeep.copy(alpha = 0.55f))
                    .border(1.dp, PhosphorGreen.copy(alpha = 0.35f), RoundedCornerShape(10.dp))
            ) {
                Sparkline(
                    values = rmsHistory,
                    minVal = -60f,
                    maxVal = 0f,
                    color = PhosphorGreen,
                    modifier = Modifier.fillMaxSize().padding(6.dp)
                )
            }
            StatBlock("RMS", "%.1f dB".format(rmsDb), PhosphorGreen, Modifier.weight(1f))
            StatBlock("AGC", "%.1f dB".format(agcDb), AmberSignal, Modifier.weight(1f))
        }
        Spacer(Modifier.height(4.dp))
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            StatBlock("GÉNERO", genre, NeonMagenta, Modifier.weight(1.3f))
            StatBlock("CONF.", "%.0f%%".format(confidence * 100f), AuroraCyan, Modifier.weight(1f))
            StatBlock("GEN·FIT", "$evoGeneration · %.2f".format(evoFitness), AmberSignal, Modifier.weight(1.2f))
        }
    }
}

@Composable
private fun Sparkline(
    values: List<Float>,
    minVal: Float,
    maxVal: Float,
    color: Color,
    modifier: Modifier = Modifier
) {
    Canvas(modifier = modifier) {
        if (values.size < 2) return@Canvas
        val range = max(1e-3f, maxVal - minVal)
        val stepX = size.width / (values.size - 1)
        var prev: Offset? = null
        values.forEachIndexed { i, v ->
            val t = min(1f, max(0f, (v - minVal) / range))
            val point = Offset(i * stepX, size.height * (1f - t))
            prev?.let { p -> drawLine(color = color.copy(alpha = 0.85f), start = p, end = point, strokeWidth = 2.5f) }
            prev = point
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  MASTER BAR
// ═══════════════════════════════════════════════════════════════════════════
@Composable
internal fun MasterBar(
    antiDolbyEnabled: Boolean,
    onAntiDolbyChange: (Boolean) -> Unit,
    autoMode: Boolean,
    onAutoModeChange: (Boolean) -> Unit,
    onOpenVisualizer: () -> Unit,
    onOpenAdaptive: () -> Unit = {}
) {
    Column(
        modifier = Modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            FlagToggle("ANTI-DOLBY", antiDolbyEnabled, CoralWarn, Modifier.weight(1f), onAntiDolbyChange)
            FlagToggle("AUTO IA", autoMode, AmberSignal, Modifier.weight(1f), onAutoModeChange)
            Surface(
                onClick = onOpenVisualizer,
                modifier = Modifier.weight(1f).height(58.dp),
                shape = RoundedCornerShape(10.dp),
                color = AuroraCyan.copy(alpha = 0.14f),
                border = androidx.compose.foundation.BorderStroke(1.dp, AuroraCyan)
            ) {
                Row(
                    modifier = Modifier.fillMaxSize(),
                    horizontalArrangement = Arrangement.Center,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Icon(Icons.Filled.PlayArrow, contentDescription = null, tint = AuroraCyan)
                    Spacer(Modifier.width(4.dp))
                    Text("VISUALIZER", style = MaterialTheme.typography.labelMedium, color = AuroraCyan, fontWeight = FontWeight.Bold)
                }
            }
        }
        // Botón ADE — fila separada para no saturar la MasterBar
        Surface(
            onClick = onOpenAdaptive,
            modifier = Modifier.fillMaxWidth().height(46.dp),
            shape = RoundedCornerShape(10.dp),
            color = NeonMagenta.copy(alpha = 0.10f),
            border = androidx.compose.foundation.BorderStroke(1.dp, NeonMagenta.copy(alpha = 0.55f))
        ) {
            Row(
                modifier = Modifier.fillMaxSize(),
                horizontalArrangement = Arrangement.Center,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text("◈", color = NeonMagenta, fontSize = 14.sp)
                Spacer(Modifier.width(6.dp))
                Text(
                    "ADAPTIVE ENGINE",
                    style = MaterialTheme.typography.labelMedium,
                    color = NeonMagenta,
                    fontWeight = FontWeight.Bold,
                    letterSpacing = 1.5.sp
                )
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  GLASS CARD — doble borde (glow exterior + highlight especular superior)
// ═══════════════════════════════════════════════════════════════════════════
@Composable
internal fun GlassCard(
    title: String,
    accent: Color,
    subtitle: String? = null,
    rightSlot: (@Composable () -> Unit)? = null,
    content: @Composable ColumnScope.() -> Unit
) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(16.dp))
            .background(
                Brush.verticalGradient(
                    listOf(ObsidianGlass, ObsidianSoft.copy(alpha = 0.85f))
                )
            )
            .border(1.dp, accent.copy(alpha = 0.28f), RoundedCornerShape(16.dp))
            .border(1.dp, GlassHighlight, RoundedCornerShape(topStart = 16.dp, topEnd = 16.dp, bottomStart = 0.dp, bottomEnd = 0.dp))
            .padding(14.dp),
        verticalArrangement = Arrangement.spacedBy(10.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Box(
                        modifier = Modifier
                            .size(8.dp)
                            .clip(CircleShape)
                            .background(Brush.radialGradient(listOf(accent, accent.copy(alpha = 0.3f))))
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

// ═══════════════════════════════════════════════════════════════════════════
//  AURORA SLIDER — con glow detrás del thumb
// ═══════════════════════════════════════════════════════════════════════════
@Composable
internal fun AuroraSlider(
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
                displayValue?.invoke(value) ?: "%.2f%s".format(value, if (unit.isNotEmpty()) " $unit" else ""),
                style = MaterialTheme.typography.labelLarge,
                color = AuroraCyan
            )
        }
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .padding(top = 2.dp)
                .background(
                    Brush.horizontalGradient(listOf(AuroraCyanGlow.copy(alpha = 0.18f), Color.Transparent)),
                    RoundedCornerShape(8.dp)
                )
        ) {
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
}

// ═══════════════════════════════════════════════════════════════════════════
//  Resto de componentes atómicos
// ═══════════════════════════════════════════════════════════════════════════
@Composable
internal fun ToggleSwitch(checked: Boolean, onCheckedChange: (Boolean) -> Unit, accent: Color) {
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
internal fun FlagToggle(
    label: String,
    checked: Boolean,
    accent: Color,
    modifier: Modifier = Modifier,
    onCheckedChange: (Boolean) -> Unit
) {
    val bg by animateColorAsState(if (checked) accent.copy(alpha = 0.20f) else Color.Transparent, label = "flagBg")
    val border by animateColorAsState(if (checked) accent else ObsidianEdge, label = "flagBorder")
    val fg by animateColorAsState(if (checked) accent else TextMuted, label = "flagFg")
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
            Text(label, style = MaterialTheme.typography.labelSmall, color = fg, fontWeight = FontWeight.SemiBold)
            Text(if (checked) "ON" else "OFF", style = MaterialTheme.typography.labelMedium, color = fg, fontWeight = FontWeight.Bold)
        }
    }
}

@Composable
internal fun StatusPill(text: String, accent: Color) {
    Box(
        modifier = Modifier
            .clip(RoundedCornerShape(50))
            .background(accent.copy(alpha = 0.16f))
            .border(1.dp, accent.copy(alpha = 0.6f), RoundedCornerShape(50))
            .padding(horizontal = 10.dp, vertical = 4.dp)
    ) {
        Text(text, style = MaterialTheme.typography.labelSmall, color = accent, fontWeight = FontWeight.SemiBold)
    }
}

@Composable
internal fun StatBlock(label: String, value: String, accent: Color, modifier: Modifier = Modifier) {
    Column(
        modifier = modifier
            .clip(RoundedCornerShape(10.dp))
            .background(ObsidianDeep.copy(alpha = 0.55f))
            .border(1.dp, accent.copy(alpha = 0.35f), RoundedCornerShape(10.dp))
            .padding(vertical = 8.dp, horizontal = 10.dp)
    ) {
        Text(label, style = MaterialTheme.typography.labelSmall, color = TextMuted)
        Spacer(Modifier.height(2.dp))
        Text(value, style = MaterialTheme.typography.titleSmall, color = accent, fontWeight = FontWeight.Bold)
    }
}
