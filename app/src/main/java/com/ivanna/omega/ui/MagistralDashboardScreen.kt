package com.ivanna.omega.ui

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.ivanna.omega.ui.theme.*

/**
 * MagistralDashboardScreen — tab BRAIN.
 *
 * bandData: FloatArray[N] energía lineal [0..1] por banda (del VisualizerBridge).
 *           Si null → muestra baseline plano; sin animación fake de sin().
 * onOpenPerceptualBrain → push a "perceptual_brain" desde el BRAIN tab.
 */
@Composable
fun MagistralDashboardScreen(
    latencyMs             : Float         = 0f,
    isDaemonActive        : Boolean       = false,
    onResetToNeutral      : () -> Unit    = {},
    onCalibrateHrtf       : () -> Unit    = {},
    onOpenPerceptualBrain : () -> Unit    = {},
    bandData              : FloatArray?   = null,
    modifier              : Modifier      = Modifier
) {
    Column(
        modifier = modifier
            .fillMaxSize()
            .background(ObsidianVoid)
            .padding(16.dp)
    ) {
        // ── Header ───────────────────────────────────────────────────────
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column {
                Text(
                    "IVANNA OMEGA SUPREME",
                    color = AuroraCyan,
                    style = MaterialTheme.typography.titleLarge,
                    fontWeight = FontWeight.Bold
                )
                Text(
                    "Cognitive Realtime Audio Engine",
                    color = TextMuted,
                    style = MaterialTheme.typography.labelSmall
                )
            }
            Surface(
                color  = if (isDaemonActive) PhosphorGreen.copy(0.10f) else CoralWarn.copy(0.10f),
                shape  = RoundedCornerShape(12.dp),
                border = BorderStroke(1.dp, if (isDaemonActive) PhosphorGreen else CoralWarn)
            ) {
                Text(
                    if (isDaemonActive) "RT ${latencyMs.toInt()} ms" else "DAEMON OFF",
                    color  = if (isDaemonActive) PhosphorGreen else CoralWarn,
                    style  = MaterialTheme.typography.labelSmall,
                    fontWeight = FontWeight.Bold,
                    modifier   = Modifier.padding(horizontal = 10.dp, vertical = 6.dp)
                )
            }
        }

        Spacer(Modifier.height(16.dp))

        // ── Adaptive Engine en vivo (JNI huérfanas cableadas) ──
        AdaptiveEngineLivePanel()
        Spacer(modifier = Modifier.height(12.dp))

        // ── Bark Spectrum — datos reales, sin sin() fake ──────────────
        Text("BARK SPECTRUM", color = TextSecondary, style = MaterialTheme.typography.labelMedium)
        Spacer(Modifier.height(6.dp))
        Canvas(
            modifier = Modifier
                .fillMaxWidth()
                .height(100.dp)
                .background(ObsidianSoft, RoundedCornerShape(8.dp))
        ) {
            val count    = (bandData?.size ?: 32).coerceIn(8, 64)
            val barWidth = size.width / count
            for (i in 0 until count) {
                val energy = if (bandData != null && i < bandData.size)
                    bandData[i].coerceIn(0f, 1f)
                else 0.04f                               // baseline plano, no fake
                val barH = size.height * energy
                drawRect(
                    color     = AuroraCyan.copy(alpha = 0.25f + energy * 0.75f),
                    topLeft   = Offset(i * barWidth + 1f, size.height - barH),
                    size      = Size(barWidth - 2f, barH)
                )
            }
        }

        Spacer(Modifier.height(16.dp))

        // ── Spatial Scene ─────────────────────────────────────────────
        Text("ESCENA ESPACIAL 3D", color = TextSecondary, style = MaterialTheme.typography.labelMedium)
        Spacer(Modifier.height(6.dp))
        Canvas(
            modifier = Modifier
                .fillMaxWidth()
                .height(90.dp)
                .background(ObsidianSoft, RoundedCornerShape(8.dp))
        ) {
            val cx = size.width / 2f
            val cy = size.height / 2f
            drawCircle(color = TextPrimary.copy(0.9f),    radius = 8f,  center = Offset(cx, cy))
            drawCircle(color = PhosphorGreen.copy(0.85f), radius = 10f, center = Offset(cx - 80f, cy - 18f))
            drawCircle(color = NeonMagenta.copy(0.85f),   radius = 9f,  center = Offset(cx + 85f, cy - 25f))
            drawCircle(color = AmberSignal.copy(0.85f),   radius = 11f, center = Offset(cx, cy - 38f))
        }

        Spacer(Modifier.height(16.dp))

        // ── Perceptual Brain CTA ───────────────────────────────────────
        OutlinedButton(
            onClick  = onOpenPerceptualBrain,
            modifier = Modifier.fillMaxWidth(),
            colors   = ButtonDefaults.outlinedButtonColors(contentColor = NeonMagenta),
            border   = BorderStroke(1.dp, NeonMagenta.copy(0.45f))
        ) {
            Text(
                "PERCEPTUAL BRAIN CORTEX  ↗",
                style      = MaterialTheme.typography.labelLarge,
                fontWeight = FontWeight.Bold
            )
        }

        Spacer(Modifier.weight(1f))

        // ── Actions ───────────────────────────────────────────────────
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            OutlinedButton(
                onClick  = onCalibrateHrtf,
                modifier = Modifier.weight(1f),
                colors   = ButtonDefaults.outlinedButtonColors(contentColor = AuroraCyan),
                border   = BorderStroke(1.dp, AuroraCyan.copy(0.45f))
            ) { Text("CALIBRAR HRTF", style = MaterialTheme.typography.labelSmall) }

            OutlinedButton(
                onClick  = onResetToNeutral,
                modifier = Modifier.weight(1f),
                colors   = ButtonDefaults.outlinedButtonColors(contentColor = AmberSignal),
                border   = BorderStroke(1.dp, AmberSignal.copy(0.45f))
            ) { Text("NEUTRAL", style = MaterialTheme.typography.labelSmall) }
        }
    }
}
