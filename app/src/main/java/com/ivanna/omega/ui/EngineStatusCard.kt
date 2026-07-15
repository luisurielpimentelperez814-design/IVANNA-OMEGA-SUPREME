package com.ivanna.omega.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.ivanna.omega.audio.OmegaMetrics
import com.ivanna.omega.ui.theme.AmberSignal
import com.ivanna.omega.ui.theme.AuroraCyan
import com.ivanna.omega.ui.theme.GlassHighlight
import com.ivanna.omega.ui.theme.NeonMagenta
import com.ivanna.omega.ui.theme.ObsidianEdge
import com.ivanna.omega.ui.theme.ObsidianGlass
import com.ivanna.omega.ui.theme.ObsidianSoft
import com.ivanna.omega.ui.theme.PhosphorGreen
import com.ivanna.omega.ui.theme.TextPrimary
import com.ivanna.omega.ui.theme.TextSecondary

@Composable
fun EngineStatusCard(
    metrics: OmegaMetrics,
    modifier: Modifier = Modifier
) {
    Column(
        modifier = modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(18.dp))
            .background(Brush.verticalGradient(listOf(ObsidianGlass, ObsidianSoft.copy(alpha = 0.9f))))
            .border(1.dp, AuroraCyan.copy(alpha = 0.35f), RoundedCornerShape(18.dp))
            .border(1.dp, GlassHighlight, RoundedCornerShape(topStart = 18.dp, topEnd = 18.dp))
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(10.dp)
    ) {
        Text(
            "IVANNA OMEGA ENGINE",
            style = MaterialTheme.typography.titleMedium,
            color = TextPrimary,
            fontWeight = FontWeight.Bold
        )
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            StatusLine("DSP", if (metrics.dspActive) "ACTIVE" else "STANDBY", if (metrics.dspActive) PhosphorGreen else AmberSignal)
            Text("${metrics.sampleRate / 1000}kHz", style = MaterialTheme.typography.labelLarge, color = AuroraCyan)
        }
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            Text("Latency: %.1fms".format(metrics.latencyMs), style = MaterialTheme.typography.labelMedium, color = TextSecondary)
            Text("CPU: %.0f%%".format(metrics.cpuPercent), style = MaterialTheme.typography.labelMedium, color = AmberSignal)
        }
        DividerGlow()
        Text("AI ANALYSIS", style = MaterialTheme.typography.labelMedium, color = NeonMagenta, fontWeight = FontWeight.Bold)
        Text(
            "Music · Bass +12% · Voice protection",
            style = MaterialTheme.typography.bodySmall,
            color = TextSecondary
        )
        Text(
            "Category: ${metrics.yamnetCategory}  %.0f%%".format(metrics.yamnetConfidence * 100f),
            style = MaterialTheme.typography.labelMedium,
            color = TextPrimary
        )
        DividerGlow()
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            Column {
                Text("SPATIAL", style = MaterialTheme.typography.labelMedium, color = AuroraCyan, fontWeight = FontWeight.Bold)
                StatusLine("HRTF", if (metrics.hrtfActive) "ON" else "OFF", if (metrics.hrtfActive) PhosphorGreen else AmberSignal)
            }
            Text("Width: %.0f%%".format(metrics.spatialWidth * 100f), style = MaterialTheme.typography.labelLarge, color = AuroraCyan)
        }
    }
}

@Composable
private fun StatusLine(label: String, value: String, accent: Color) {
    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        Text("$label:", style = MaterialTheme.typography.labelMedium, color = TextSecondary)
        Box(Modifier.size(8.dp).clip(CircleShape).background(accent))
        Text(value, style = MaterialTheme.typography.labelLarge, color = accent, fontWeight = FontWeight.Bold)
    }
}

@Composable
private fun DividerGlow() {
    Spacer(
        Modifier
            .fillMaxWidth()
            .height(1.dp)
            .background(Brush.horizontalGradient(listOf(Color.Transparent, ObsidianEdge, Color.Transparent)))
    )
}
