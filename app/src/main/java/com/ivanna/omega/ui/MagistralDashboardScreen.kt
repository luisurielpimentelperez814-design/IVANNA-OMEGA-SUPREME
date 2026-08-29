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
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.ivanna.omega.ui.theme.*

/**
 * MagistralDashboardScreen — tab BRAIN.
 * 
 * bandData: FloatArray[N] energía lineal [0..1] por banda.
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
            .background(Brush.verticalGradient(listOf(ObsidianVoid, ObsidianSoft)))
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
                color  = if (isDaemonActive) PhosphorGreen.copy(0.15f) else CoralWarn.copy(0.15f),
                shape  = RoundedCornerShape(12.dp),
                border = BorderStroke(1.dp, if (isDaemonActive) PhosphorGreen else CoralWarn)
            ) {
                Text(
                    if (isDaemonActive) "RT ${latencyMs.toInt()} ms" else "DAEMON OFF",
                    color  = if (isDaemonActive) PhosphorGreen else CoralWarn,
                    style  = MaterialTheme.typography.labelSmall,
                    fontWeight = FontWeight.Bold,
                    modifier   = Modifier.padding(horizontal = 12.dp, vertical = 6.dp)
                )
            }
        }
        Spacer(Modifier.height(16.dp))

        // ── Adaptive Engine en vivo ──
        AdaptiveEngineLivePanel()
        Spacer(modifier = Modifier.height(16.dp))

        // ── Bark Spectrum Magistral ──────────────
        Text("BARK SPECTRUM", color = TextSecondary, style = MaterialTheme.typography.labelMedium)
        Spacer(Modifier.height(8.dp))
        Canvas(
            modifier = Modifier
                .fillMaxWidth()
                .height(110.dp)
                .background(ObsidianGlass, RoundedCornerShape(12.dp))
                .padding(8.dp)
        ) {
            val count    = (bandData?.size ?: 32).coerceIn(8, 64)
            val barWidth = size.width / count
            for (i in 0 until count) {
                val energy = if (bandData != null && i < bandData.size)
                    bandData[i].coerceIn(0f, 1f)
                else 0.02f
                val barH = (size.height * energy).coerceAtLeast(4f)
                
                val brush = Brush.verticalGradient(
                    colors = listOf(AuroraCyan, AuroraCyan.copy(alpha = 0.2f)),
                    startY = size.height - barH,
                    endY = size.height
                )
                
                drawRoundRect(
                    brush     = brush,
                    topLeft   = Offset(i * barWidth + 2f, size.height - barH),
                    size      = Size(barWidth - 4f, barH),
                    cornerRadius = CornerRadius(barWidth / 2f, barWidth / 2f)
                )
            }
        }

        Spacer(Modifier.height(16.dp))

        // ── Spatial Scene Magistral ─────────────────────────────────────────────
        Text("ESCENA ESPACIAL 3D", color = TextSecondary, style = MaterialTheme.typography.labelMedium)
        Spacer(Modifier.height(8.dp))
        Canvas(
            modifier = Modifier
                .fillMaxWidth()
                .height(110.dp)
                .background(ObsidianGlass, RoundedCornerShape(12.dp))
        ) {
            val cx = size.width / 2f
            val cy = size.height / 2f
            
            // Grid and Depth
            for(i in 1..3) {
                drawCircle(
                    color = TextMuted.copy(alpha = 0.2f),
                    radius = i * 35f,
                    center = Offset(cx, cy),
                    style = Stroke(width = 1f)
                )
            }

            drawCircle(color = TextPrimary.copy(0.9f),    radius = 6f,  center = Offset(cx, cy))
            
            // Orbits with glows
            drawCircle(color = PhosphorGreenGlow, radius = 20f, center = Offset(cx - 80f, cy - 18f))
            drawCircle(color = PhosphorGreen, radius = 8f, center = Offset(cx - 80f, cy - 18f))
            
            drawCircle(color = NeonMagentaGlow,   radius = 18f,  center = Offset(cx + 85f, cy - 25f))
            drawCircle(color = NeonMagenta,   radius = 7f,  center = Offset(cx + 85f, cy - 25f))
            
            drawCircle(color = AmberSignalGlow,   radius = 22f, center = Offset(cx, cy - 38f))
            drawCircle(color = AmberSignal,   radius = 9f, center = Offset(cx, cy - 38f))
        }

        Spacer(Modifier.height(24.dp))

        // ── Perceptual Brain CTA ───────────────────────────────────────
        Button(
            onClick  = onOpenPerceptualBrain,
            modifier = Modifier.fillMaxWidth().height(52.dp),
            colors   = ButtonDefaults.buttonColors(containerColor = NeonMagenta.copy(0.15f), contentColor = NeonMagenta),
            shape    = RoundedCornerShape(14.dp),
            border   = BorderStroke(1.dp, NeonMagenta.copy(0.5f))
        ) {
            Text(
                "PERCEPTUAL BRAIN CORTEX",
                style      = MaterialTheme.typography.titleSmall,
                fontWeight = FontWeight.Bold
            )
        }

        Spacer(Modifier.weight(1f))

        // ── Actions ───────────────────────────────────────────────────
        Row(
            modifier = Modifier.fillMaxWidth().padding(bottom = 8.dp),
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            OutlinedButton(
                onClick  = onCalibrateHrtf,
                modifier = Modifier.weight(1f).height(48.dp),
                shape    = RoundedCornerShape(12.dp),
                colors   = ButtonDefaults.outlinedButtonColors(contentColor = AuroraCyan),
                border   = BorderStroke(1.dp, AuroraCyan.copy(0.45f))
            ) { Text("CALIBRAR HRTF", style = MaterialTheme.typography.labelSmall) }

            OutlinedButton(
                onClick  = onResetToNeutral,
                modifier = Modifier.weight(1f).height(48.dp),
                shape    = RoundedCornerShape(12.dp),
                colors   = ButtonDefaults.outlinedButtonColors(contentColor = AmberSignal),
                border   = BorderStroke(1.dp, AmberSignal.copy(0.45f))
            ) { Text("NEUTRAL", style = MaterialTheme.typography.labelSmall) }
        }
    }
}
