package com.ivanna.omega.adaptive

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.dsp.DSPBridge

// ── Palette (Adaptive theme — Magenta accent) ────────────────────────────────
private val Carbon = Color(0xFF0A0A0A)
private val Surface1 = Color(0xFF111111)
private val Surface2 = Color(0xFF181818)
private val Border1 = Color(0xFF222222)
private val CyanGlow = Color(0xFF00F5FF)
private val CyanDim = Color(0x3300F5FF)
private val MagentaGlow = Color(0xFFFF00FF)
private val MagentaDim = Color(0x33FF00FF)
private val GoldGlow = Color(0xFFFFD700)
private val GreenGlow = Color(0xFF00FF88)
private val RedGlow = Color(0xFFFF4444)
private val TextPri = Color(0xFFFFFFFF)
private val TextSec = Color(0xFF888888)

@Composable
fun AdaptiveEngineScreen(
    viewModel: AdaptiveViewModel,
    onBack: () -> Unit
) {
    val telemetry by remember { derivedStateOf { viewModel.telemetry } }
    val isRunning by remember { derivedStateOf { viewModel.isRunning } }
    val isVoiceProtection by remember { derivedStateOf { viewModel.isVoiceProtectionEnabled } }
    val isManualMode by remember { derivedStateOf { viewModel.isManualMode } }
    val confidence by remember { derivedStateOf { viewModel.analysisConfidence } }

    // Conectar callback DSP al iniciar
    LaunchedEffect(Unit) {
        viewModel.setDspCallback { snapshot ->
            DSPBridge.setAdaptiveParams(AdaptiveTelemetrySnapshot.toArray(snapshot))
        }
    }

    Column(
        Modifier
            .fillMaxSize()
            .background(Carbon)
            .windowInsetsPadding(WindowInsets.systemBars)
            .verticalScroll(rememberScrollState())
    ) {
        // ── Header ──
        Row(
            Modifier
                .fillMaxWidth()
                .background(Surface2)
                .padding(horizontal = 16.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Column {
                Text(
                    "MODO ADAPTATIVO",
                    color = MagentaGlow,
                    fontSize = 14.sp,
                    fontWeight = FontWeight.ExtraBold,
                    letterSpacing = 1.5.sp
                )
                Text(
                    "Magistral · Análisis + Control @ 10Hz",
                    color = TextSec,
                    fontSize = 9.sp,
                    letterSpacing = 1.sp
                )
            }
            OutlinedButton(
                onClick = onBack,
                border = BorderStroke(1.dp, Border1),
                shape = RoundedCornerShape(8.dp),
                modifier = Modifier.height(36.dp)
            ) {
                Text("← VOLVER", color = TextSec, fontSize = 10.sp, fontWeight = FontWeight.Bold)
            }
        }

        Spacer(Modifier.height(12.dp))

        // ── Status Cards ──
        Row(
            Modifier
                .fillMaxWidth()
                .padding(horizontal = 12.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            StatusCard(
                title = "MOTOR",
                value = if (isRunning) "ACTIVO" else "DETENIDO",
                color = if (isRunning) GreenGlow else RedGlow,
                modifier = Modifier.weight(1f)
            )
            StatusCard(
                title = "BPM",
                value = "${telemetry.bpm.toInt()}",
                color = CyanGlow,
                modifier = Modifier.weight(1f)
            )
            StatusCard(
                title = "CONFIANZA",
                value = "${(confidence * 100).toInt()}%",
                color = GoldGlow,
                modifier = Modifier.weight(1f)
            )
        }

        Spacer(Modifier.height(12.dp))

        // ── Control Panel ──
        Column(
            Modifier
                .fillMaxWidth()
                .padding(horizontal = 12.dp)
                .border(1.dp, Border1, RoundedCornerShape(12.dp))
                .background(Surface1, RoundedCornerShape(12.dp))
                .padding(14.dp)
        ) {
            Text(
                "CONTROLES",
                color = MagentaGlow,
                fontSize = 11.sp,
                fontWeight = FontWeight.Bold,
                letterSpacing = 1.5.sp
            )
            Spacer(Modifier.height(12.dp))

            // Start/Stop
            Button(
                onClick = { if (isRunning) viewModel.stop() else viewModel.start() },
                modifier = Modifier.fillMaxWidth().height(48.dp),
                colors = ButtonDefaults.buttonColors(
                    containerColor = if (isRunning) RedGlow else GreenGlow
                ),
                shape = RoundedCornerShape(10.dp)
            ) {
                Text(
                    if (isRunning) "⏹ DETENER MOTOR" else "▶ INICIAR MOTOR",
                    color = Carbon,
                    fontWeight = FontWeight.ExtraBold,
                    fontSize = 13.sp,
                    letterSpacing = 1.sp
                )
            }

            Spacer(Modifier.height(10.dp))

            // Voice Protection Toggle
            ToggleRow(
                label = "Voice Protection",
                description = "Protege frecuencias vocales del procesamiento",
                checked = isVoiceProtection,
                onCheckedChange = { viewModel.toggleVoiceProtection() },
                activeColor = CyanGlow
            )

            Spacer(Modifier.height(8.dp))

            // Manual Mode Toggle
            ToggleRow(
                label = "Modo Manual",
                description = "Control manual de parámetros (desactiva auto)",
                checked = isManualMode,
                onCheckedChange = { viewModel.toggleManualMode() },
                activeColor = GoldGlow
            )
        }

        Spacer(Modifier.height(12.dp))

        // ── Telemetry Display ──
        Column(
            Modifier
                .fillMaxWidth()
                .padding(horizontal = 12.dp)
                .border(1.dp, Border1, RoundedCornerShape(12.dp))
                .background(Surface1, RoundedCornerShape(12.dp))
                .padding(14.dp)
        ) {
            Text(
                "TELEMETRÍA EN TIEMPO REAL",
                color = CyanGlow,
                fontSize = 11.sp,
                fontWeight = FontWeight.Bold,
                letterSpacing = 1.5.sp
            )
            Spacer(Modifier.height(10.dp))

            TelemetryRow("Clase Espectral", when (telemetry.spectralClass) {
                0 -> "Bajos dominantes"
                1 -> "Medios dominantes"
                2 -> "Altos dominantes"
                3 -> "Espectral plano"
                else -> "Desconocido"
    })
            TelemetryRow("Fitness Genoma", "%.3f".format(telemetry.genomeFitness))
            TelemetryRow("Modo DSP", when (telemetry.mode) {
                0 -> "DSP"
                1 -> "DSP + Adaptativo"
                2 -> "Voice Protection"
                else -> "Full"
            })

            Spacer(Modifier.height(8.dp))

            // Parámetros actuales
            Text(
                "PARÁMETROS ACTIVOS",
                color = TextSec,
                fontSize = 9.sp,
                fontWeight = FontWeight.Bold,
                letterSpacing = 1.sp
            )
            Spacer(Modifier.height(6.dp))

            ParamBar("DRIVE", telemetry.drive, MagentaGlow)
            ParamBar("WET", telemetry.wet, CyanGlow)
            ParamBar("MIX", telemetry.mix, GoldGlow)
            ParamBar("ALPHA", telemetry.alpha, CyanGlow)
            ParamBar("BETA", telemetry.beta, CyanGlow)
            ParamBar("GAMMA", telemetry.gamma, CyanGlow)
            ParamBar("LOW", (telemetry.low + 12f) / 24f, MagentaGlow)
            ParamBar("MID", (telemetry.mid + 12f) / 24f, MagentaGlow)
            ParamBar("HIGH", (telemetry.high + 12f) / 24f, MagentaGlow)
        }

        Spacer(Modifier.height(12.dp))

        // ── Manual Override (solo visible en modo manual) ──
        if (isManualMode) {
            Column(
                Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 12.dp)
                    .border(2.dp, GoldGlow, RoundedCornerShape(12.dp))
                    .background(MagentaDim, RoundedCornerShape(12.dp))
                    .padding(14.dp)
            ) {
                Text(
                    "OVERRIDE MANUAL",
                    color = GoldGlow,
                    fontSize = 11.sp,
                    fontWeight = FontWeight.Bold,
                    letterSpacing = 1.5.sp
                )
                Spacer(Modifier.height(8.dp))
                Text(
                    "Los parámetros se controlan manualmente desde el Dashboard.",
                    color = TextSec,
                    fontSize = 10.sp,
                    textAlign = TextAlign.Center
                )
            }
            Spacer(Modifier.height(12.dp))
        }
    }
}

// ── Components ────────────────────────────────────────────────────────────────

@Composable
private fun StatusCard(title: String, value: String, color: Color, modifier: Modifier = Modifier) {
    Column(
        modifier = modifier
            .border(1.dp, Border1, RoundedCornerShape(10.dp))
            .background(Surface2, RoundedCornerShape(10.dp))
            .padding(10.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Text(title, color = TextSec, fontSize = 8.sp, letterSpacing = 1.sp)
        Spacer(Modifier.height(4.dp))
        Text(value, color = color, fontSize = 14.sp, fontWeight = FontWeight.ExtraBold)
    }
}

@Composable
private fun ToggleRow(
    label: String,
    description: String,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit,
    activeColor: Color
) {
    Row(
        Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.SpaceBetween
    ) {
        Column(Modifier.weight(1f)) {
            Text(label, color = TextPri, fontSize = 11.sp, fontWeight = FontWeight.Bold)
            Text(description, color = TextSec, fontSize = 8.sp, lineHeight = 10.sp)
        }
        Switch(
            checked = checked,
            onCheckedChange = onCheckedChange,
            colors = SwitchDefaults.colors(
                checkedThumbColor = activeColor,
                checkedTrackColor = activeColor.copy(alpha = 0.3f),
                uncheckedThumbColor = Color(0xFF444444),
                uncheckedTrackColor = Color(0xFF222222)
            )
        )
    }
}

@Composable
private fun TelemetryRow(label: String, value: String) {
    Row(
        Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(label, color = TextSec, fontSize = 10.sp)
        Text(value, color = CyanGlow, fontSize = 10.sp, fontWeight = FontWeight.Bold)
    }
    Spacer(Modifier.height(4.dp))
}

@Composable
private fun ParamBar(name: String, value: Float, color: Color) {
    val clamped = value.coerceIn(0f, 1f)
    Column(Modifier.fillMaxWidth().padding(vertical = 2.dp)) {
        Row(
            Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Text(name, color = TextSec, fontSize = 8.sp)
            Text("%.2f".format(value), color = color, fontSize = 8.sp, fontWeight = FontWeight.Bold)
        }
        Spacer(Modifier.height(2.dp))
        Box(
            Modifier
                .fillMaxWidth()
                .height(4.dp)
                .clip(RoundedCornerShape(2.dp))
                .background(Color(0xFF222222))
        ) {
            Box(
                Modifier
                    .fillMaxWidth(clamped)
                    .height(4.dp)
                    .clip(RoundedCornerShape(2.dp))
                    .background(color)
            )
        }
    }
}
