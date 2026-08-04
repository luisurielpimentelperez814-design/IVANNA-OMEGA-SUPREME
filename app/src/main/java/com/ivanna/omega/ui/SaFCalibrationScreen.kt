package com.ivanna.omega.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.spatial.SaFOptimizer
import com.ivanna.omega.ui.theme.*

/**
 * SaFCalibrationScreen — Overlay de calibración HRTF Φ_SAF^∞.
 *
 * Guía al usuario a través de 5 direcciones de escucha (frente, atrás,
 * izquierda, derecha, arriba). Por cada dirección, el usuario indica si
 * el sonido HRTF coincide con su percepción (slider de error 0-1).
 * Al completar, SaFOptimizer.runCalibrationStep() actualiza el estado
 * del optimizador Riemanniano.
 *
 * @param rendererHandle Handle nativo del ObjectRenderer (para ajuste directo
 *                       del HRTF en tiempo real vía nativeObjectRendererSet*).
 * @param onDismiss      Callback para cerrar el overlay.
 */
@Composable
fun SaFCalibrationScreen(
    rendererHandle: Long,
    onDismiss: () -> Unit
) {
    val directions = listOf(
        "FRENTE" to "0°  · 0° elevación",
        "ATRÁS"  to "180° · 0° elevación",
        "IZQ."   to "−90° · 0° elevación",
        "DER."   to "+90° · 0° elevación",
        "ARRIBA" to "0°  · +90° elevación"
    )
    val errors = remember { mutableStateListOf(*Array(5) { 0.5f }) }
    var step         by remember { mutableIntStateOf(0) }
    var isCompleted  by remember { mutableStateOf(false) }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(ObsidianVoid)
            .padding(20.dp),
        contentAlignment = Alignment.Center
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .background(ObsidianSoft, RoundedCornerShape(16.dp))
                .border(1.dp, NeonMagenta.copy(alpha = 0.4f), RoundedCornerShape(16.dp))
                .padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            // ── Header ────────────────────────────────────────────────────────
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically) {
                Column {
                    Text("CALIBRACIÓN Φ_SAF^∞", color = NeonMagenta, fontSize = 13.sp,
                        fontWeight = FontWeight.ExtraBold, letterSpacing = 1.5.sp)
                    Text("Optimizador Riemanniano · 5 direcciones",
                        color = TextMuted, fontSize = 10.sp)
                }
                TextButton(onClick = onDismiss) {
                    Text("CERRAR", color = TextMuted, fontSize = 10.sp)
                }
            }

            HorizontalDivider(color = ObsidianEdge)

            if (!isCompleted) {
                // ── Step actual ────────────────────────────────────────────────
                val (dirLabel, dirDesc) = directions[step]
                Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                        Box(
                            Modifier.size(36.dp)
                                .background(NeonMagenta.copy(alpha = 0.15f), RoundedCornerShape(8.dp))
                                .border(1.dp, NeonMagenta, RoundedCornerShape(8.dp)),
                            contentAlignment = Alignment.Center
                        ) {
                            Text("${step + 1}/5", color = NeonMagenta, fontSize = 11.sp,
                                fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold)
                        }
                        Column {
                            Text(dirLabel, color = TextPrimary, fontSize = 16.sp,
                                fontWeight = FontWeight.Bold)
                            Text(dirDesc, color = TextSecondary, fontSize = 10.sp,
                                fontFamily = FontFamily.Monospace)
                        }
                    }
                    Text(
                        "Escucha el tono de referencia y ajusta el deslizador " +
                        "según qué tan bien coincide la dirección percibida.",
                        color = TextMuted, fontSize = 11.sp, lineHeight = 16.sp
                    )
                    Column {
                        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                            Text("Error percibido", color = TextSecondary, fontSize = 11.sp)
                            val pct = (errors[step] * 100).toInt()
                            val label = when {
                                pct < 20 -> "Perfecta ($pct%)"
                                pct < 50 -> "Buena ($pct%)"
                                pct < 80 -> "Regular ($pct%)"
                                else     -> "Alta ($pct%)"
                            }
                            Text(label, color = NeonMagenta, fontSize = 11.sp,
                                fontFamily = FontFamily.Monospace)
                        }
                        Slider(
                            value = errors[step],
                            onValueChange = { errors[step] = it },
                            colors = SliderDefaults.colors(
                                thumbColor = NeonMagenta, activeTrackColor = NeonMagenta,
                                inactiveTrackColor = ObsidianEdge
                            )
                        )
                    }
                    // Progress dots
                    Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                        repeat(5) { i ->
                            Box(
                                Modifier.size(if (i == step) 12.dp else 8.dp)
                                    .background(
                                        if (i < step) PhosphorGreen
                                        else if (i == step) NeonMagenta
                                        else ObsidianEdge,
                                        RoundedCornerShape(50)
                                    )
                            )
                        }
                    }
                }
                // ── Buttons ───────────────────────────────────────────────────
                Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    if (step > 0) {
                        OutlinedButton(
                            onClick = { step-- },
                            modifier = Modifier.weight(1f),
                            colors = ButtonDefaults.outlinedButtonColors(contentColor = TextSecondary)
                        ) { Text("ATRÁS", fontSize = 11.sp) }
                    }
                    Button(
                        onClick = {
                            if (step < 4) {
                                step++
                            } else {
                                SaFOptimizer.runCalibrationStep(errors.toFloatArray())
                                isCompleted = true
                            }
                        },
                        modifier = Modifier.weight(1f),
                        colors = ButtonDefaults.buttonColors(
                            containerColor = NeonMagenta.copy(alpha = 0.20f),
                            contentColor   = NeonMagenta
                        ),
                        border = androidx.compose.foundation.BorderStroke(1.dp, NeonMagenta),
                        shape  = RoundedCornerShape(10.dp)
                    ) {
                        Text(if (step < 4) "SIGUIENTE" else "CALIBRAR", fontSize = 11.sp,
                            fontFamily = FontFamily.Monospace)
                    }
                }
            } else {
                // ── Resultado ─────────────────────────────────────────────────
                val safState by SaFOptimizer.state.collectAsState()
                Column(verticalArrangement = Arrangement.spacedBy(10.dp),
                    horizontalAlignment = Alignment.CenterHorizontally) {
                    Text("✓ CALIBRACIÓN COMPLETADA", color = PhosphorGreen, fontSize = 13.sp,
                        fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace)
                    Text("Sujeto seleccionado: ${safState.selectedSubject}",
                        color = AuroraCyan, fontSize = 11.sp, fontFamily = FontFamily.Monospace)
                    Row(horizontalArrangement = Arrangement.spacedBy(16.dp)) {
                        Column(horizontalAlignment = Alignment.CenterHorizontally) {
                            Text("‖p_t‖", color = TextMuted, fontSize = 9.sp)
                            Text("%.3f".format(safState.paramNorm),
                                color = TextPrimary, fontSize = 12.sp, fontFamily = FontFamily.Monospace)
                        }
                        Column(horizontalAlignment = Alignment.CenterHorizontally) {
                            Text("Error E", color = TextMuted, fontSize = 9.sp)
                            Text("%.3f".format(safState.errorEnergy),
                                color = TextPrimary, fontSize = 12.sp, fontFamily = FontFamily.Monospace)
                        }
                    }
                    Spacer(Modifier.height(8.dp))
                    Button(
                        onClick = onDismiss,
                        modifier = Modifier.fillMaxWidth().height(46.dp),
                        colors = ButtonDefaults.buttonColors(
                            containerColor = AuroraCyan.copy(alpha = 0.15f),
                            contentColor   = AuroraCyan
                        ),
                        border = androidx.compose.foundation.BorderStroke(1.dp, AuroraCyan),
                        shape  = RoundedCornerShape(10.dp)
                    ) {
                        Text("LISTO", fontFamily = FontFamily.Monospace, fontSize = 12.sp)
                    }
                }
            }
        }
    }
}
