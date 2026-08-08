package com.ivanna.omega.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.ui.theme.*

/**
 * SystemScreen — Sección SISTEMA unificada.
 * Reemplaza: MagiskStatusPanel + ProfileSelectorScreen + TelemetryDashboard
 * en una sola pantalla con 3 tabs.
 */
@Composable
fun SystemScreen(
    modifier: Modifier = Modifier,
    onOpenMagisk: () -> Unit = {},
    onOpenProfiles: () -> Unit = {}
) {
    var selectedTab by remember { mutableIntStateOf(0) }
    val tabs = listOf("MAGISK", "PERFILES", "TELEMETRÍA")

    Column(modifier = modifier.background(ObsidianDeep)) {
        Text(
            "SISTEMA",
            color = PhosphorGreen,
            fontSize = 11.sp,
            fontWeight = FontWeight.ExtraBold,
            letterSpacing = 3.sp,
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 12.dp)
        )
        ScrollableTabRow(
            selectedTabIndex = selectedTab,
            containerColor = Color.Transparent,
            contentColor = PhosphorGreen,
            edgePadding = 16.dp
        ) {
            tabs.forEachIndexed { i, title ->
                Tab(
                    selected = selectedTab == i,
                    onClick  = { selectedTab = i },
                    text = {
                        Text(
                            title, fontSize = 11.sp,
                            fontWeight = if (selectedTab == i) FontWeight.Bold else FontWeight.Normal,
                            color = if (selectedTab == i) PhosphorGreen else TextMuted
                        )
                    }
                )
            }
        }
        HorizontalDivider(color = ObsidianEdge, thickness = 0.5.dp)
        Column(
            modifier = Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 16.dp, vertical = 12.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            when (selectedTab) {
                0 -> MagiskTab(onOpenMagisk)
                1 -> ProfilesTab(onOpenProfiles)
                2 -> TelemetryTab()
            }
        }
    }
}

@Composable
private fun MagiskTab(onOpenMagisk: () -> Unit) {
    GlassCard("MÓDULO MAGISK", PhosphorGreen, "ivanna_omega · libomega_effect.so · daemon") {
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            val loaded by remember { derivedStateOf { IvannaNativeLib.isLoaded } }
            Row(verticalAlignment = Alignment.CenterVertically) {
                Box(
                    Modifier.size(8.dp)
                        .background(if (loaded) PhosphorGreen else Color.Red, androidx.compose.foundation.shape.CircleShape)
                )
                Spacer(Modifier.width(8.dp))
                Text(
                    if (loaded) "Módulo cargado · JNI activo" else "Módulo no detectado",
                    color = if (loaded) PhosphorGreen else Color.Red,
                    fontSize = 12.sp
                )
            }
            listOf(
                "libivanna_omega.so" to loaded,
                "libomega_effect.so" to loaded,
                "ivanna_daemon"      to loaded
            ).forEach { (name, ok) ->
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Text(name, color = TextSecondary, fontSize = 11.sp, fontFamily = FontFamily.Monospace)
                    Text(if (ok) "OK" else "MISSING", color = if (ok) PhosphorGreen else Color.Red, fontSize = 11.sp)
                }
            }
            Spacer(Modifier.height(4.dp))
            OutlinedButton(
                onClick = onOpenMagisk,
                modifier = Modifier.fillMaxWidth(),
                colors = ButtonDefaults.outlinedButtonColors(contentColor = PhosphorGreen)
            ) { Text("VER PANEL MAGISK COMPLETO", fontSize = 11.sp) }
        }
    }
}

@Composable
private fun ProfilesTab(onOpenProfiles: () -> Unit) {
    GlassCard("PERFILES DE AUDIO", AuroraCyan, "Presets · Calibración · Historial") {
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            listOf("Cálido · Graves realzados, agudos suaves",
                   "Natural · Curva plana, respuesta neutra",
                   "Espacial · HRTF + widener máximo",
                   "Vocal · Presencia 2.5kHz, menos graves",
                   "Extremo · Todo al máximo · Material denso"
            ).forEachIndexed { i, desc ->
                val name = listOf("CÁLIDO","NATURAL","ESPACIAL","VOCAL","EXTREMO")[i]
                OutlinedButton(
                    onClick = onOpenProfiles,
                    modifier = Modifier.fillMaxWidth(),
                    colors = ButtonDefaults.outlinedButtonColors(contentColor = AuroraCyan)
                ) {
                    Column {
                        Text(name, fontSize = 12.sp, fontWeight = FontWeight.Bold)
                        Text(desc, fontSize = 10.sp, color = TextMuted)
                    }
                }
            }
        }
    }
}

@Composable
private fun TelemetryTab() {
    var telemetry by remember { mutableStateOf<FloatArray?>(null) }
    LaunchedEffect(Unit) {
        while (true) {
            if (IvannaNativeLib.isLoaded)
                telemetry = runCatching { IvannaNativeLib.nativeGetAdaptiveTelemetry() }.getOrNull()
            kotlinx.coroutines.delay(100)
        }
    }
    GlassCard("TELEMETRÍA EN VIVO", PhosphorGreen, "10Hz · Motor A · Pipeline completo") {
        val labels = listOf("RMS","Peak dB","GR dB","CPU %","Target","CompAmt","ExcRed","SpatW","VoiceProt","Running")
        val colors = listOf(AuroraCyan, Color.Red, NeonMagenta, PhosphorGreen,
            AuroraCyan, NeonMagenta, AuroraCyan, PhosphorGreen, NeonMagenta, AuroraCyan)
        Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
            telemetry?.forEachIndexed { i, v ->
                if (i < labels.size) {
                    val c = colors[i]
                    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                        Text(labels[i], color = TextSecondary, fontSize = 10.sp)
                        Text("%.4f".format(v), color = c, fontSize = 10.sp, fontFamily = FontFamily.Monospace)
                    }
                    LinearProgressIndicator(
                        progress = { v.coerceIn(0f, 1f) },
                        modifier = Modifier.fillMaxWidth().height(2.dp),
                        color = c, trackColor = ObsidianEdge
                    )
                }
            } ?: Text("Cargando...", color = TextMuted, fontSize = 11.sp)
        }
    }
}
