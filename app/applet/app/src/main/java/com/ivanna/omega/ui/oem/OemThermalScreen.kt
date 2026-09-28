package com.ivanna.omega.ui.oem

import androidx.compose.animation.core.*
import androidx.compose.foundation.*
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.ui.theme.*
import kotlin.math.*

@Composable
fun OemThermalScreen(state: OemState, onBack: () -> Unit) {
    val thermalColor = when (state.thermalLevel) {
        OemState.ThermalLevel.NORMAL   -> PhosphorGreen
        OemState.ThermalLevel.LIGHT    -> AmberSignal
        OemState.ThermalLevel.MODERATE -> Color(0xFFFF8C00)
        OemState.ThermalLevel.SEVERE   -> CoralWarn
    }

    Column(
        Modifier.fillMaxSize().background(ObsidianVoid)
            .windowInsetsPadding(WindowInsets.systemBars)
    ) {
        // Header
        Row(
            Modifier.fillMaxWidth().background(ObsidianSoft).padding(14.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            IconButton(onClick = onBack, modifier = Modifier.size(32.dp)) {
                Icon(Icons.Default.ArrowBack, null, tint = thermalColor)
            }
            Column(Modifier.weight(1f)) {
                Text("CONTROL TÉRMICO", color = thermalColor,
                    fontWeight = FontWeight.ExtraBold, fontSize = 12.sp, letterSpacing = 1.5.sp)
                Text("ThermalGovernor OEM · PowerManager API 29+",
                    color = TextMuted, fontSize = 9.sp)
            }
            if (state.thermalLevel != OemState.ThermalLevel.NORMAL)
                PulseDot(thermalColor)
        }

        Column(
            Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(14.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {

            // Thermal gauge
            OemCard(Modifier.fillMaxWidth()) {
                Row(
                    Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(16.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    // Arc gauge
                    Box(Modifier.size(100.dp), contentAlignment = Alignment.Center) {
                        androidx.compose.foundation.Canvas(Modifier.size(100.dp)) {
                            val sweep = 240f
                            val start = 150f
                            val load  = state.thermalLoad.coerceIn(0f, 1f)
                            drawArc(color = Color(0xFF1A2535), startAngle = start,
                                sweepAngle = sweep, useCenter = false,
                                style = Stroke(10f, cap = StrokeCap.Round))
                            if (load > 0f)
                                drawArc(color = thermalColor, startAngle = start,
                                    sweepAngle = sweep * load, useCenter = false,
                                    style = Stroke(10f, cap = StrokeCap.Round))
                        }
                        Column(horizontalAlignment = Alignment.CenterHorizontally) {
                            Text("${(state.thermalLoad * 100).toInt()}%",
                                color = thermalColor, fontSize = 18.sp,
                                fontWeight = FontWeight.ExtraBold,
                                fontFamily = FontFamily.Monospace)
                            Text("carga", color = TextMuted, fontSize = 8.sp)
                        }
                    }

                    Column(Modifier.weight(1f)) {
                        ThermalLevelBadge(state.thermalLevel, thermalColor)
                        Spacer(Modifier.height(8.dp))
                        TelRow("API térmica", if (state.thermalApiOk) "PowerManager v29+ ✓" else "No disponible", PhosphorGreen)
                        TelRow("Headroom", "${"%.1f".format((1f - state.thermalLoad) * 100)}% libre", thermalColor)
                        TelRow("Poll rate", "2s (hilo IO)", TextSecondary)
                    }
                }
            }

            // Reducción activa por subsistema
            OemCard(Modifier.fillMaxWidth()) {
                Text("REDUCCIÓN DSP ACTIVA", color = TextMuted, fontSize = 8.sp,
                    fontWeight = FontWeight.Bold, letterSpacing = 1.sp)
                Spacer(Modifier.height(8.dp))

                ThermalSubsystem(
                    name     = "EXCITADOR ARMÓNICO",
                    detail   = "Oversampling 2× + biquads OS — etapa más costosa",
                    reduction = state.exciterRed,
                    priority = 1,
                    color    = NeonMagenta
                )
                Spacer(Modifier.height(6.dp))
                ThermalSubsystem(
                    name     = "ANCHO ESPACIAL",
                    detail   = "ITD/ILD + alimenta el RIR convolucionador",
                    reduction = (1f - state.spatialWidth / 2f).coerceIn(0f, 1f),
                    priority = 2,
                    color    = AuroraCyan
                )
                Spacer(Modifier.height(6.dp))
                ThermalSubsystem(
                    name     = "COMPRESOR",
                    detail   = "Envolvente log por muestra — costo medio",
                    reduction = 1f - state.compAmount,
                    priority = 3,
                    color    = AmberSignal
                )
            }

            // Política térmica
            OemCard(Modifier.fillMaxWidth()) {
                Text("POLÍTICA DE DEGRADACIÓN", color = TextMuted, fontSize = 8.sp,
                    fontWeight = FontWeight.Bold, letterSpacing = 1.sp)
                Spacer(Modifier.height(8.dp))
                PolicyRow("< 30%",  "Normal — pipeline completo",           PhosphorGreen)
                PolicyRow("30–60%", "Excitador −40%",                       AmberSignal)
                PolicyRow("60–80%", "Excitador −70%, espacial −25%, comp −15%", Color(0xFFFF8C00))
                PolicyRow("> 80%",  "Excitador −100%, espacial −50%, comp −30%", CoralWarn)
                Spacer(Modifier.height(6.dp))
                Text(
                    "Regla OEM: mejor reducir calidad que producir un XRun. " +
                    "El volumen (target_gain) NUNCA se toca — el nivel percibido " +
                    "permanece estable aunque cambien los efectos.",
                    color = TextMuted, fontSize = 8.sp, lineHeight = 13.sp
                )
            }

            // Estado en tiempo real
            if (state.thermalLevel != OemState.ThermalLevel.NORMAL) {
                Surface(
                    shape = RoundedCornerShape(10.dp),
                    border = BorderStroke(1.dp, thermalColor.copy(alpha = 0.5f)),
                    color = thermalColor.copy(alpha = 0.08f),
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Row(Modifier.padding(14.dp),
                        horizontalArrangement = Arrangement.spacedBy(10.dp),
                        verticalAlignment = Alignment.CenterVertically) {
                        Icon(Icons.Default.Thermostat, null, tint = thermalColor,
                            modifier = Modifier.size(20.dp))
                        Column {
                            Text("Protección térmica activa", color = thermalColor,
                                fontWeight = FontWeight.Bold, fontSize = 10.sp)
                            Text(
                                when (state.thermalLevel) {
                                    OemState.ThermalLevel.LIGHT    ->
                                        "Reduciendo excitador armónico para mantener estabilidad térmica."
                                    OemState.ThermalLevel.MODERATE ->
                                        "Reduciendo excitador y ancho espacial. " +
                                        "El compresor opera al mínimo efectivo."
                                    OemState.ThermalLevel.SEVERE   ->
                                        "Protección agresiva: excitador silenciado, " +
                                        "espacialidad y compresor al mínimo. " +
                                        "Enfriar el dispositivo para restaurar el pipeline completo."
                                    else -> ""
                                },
                                color = TextSecondary, fontSize = 9.sp, lineHeight = 14.sp
                            )
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun ThermalLevelBadge(level: OemState.ThermalLevel, color: Color) {
    Surface(shape = RoundedCornerShape(6.dp), color = color.copy(alpha = 0.15f)) {
        Row(
            Modifier.padding(horizontal = 10.dp, vertical = 5.dp),
            horizontalArrangement = Arrangement.spacedBy(5.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Icon(
                when (level) {
                    OemState.ThermalLevel.NORMAL   -> Icons.Default.CheckCircle
                    OemState.ThermalLevel.LIGHT    -> Icons.Default.Warning
                    OemState.ThermalLevel.MODERATE -> Icons.Default.Warning
                    OemState.ThermalLevel.SEVERE   -> Icons.Default.Error
                },
                null, tint = color, modifier = Modifier.size(12.dp)
            )
            Text(
                when (level) {
                    OemState.ThermalLevel.NORMAL   -> "NORMAL"
                    OemState.ThermalLevel.LIGHT    -> "LEVE"
                    OemState.ThermalLevel.MODERATE -> "MODERADO"
                    OemState.ThermalLevel.SEVERE   -> "SEVERO"
                },
                color = color, fontSize = 9.sp, fontWeight = FontWeight.Bold
            )
        }
    }
}

@Composable
private fun ThermalSubsystem(name: String, detail: String, reduction: Float, priority: Int, color: Color) {
    Column {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically) {
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp),
                verticalAlignment = Alignment.CenterVertically) {
                Box(
                    Modifier.size(16.dp).clip(RoundedCornerShape(3.dp))
                        .background(color.copy(alpha = 0.2f)),
                    contentAlignment = Alignment.Center
                ) {
                    Text("$priority", color = color, fontSize = 7.sp, fontWeight = FontWeight.Bold)
                }
                Text(name, color = TextPrimary, fontSize = 9.sp, fontWeight = FontWeight.Bold)
            }
            Text(
                if (reduction < 0.01f) "COMPLETO"
                else "−${(reduction * 100).toInt()}%",
                color = if (reduction < 0.01f) color else AmberSignal,
                fontSize = 9.sp, fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold
            )
        }
        Text(detail, color = TextMuted, fontSize = 8.sp)
        Spacer(Modifier.height(4.dp))
        Box(Modifier.fillMaxWidth().height(3.dp).clip(RoundedCornerShape(2.dp))
            .background(ObsidianEdge)) {
            Box(
                Modifier
                    .fillMaxWidth(1f - reduction.coerceIn(0f, 1f))
                    .fillMaxHeight()
                    .clip(RoundedCornerShape(2.dp))
                    .background(color)
            )
        }
    }
}

@Composable
private fun PolicyRow(threshold: String, description: String, color: Color) {
    Row(
        Modifier.fillMaxWidth().padding(vertical = 3.dp),
        horizontalArrangement = Arrangement.spacedBy(10.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(threshold, color = color, fontSize = 9.sp,
            fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold,
            modifier = Modifier.width(52.dp))
        Text(description, color = TextSecondary, fontSize = 9.sp)
    }
}
