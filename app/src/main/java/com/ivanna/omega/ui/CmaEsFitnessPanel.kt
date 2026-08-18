package com.ivanna.omega.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.ui.theme.*
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlin.math.sin
import kotlin.math.cos

@Composable
internal fun CmaEsFitnessPanel(popSize: Int = 4, modifier: Modifier = Modifier) {
    val fitnessHistory = remember { ArrayDeque<Float>(80) }
    var bestFitness by remember { mutableDoubleStateOf(0.0) }
    var currentGen  by remember { mutableIntStateOf(0) }
    var isRunning   by remember { mutableStateOf(false) }
    var stepSize    by remember { mutableFloatStateOf(0.10f) }
    // Rango dinámico para normalización adaptativa de la curva
    var fitnessMin  by remember { mutableDoubleStateOf(Double.MAX_VALUE) }
    var fitnessMax  by remember { mutableDoubleStateOf(Double.MIN_VALUE) }

    LaunchedEffect(Unit) {
        while (isActive) {
            if (IvannaNativeLib.isLoaded) {
                runCatching {
                    val bf  = IvannaNativeLib.nativeGetBestFitness()
                    val gen = IvannaNativeLib.nativeGetGeneration()
                    val run = IvannaNativeLib.nativeIsAdaptiveEngineRunning()
                    bestFitness = bf; currentGen = gen; isRunning = run
                    // Normalización adaptativa: rango crece con los valores observados.
                    // La normalización fija (bf+1)/1 comprimía todo a ~1.0 cuando
                    // bestFitness > 0, dejando la curva pegada al tope del canvas.
                    if (bf < fitnessMin) fitnessMin = bf
                    if (bf > fitnessMax) fitnessMax = bf
                    val range = (fitnessMax - fitnessMin).coerceAtLeast(0.001)
                    val norm = ((bf - fitnessMin) / range).coerceIn(0.0, 1.0).toFloat()
                    if (fitnessHistory.size >= 80) fitnessHistory.removeFirst()
                    fitnessHistory.addLast(norm)
                }
            }
            delay(500L)
        }
    }

    Column(modifier = modifier, verticalArrangement = Arrangement.spacedBy(10.dp)) {
        GlassCard("512-BAND GENOME ENGINE  LM-CMA-ES EVOLUTIONARY FIR EQUALIZER", PhosphorGreen,
            subtitle = "256-Tap time-domain FIR · covariance adaptation · smooth phase penalization") {
            val stateColor = when { isRunning -> PhosphorGreen; currentGen > 0 -> AmberSignal; else -> TextMuted }
            val stateLabel = when { isRunning -> "EVOLUTION RUNNING"; currentGen > 0 -> "CONVERGIDO gen. $currentGen"; else -> "EN ESPERA" }
            Box(Modifier.fillMaxWidth().height(40.dp)
                .clip(RoundedCornerShape(8.dp))
                .background(stateColor.copy(0.12f))
                .border(1.dp, stateColor.copy(0.4f), RoundedCornerShape(8.dp)),
                contentAlignment = Alignment.Center) {
                Text(stateLabel, color = stateColor, fontSize = 12.sp,
                    fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold)
            }
        }

        GlassCard("512-BAND EVOLUTIONARY MAGNITUDE RESPONSE CURVE", PhosphorGreen,
            rightSlot = {
                Column(horizontalAlignment = Alignment.End) {
                    Text("Fitness Score:", color = TextMuted, fontSize = 9.sp)
                    Text("${"%.5f".format(bestFitness)}", color = PhosphorGreen,
                        fontSize = 11.sp, fontFamily = FontFamily.Monospace)
                }
            }) {
            Canvas(Modifier.fillMaxWidth().height(110.dp)
                .clip(RoundedCornerShape(8.dp)).background(ObsidianVoid)) {
                val w = size.width; val h = size.height
                for (i in 1..3) drawLine(ObsidianEdge.copy(0.3f), Offset(0f, h * i / 4f), Offset(w, h * i / 4f), 0.5f)
                for (i in 0..4) drawLine(ObsidianEdge.copy(0.2f), Offset(w * i / 4f, 0f), Offset(w * i / 4f, h), 0.5f)
                val hist = fitnessHistory.toList()
                if (hist.size >= 2) {
                    val path = Path()
                    hist.forEachIndexed { idx, v ->
                        val x = idx / (hist.size - 1).toFloat() * w
                        val y = h - v * h * 0.85f - h * 0.05f
                        if (idx == 0) path.moveTo(x, y) else path.lineTo(x, y)
                    }
                    drawPath(path, PhosphorGreen, style = Stroke(2f, cap = StrokeCap.Round))
                    drawCircle(PhosphorGreen, 4f, Offset(w, h - hist.last() * h * 0.85f - h * 0.05f))
                } else {
                    val path = Path()
                    for (i in 0..80) {
                        val x = i / 80f * w; val t = i / 80f * 3.14159f
                        val y = h * 0.5f - (sin(t * 2 + cos(t * 3) * 0.5f) * h * 0.3f).toFloat()
                        if (i == 0) path.moveTo(x, y) else path.lineTo(x, y)
                    }
                    drawPath(path, PhosphorGreen.copy(0.35f), style = Stroke(1.5f, cap = StrokeCap.Round))
                }
            }
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                listOf("20 Hz","250 Hz","1,000 Hz","4,000 Hz","20,000 Hz").forEach {
                    Text(it, color = TextMuted, fontSize = 8.sp, fontFamily = FontFamily.Monospace)
                }
            }
        }

        GlassCard("PARÁMETROS CMA-ES", AuroraCyan) {
            Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Column {
                    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                        Column {
                            Text("CMA-ES Step Size (σ):", color = TextSecondary, fontSize = 11.sp)
                            Text("Distancia de mutación por generación", color = TextMuted, fontSize = 9.sp)
                        }
                        Text("${"%.2f".format(stepSize)}", color = AuroraCyan,
                            fontSize = 15.sp, fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold)
                    }
                    Slider(value = stepSize,
                        onValueChange = { v ->
                            stepSize = v
                            if (IvannaNativeLib.isLoaded)
                                runCatching { IvannaNativeLib.nativeSetMutationRate(v) }
                        },
                        valueRange = 0.01f..0.5f,
                        colors = SliderDefaults.colors(thumbColor = AuroraCyan,
                            activeTrackColor = AuroraCyan, inactiveTrackColor = ObsidianEdge))
                    Text("Menor valor = calibración acústica de mayor precisión",
                        color = TextMuted, fontSize = 9.sp)
                }
                HorizontalDivider(color = ObsidianEdge, thickness = 0.5.dp)
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceEvenly) {
                    Column(horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.spacedBy(3.dp)) {
                        Text("FIR FILTER TAPS", color = TextMuted, fontSize = 9.sp, fontFamily = FontFamily.Monospace)
                        Text("256 Time-Domain Taps", color = TextPrimary, fontSize = 11.sp,
                            fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold)
                        Text("NEON 4-Tap Unroll (vmlaq_f32)", color = TextMuted, fontSize = 9.sp)
                    }
                    VerticalDivider(color = ObsidianEdge, modifier = Modifier.height(52.dp))
                    Column(horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.spacedBy(3.dp)) {
                        Text("CMA-ES GENOME POPULATION", color = TextMuted, fontSize = 9.sp, fontFamily = FontFamily.Monospace)
                        Text("λ = $popSize Candidates", color = PhosphorGreen, fontSize = 13.sp,
                            fontFamily = FontFamily.Monospace, fontWeight = FontWeight.ExtraBold)
                        Text("Smooth Phase Penalization Active", color = PhosphorGreen, fontSize = 9.sp)
                    }
                }
            }
        }
    }
}
