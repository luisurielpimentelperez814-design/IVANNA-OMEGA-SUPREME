package com.ivanna.omega.ui

import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.tween
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.ivanna.omega.ui.theme.*

// ═══════════════════════════════════════════════════════════════════════════
//  ADAPTIVE CONTROL CENTER
//
//  Toda la telemetría de este archivo viene de un único origen real:
//  IvannaNativeLib.nativeGetAdaptiveTelemetry() (AdaptiveDecisionEngine, el
//  motor cerrado en fases previas de esta sesión — ver commits e1286d7,
//  0b25913, 280f3f8). No hay datos inventados: AdaptiveTelemetrySnapshot es
//  un espejo 1:1 del FloatArray de 10 posiciones que ese endpoint devuelve.
//
//  Auditoría de cableado (Fase 5 del prompt) de cada control:
//    - Adaptive Mode (OFF/NATURAL/STUDIO/EXTREME): SIN backend. El motor no
//      tiene concepto de "modo" — es un engine de decisión continua, no
//      basado en presets. UI preparada + TODO explícito, no se inventa
//      comportamiento.
//    - Intensity 0-100%: SIN backend. No existe ningún escalar de
//      "intensidad global" en AdaptiveDecisionEngine ni en su bus de
//      estado. UI preparada + TODO explícito.
//    - Spatial Control 0-150%: SÍ tiene backend real — ParameterStore.
//      setSpatialWidth()/getSpatialWidth(), la MISMA ruta que ya usaba el
//      slider manual de ancho estéreo en IvannaControlPanel. Distinto
//      concepto de "Spatial Intelligence" (telemetría, decisión calculada
//      por el motor) — acá el usuario fija la base que el motor modula.
//    - Voice Protection ON/OFF: SÍ tiene backend real —
//      IvannaBridgePlayer.setVoiceProtectionEnabled(), que delega a
//      VoiceProtectionController.enabled (confirmado que SÍ gatea feed()
//      de verdad, no es un flag muerto).
// ═══════════════════════════════════════════════════════════════════════════

/** Espejo 1:1 del FloatArray de nativeGetAdaptiveTelemetry() (10 posiciones). */
data class AdaptiveTelemetrySnapshot(
    val running: Boolean = false,
    val rms: Float = 0f,
    val peak: Float = 0f,
    val gainReductionDb: Float = 0f,
    val targetGain: Float = 1f,
    val compressorAmount: Float = 0f,
    val exciterReduction: Float = 0f,
    val spatialWidth: Float = 1f,
    val safetyMargin: Float = 1f,
    val voiceProtectionAmount: Float = 0f,
    val appliedCount: Long = 0L
) {
    companion object {
        /** Parseo directo del array crudo — misma indexación que el log ya existente en MainActivity. */
        fun fromArray(t: FloatArray?, running: Boolean): AdaptiveTelemetrySnapshot {
            if (t == null || t.size < 10) return AdaptiveTelemetrySnapshot(running = running)
            return AdaptiveTelemetrySnapshot(
                running = running,
                rms = t[0], peak = t[1], gainReductionDb = t[2],
                targetGain = t[3], compressorAmount = t[4], exciterReduction = t[5],
                spatialWidth = t[6], safetyMargin = t[7], voiceProtectionAmount = t[8],
                appliedCount = t[9].toLong()
            )
        }
    }
}

enum class AdaptiveMode(val label: String) {
    OFF("OFF"), NATURAL("NATURAL"), STUDIO("STUDIO"), EXTREME("EXTREME")
}

// ── Tarjeta de estado + telemetría (Fase 2) ─────────────────────────────────

@Composable
fun AdaptiveEngineStatusCard(
    telemetry: AdaptiveTelemetrySnapshot,
    modifier: Modifier = Modifier
) {
    GlassCard(
        title = "IVANNA ADAPTIVE ENGINE",
        accent = if (telemetry.running) PhosphorGreen else TextMuted,
        subtitle = "Motor de decisión en tiempo real",
        rightSlot = { AdaptiveStatusPill(online = telemetry.running) }
    ) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            AdaptiveLevelMeter("RMS", telemetry.rms, AuroraCyan, Modifier.weight(1f))
            AdaptiveLevelMeter("PEAK", telemetry.peak, if (telemetry.peak > 0.95f) CoralWarn else NeonMagenta, Modifier.weight(1f))
        }

        Spacer(Modifier.height(2.dp))

        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            StatBlock("DYNAMIC GAIN", "%.0f%%".format(telemetry.targetGain * 100f), AuroraCyan, Modifier.weight(1f))
            StatBlock("COMPRESSION", "%.0f%%".format(telemetry.compressorAmount * 100f), NeonMagenta, Modifier.weight(1f))
            StatBlock("EXCITER PROT.", "%.0f%%".format(telemetry.exciterReduction * 100f), AmberSignal, Modifier.weight(1f))
        }
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            StatBlock("SPATIAL INTEL.", "%.0f%%".format(telemetry.spatialWidth * 100f), AuroraCyan, Modifier.weight(1f))
            StatBlock("VOICE PROT.", "%.0f%%".format(telemetry.voiceProtectionAmount * 100f), PhosphorGreen, Modifier.weight(1f))
            StatBlock(
                "SAFETY MARGIN", "%.0f%%".format(telemetry.safetyMargin * 100f),
                if (telemetry.safetyMargin < 0.3f) CoralWarn else PhosphorGreen, Modifier.weight(1f)
            )
        }

        Spacer(Modifier.height(2.dp))
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            Text(
                "Gain reduction: %.1f dB".format(telemetry.gainReductionDb),
                style = MaterialTheme.typography.labelSmall, color = TextMuted
            )
            Text(
                "Blocks aplicados: ${telemetry.appliedCount}",
                style = MaterialTheme.typography.labelSmall, color = TextMuted
            )
        }
    }
}

/** Punto pulsante estilo "breathing" — ONLINE late suave, OFFLINE estático. */
@Composable
private fun AdaptiveStatusPill(online: Boolean) {
    val infinite = rememberInfiniteTransition(label = "adaptivePulse")
    val pulse by infinite.animateFloat(
        initialValue = 0.4f,
        targetValue = 1f,
        animationSpec = infiniteRepeatable(
            animation = tween(1100, easing = LinearEasing),
            repeatMode = RepeatMode.Reverse
        ),
        label = "pulseAlpha"
    )
    val accent = if (online) PhosphorGreen else TextMuted
    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        Box(
            modifier = Modifier
                .size(8.dp)
                .clip(CircleShape)
                .background(accent.copy(alpha = if (online) pulse else 0.5f))
        )
        StatusPill(if (online) "ONLINE" else "OFFLINE", accent)
    }
}

/** Barra de nivel horizontal animada — RMS/Peak con umbral de color. */
@Composable
private fun AdaptiveLevelMeter(label: String, value: Float, accent: Color, modifier: Modifier = Modifier) {
    val clamped = value.coerceIn(0f, 1.2f) / 1.2f
    val animatedFraction by animateFloatAsState(targetValue = clamped, label = "levelMeter_$label")
    Column(modifier) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            Text(label, style = MaterialTheme.typography.labelSmall, color = TextSecondary)
            Text("%.2f".format(value), style = MaterialTheme.typography.labelSmall, color = accent)
        }
        Box(
            Modifier
                .fillMaxWidth()
                .height(6.dp)
                .padding(top = 3.dp)
                .clip(RoundedCornerShape(4.dp))
                .background(ObsidianEdge.copy(alpha = 0.5f))
        ) {
            Box(
                Modifier
                    .fillMaxWidth(animatedFraction.coerceIn(0f, 1f))
                    .fillMaxHeight()
                    .clip(RoundedCornerShape(4.dp))
                    .background(Brush.horizontalGradient(listOf(accent.copy(alpha = 0.5f), accent)))
            )
        }
    }
}

// ── Tarjeta de controles (Fase 3) ────────────────────────────────────────────

@Composable
fun AdaptiveControlsCard(
    mode: AdaptiveMode,
    onModeChange: (AdaptiveMode) -> Unit,
    intensity: Float,
    onIntensityChange: (Float) -> Unit,
    spatialControlPercent: Float,
    onSpatialControlChange: (Float) -> Unit,
    voiceProtectionEnabled: Boolean,
    onVoiceProtectionChange: (Boolean) -> Unit,
    modifier: Modifier = Modifier
) {
    GlassCard(
        title = "ADAPTIVE CONTROLS",
        accent = AuroraCyan,
        subtitle = "Ajustes del motor adaptativo"
    ) {
        Text("Adaptive Mode", style = MaterialTheme.typography.labelMedium, color = TextSecondary)
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            AdaptiveMode.values().forEach { m ->
                ModeChip(
                    label = m.label,
                    selected = mode == m,
                    modifier = Modifier.weight(1f),
                    onClick = { onModeChange(m) }
                )
            }
        }
        PendingBackendNote()

        AuroraSlider(
            label = "Intensity",
            value = intensity,
            range = 0f..100f,
            unit = "%",
            onValueChange = onIntensityChange
        )
        PendingBackendNote()

        // REAL: mapea a ParameterStore.setSpatialWidth(), la misma ruta que
        // ya consume el slider manual de ancho estéreo — no un motor
        // paralelo, el control que ya existía, expuesto acá también.
        AuroraSlider(
            label = "Spatial Control",
            value = spatialControlPercent,
            range = 0f..150f,
            unit = "%",
            onValueChange = onSpatialControlChange
        )

        Row(
            Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column {
                Text("Voice Protection", style = MaterialTheme.typography.labelMedium, color = TextSecondary)
                Text(
                    "Real — VoiceProtectionController.enabled",
                    style = MaterialTheme.typography.labelSmall, color = TextMuted
                )
            }
            ToggleSwitch(checked = voiceProtectionEnabled, onCheckedChange = onVoiceProtectionChange, accent = PhosphorGreen)
        }
    }
}

@Composable
private fun PendingBackendNote() {
    Text(
        "⚠ UI preparada — sin backend real todavía (TODO)",
        style = MaterialTheme.typography.labelSmall,
        color = AmberSignal.copy(alpha = 0.85f)
    )
}

@Composable
private fun ModeChip(label: String, selected: Boolean, modifier: Modifier = Modifier, onClick: () -> Unit) {
    Box(
        modifier = modifier
            .clip(RoundedCornerShape(10.dp))
            .background(if (selected) AuroraCyan.copy(alpha = 0.18f) else ObsidianEdge.copy(alpha = 0.35f))
            .border(
                1.dp,
                if (selected) AuroraCyan.copy(alpha = 0.7f) else ObsidianEdge,
                RoundedCornerShape(10.dp)
            )
            .clickable(onClick = onClick)
            .padding(vertical = 8.dp),
        contentAlignment = Alignment.Center
    ) {
        Text(
            label,
            style = MaterialTheme.typography.labelSmall,
            color = if (selected) AuroraCyan else TextSecondary,
            fontWeight = if (selected) FontWeight.SemiBold else FontWeight.Normal
        )
    }
}
