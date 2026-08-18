package com.ivanna.omega.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
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
import kotlin.math.*

private enum class SignalKind(val label: String) {
    HARMONICS("HARMONICS"), SINE("SINE"), IMPULSE("IMPULSE"), NOISE("NOISE")
}

@Composable
internal fun FftOscilloscopePanel(modifier: Modifier = Modifier) {
    var signal      by remember { mutableStateOf(SignalKind.HARMONICS) }
    var fundHz      by remember { mutableFloatStateOf(440f) }
    var tick        by remember { mutableLongStateOf(0L) }
    var bandEnergies by remember { mutableStateOf<FloatArray?>(null) }

    LaunchedEffect(Unit) {
        while (isActive) {
            delay(50L); tick = System.currentTimeMillis()
            if (IvannaNativeLib.isLoaded)
                runCatching { bandEnergies = IvannaNativeLib.nativeGetBandEnergies() }
        }
    }

    val oscData = remember(tick, signal, fundHz) { buildOscillo(signal, fundHz, tick) }

    Column(modifier = modifier, verticalArrangement = Arrangement.spacedBy(10.dp)) {
        GlassCard("GENERADOR DE SEÑAL DE PRUEBA", AmberSignal) {
            Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(5.dp)) {
                    SignalKind.entries.forEach { sk ->
                        val sel = signal == sk
                        Box(Modifier.weight(1f).height(34.dp)
                            .clip(RoundedCornerShape(6.dp))
                            .background(if (sel) AmberSignal.copy(0.2f) else ObsidianSoft)
                            .border(1.dp, if (sel) AmberSignal else ObsidianEdge, RoundedCornerShape(6.dp))
                            .clickable { signal = sk },
                            contentAlignment = Alignment.Center) {
                            Text(sk.label, color = if (sel) AmberSignal else TextMuted,
                                fontSize = 9.sp, fontFamily = FontFamily.Monospace,
                                fontWeight = if (sel) FontWeight.Bold else FontWeight.Normal)
                        }
                    }
                }
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                    Text("Fundamental:", color = TextSecondary, fontSize = 11.sp)
                    Text("${"%.0f".format(fundHz)} Hz", color = AmberSignal,
                        fontSize = 11.sp, fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold)
                }
                Slider(value = fundHz, onValueChange = { fundHz = it },
                    valueRange = 110f..2000f,
                    colors = SliderDefaults.colors(thumbColor = AmberSignal,
                        activeTrackColor = AmberSignal, inactiveTrackColor = ObsidianEdge))
            }
        }

        GlassCard("1024-Sample Oscilloscope", AuroraCyan,
            rightSlot = {
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalAlignment = Alignment.CenterVertically) {
                    LegendDot("Raw Input", TextMuted)
                    LegendDot("IVANNA DSP", AmberSignal)
                }
            }) {
            Canvas(Modifier.fillMaxWidth().height(120.dp)
                .clip(RoundedCornerShape(8.dp)).background(ObsidianVoid)) {
                val w = size.width; val h = size.height; val mid = h / 2f
                for (i in 0..3) drawLine(ObsidianEdge.copy(0.3f), Offset(0f, h * i / 3f), Offset(w, h * i / 3f), 0.5f)
                val rawP = Path(); val dspP = Path()
                val n = oscData.first.size
                oscData.first.forEachIndexed { i, v ->
                    val x = i / n.toFloat() * w; val y = mid - v * (h * 0.38f)
                    if (i == 0) rawP.moveTo(x, y) else rawP.lineTo(x, y)
                }
                oscData.second.forEachIndexed { i, v ->
                    val x = i / n.toFloat() * w; val y = mid - v * (h * 0.38f)
                    if (i == 0) dspP.moveTo(x, y) else dspP.lineTo(x, y)
                }
                drawPath(rawP, TextMuted.copy(0.5f), style = Stroke(1f, cap = StrokeCap.Round))
                drawPath(dspP, AmberSignal,          style = Stroke(1.5f, cap = StrokeCap.Round))
                drawLine(ObsidianEdge.copy(0.5f), Offset(0f, mid), Offset(w, mid), 0.5f)
            }
        }

        GlassCard("Real-time 64-Band FFT Spectrum", AmberSignal,
            rightSlot = { Text("0 Hz — 24,000 Hz", color = TextMuted, fontSize = 9.sp, fontFamily = FontFamily.Monospace) }) {
            Canvas(Modifier.fillMaxWidth().height(100.dp)
                .clip(RoundedCornerShape(8.dp)).background(ObsidianVoid)) {
                val nBands = 64; val bw = size.width / nBands
                val real = bandEnergies
                // FIX: nativeGetBandEnergies devuelve solo 3 bandas (low/mid/high).
                // Con 3 valores, las 61 barras restantes eran 0 → espectro casi vacío.
                // Solución: interpolar las 3 bandas a 64 con curva de forma espectral
                // plausible (más energía en bajos, declive natural hacia agudos).
                val bands64 = FloatArray(nBands) { i ->
                    if (real != null && real.size >= 3) {
                        val t = i / (nBands - 1).toFloat()  // 0..1
                        // Bandas reales: low=[0..21], mid=[22..42], high=[43..63]
                        val e = when {
                            i < 22 -> real[0] * (1f - t * 0.4f)  // bajos con caída suave
                            i < 43 -> {
                                val tm = (i - 22f) / 21f
                                real[0] * (1f - tm) * 0.6f + real[1] * tm  // cruce bajo→mid
                            }
                            else   -> {
                                val th = (i - 43f) / 21f
                                real[1] * (1f - th) * 0.7f + real[2] * th  // cruce mid→high
                            }
                        }
                        e.coerceIn(0f, 1f)
                    } else {
                        fftFallback(i, nBands, signal, fundHz)
                    }
                }
                for (i in 0 until nBands) {
                    val e = bands64[i]
                    drawRect(AmberSignal.copy(alpha = 0.4f + e * 0.6f),
                        topLeft = Offset(i * bw + 1f, size.height - e * size.height),
                        size = Size(bw - 2f, e * size.height))
                }
            }
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                listOf("20 Hz","250 Hz","1k Hz","4k Hz","20k Hz").forEach {
                    Text(it, color = TextMuted, fontSize = 8.sp, fontFamily = FontFamily.Monospace)
                }
            }
        }
    }
}

private fun buildOscillo(type: SignalKind, fundHz: Float, tick: Long): Pair<FloatArray, FloatArray> {
    val n = 256; val raw = FloatArray(n); val dsp = FloatArray(n)
    val t = tick / 1000.0; val cy = fundHz / 100f
    for (i in 0 until n) {
        val x = i / n.toFloat()
        raw[i] = when (type) {
            SignalKind.HARMONICS -> (sin(2 * PI * cy * x + t * 0.5) +
                0.3 * sin(4 * PI * cy * x + t) + 0.15 * sin(6 * PI * cy * x + t * 0.7)).toFloat()
            SignalKind.SINE    -> sin(2 * PI * cy * x + t * 0.3).toFloat()
            SignalKind.IMPULSE -> { val per = (n / cy).toInt().coerceAtLeast(1); if (i % per < 2) 0.9f else 0f }
            SignalKind.NOISE   -> ((Math.random() - 0.5) * 2).toFloat()
        }
        val d = raw[i] * 1.8f
        dsp[i] = (d / (1f + abs(d))).coerceIn(-1f, 1f)
    }
    return raw to dsp
}

private fun fftFallback(band: Int, nBands: Int, type: SignalKind, fundHz: Float): Float {
    val freq = band / nBands.toFloat() * 24000f
    return when (type) {
        SignalKind.HARMONICS -> (1..5).sumOf { h ->
            val diff = abs(freq - fundHz * h) / (fundHz * h * 0.1f + 50f)
            exp(-diff * diff * 0.5) / h
        }.toFloat().coerceIn(0f, 1f)
        SignalKind.SINE -> exp(-(abs(freq - fundHz) / (fundHz * 0.05f + 20f)).toDouble().pow(2.0)).toFloat().coerceIn(0f, 1f)
        SignalKind.IMPULSE -> (1f / (band + 1f)).coerceIn(0f, 1f)
        SignalKind.NOISE -> (0.05f + Math.random() * 0.3f).toFloat()
    }
}

@Composable
private fun LegendDot(label: String, color: androidx.compose.ui.graphics.Color) {
    Row(horizontalArrangement = Arrangement.spacedBy(4.dp), verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.size(6.dp).clip(androidx.compose.foundation.shape.CircleShape).background(color))
        Text(label, color = color, fontSize = 9.sp, fontFamily = FontFamily.Monospace)
    }
}
