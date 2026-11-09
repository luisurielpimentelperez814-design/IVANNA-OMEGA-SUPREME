package com.ivanna.omega.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.audio.IvannaLabMonitor
import com.ivanna.omega.ui.theme.*

// Paleta realineada con IvannaTheme Aurora Obsidiana v3.0 (nombres intactos):
// fondos gris plano → obsidiana azul; semáforo de alertas → tonos del tema
// (PhosphorGreen/AmberSignal/CoralWarn) para coherencia con el resto de la app.

// Umbrales de alerta pedidos en el brief: THD>1% = rojo, SNR<60dB = amarillo.
private fun thdColor(thd: Float) = when {
    thd < 0f -> TextMuted
    thd > 1f -> CoralWarn
    thd > 0.3f -> AmberSignal
    else -> PhosphorGreen
}
private fun snrColor(snr: Float) = when {
    snr < 0f && snr != -1f -> PhosphorGreen
    snr == -1f -> TextMuted
    snr < 60f -> AmberSignal
    else -> PhosphorGreen
}

@Composable
fun IvannaLabScreen(modifier: Modifier = Modifier) {
    val snapshot by IvannaLabMonitor.snapshot.collectAsState()
    val autoActive by IvannaLabMonitor.autoMeasureActive.collectAsState()

    Column(
        modifier = modifier.fillMaxSize().background(ObsidianVoid).padding(16.dp)
    ) {
        Text("IVANNA LAB", color = AuroraCyan, fontSize = 20.sp, fontWeight = FontWeight.Bold, letterSpacing = 1.5.sp)
        Text("THD · IMD · LUFS · SNR · Peak · True Peak", color = TextMuted, fontSize = 11.sp)
        Spacer(Modifier.height(16.dp))

        Row(
            Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            Button(
                onClick = { IvannaLabMonitor.measureNow() },
                colors = ButtonDefaults.buttonColors(containerColor = AuroraCyan, contentColor = ObsidianDeep),
                shape = RoundedCornerShape(10.dp),
                modifier = Modifier.weight(1f)
            ) { Text("MEDIR AHORA", fontWeight = FontWeight.Bold, fontSize = 12.sp) }

            OutlinedButton(
                onClick = { IvannaLabMonitor.resetAndStart() },
                shape = RoundedCornerShape(10.dp)
            ) { Text("RESET", fontSize = 12.sp) }
        }

        Spacer(Modifier.height(10.dp))
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Text("Auto-medición cada 30s", color = TextPrimary, fontSize = 13.sp, modifier = Modifier.weight(1f))
            Switch(
                checked = autoActive,
                onCheckedChange = { on ->
                    if (on) IvannaLabMonitor.startAutoMeasure() else IvannaLabMonitor.stopAutoMeasure()
                }
            )
        }

        Spacer(Modifier.height(20.dp))

        if (snapshot == null) {
            Text("Sin mediciones todavía — pulsa MEDIR AHORA o activa auto-medición.",
                color = TextMuted, fontSize = 12.sp)
        } else {
            val s = snapshot!!
            LabMetricRow("THD", s.thd, "%", thdColor(s.thd))
            LabMetricRow("IMD", s.imd, "%", TextPrimary)
            LabMetricRow("LUFS Integrado", s.lufs, "LUFS", TextPrimary)
            LabMetricRow("Rango Dinámico (LU)", s.luRange, "LU", TextPrimary)
            LabMetricRow("SNR", s.snr, "dB", snrColor(s.snr))
            LabMetricRow("Peak", s.peak, "dBFS", TextPrimary)
            LabMetricRow("True Peak", s.truepeak, "dBTP", TextPrimary)

            Spacer(Modifier.height(16.dp))
            if (s.report.isNotBlank()) {
                Column(
                    Modifier.fillMaxWidth().border(1.dp, ObsidianEdge, RoundedCornerShape(10.dp))
                        .background(ObsidianSoft, RoundedCornerShape(10.dp)).padding(12.dp)
                ) {
                    Text("REPORTE", color = AuroraCyan, fontSize = 10.sp, fontWeight = FontWeight.Bold, letterSpacing = 1.sp)
                    Spacer(Modifier.height(6.dp))
                    Text(s.report, color = TextSecondary, fontSize = 11.sp, lineHeight = 15.sp)
                }
            }
        }
    }
}

@Composable
private fun LabMetricRow(label: String, value: Float, unit: String, color: Color) {
    Row(
        Modifier.fillMaxWidth().padding(vertical = 4.dp),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(label, color = TextSecondary, fontSize = 13.sp)
        Text(
            if (value == -1f) "—" else "%.2f %s".format(value, unit),
            color = color, fontSize = 14.sp, fontWeight = FontWeight.Bold
        )
    }
}
