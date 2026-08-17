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

private val LabBg = Color(0xFF0A0A0A)
private val LabCard = Color(0xFF141414)
private val LabBorder = Color(0xFF222222)
private val LabCyan = Color(0xFF00F5FF)
private val LabGreen = Color(0xFF00E676)
private val LabYellow = Color(0xFFFFD600)
private val LabRed = Color(0xFFFF3B30)

// Umbrales de alerta pedidos en el brief: THD>1% = rojo, SNR<60dB = amarillo.
private fun thdColor(thd: Float) = when {
    thd < 0f -> Color(0xFF555555)
    thd > 1f -> LabRed
    thd > 0.3f -> LabYellow
    else -> LabGreen
}
private fun snrColor(snr: Float) = when {
    snr < 0f && snr != -1f -> LabGreen
    snr == -1f -> Color(0xFF555555)
    snr < 60f -> LabYellow
    else -> LabGreen
}

@Composable
fun IvannaLabScreen(modifier: Modifier = Modifier) {
    val snapshot by IvannaLabMonitor.snapshot.collectAsState()
    val autoActive by IvannaLabMonitor.autoMeasureActive.collectAsState()

    Column(
        modifier = modifier.fillMaxSize().background(LabBg).padding(16.dp)
    ) {
        Text("IVANNA LAB", color = LabCyan, fontSize = 20.sp, fontWeight = FontWeight.Bold, letterSpacing = 1.5.sp)
        Text("THD · IMD · LUFS · SNR · Peak · True Peak", color = Color(0xFF888888), fontSize = 11.sp)
        Spacer(Modifier.height(16.dp))

        Row(
            Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            Button(
                onClick = { IvannaLabMonitor.measureNow() },
                colors = ButtonDefaults.buttonColors(containerColor = LabCyan, contentColor = Color.Black),
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
            Text("Auto-medición cada 30s", color = Color.White, fontSize = 13.sp, modifier = Modifier.weight(1f))
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
                color = Color(0xFF666666), fontSize = 12.sp)
        } else {
            val s = snapshot!!
            LabMetricRow("THD", s.thd, "%", thdColor(s.thd))
            LabMetricRow("IMD", s.imd, "%", Color.White)
            LabMetricRow("LUFS Integrado", s.lufs, "LUFS", Color.White)
            LabMetricRow("Rango Dinámico (LU)", s.luRange, "LU", Color.White)
            LabMetricRow("SNR", s.snr, "dB", snrColor(s.snr))
            LabMetricRow("Peak", s.peak, "dBFS", Color.White)
            LabMetricRow("True Peak", s.truepeak, "dBTP", Color.White)

            Spacer(Modifier.height(16.dp))
            if (s.report.isNotBlank()) {
                Column(
                    Modifier.fillMaxWidth().border(1.dp, LabBorder, RoundedCornerShape(10.dp))
                        .background(LabCard, RoundedCornerShape(10.dp)).padding(12.dp)
                ) {
                    Text("REPORTE", color = LabCyan, fontSize = 10.sp, fontWeight = FontWeight.Bold, letterSpacing = 1.sp)
                    Spacer(Modifier.height(6.dp))
                    Text(s.report, color = Color(0xFFCCCCCC), fontSize = 11.sp, lineHeight = 15.sp)
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
        Text(label, color = Color(0xFFAAAAAA), fontSize = 13.sp)
        Text(
            if (value == -1f) "—" else "%.2f %s".format(value, unit),
            color = color, fontSize = 14.sp, fontWeight = FontWeight.Bold
        )
    }
}
