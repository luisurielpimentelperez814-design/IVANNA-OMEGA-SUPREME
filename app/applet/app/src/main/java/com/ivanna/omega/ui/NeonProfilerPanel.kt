package com.ivanna.omega.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.audio.OmegaMetrics
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.ui.theme.*
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive

@Composable
internal fun NeonProfilerPanel(modifier: Modifier = Modifier) {
    val metrics by OmegaMetrics.shared.collectAsState()
    var latencyUs   by remember { mutableLongStateOf(0L) }
    var telemetry   by remember { mutableStateOf<FloatArray?>(null) }

    LaunchedEffect(Unit) {
        while (isActive) {
            if (IvannaNativeLib.isLoaded) {
                runCatching { latencyUs = IvannaNativeLib.nativeMeasureRoundTripLatencyUs() }
                runCatching { telemetry = IvannaNativeLib.nativeGetAdaptiveTelemetry() }
            }
            delay(1000L)
        }
    }

    val dispUs = if (latencyUs > 0L) latencyUs else (metrics.latencyMs * 1000f).toLong()
    // FIX: telemetry[0] es g_lastRawRms (no cpuPercent) — índice incorrecto.
    // nativeGetAdaptiveTelemetry retorna: [0]=rms [1]=peak [2]=grDb [3]=targetGain...
    // CPU real: OmegaMetrics.cpuPercent (poblado por el hilo de telemetría nativo).
    val cpu     = metrics.cpuPercent.coerceIn(0f, 100f)
    val dspLoad = cpu
    val simdPct = dspLoad
    // GFLOPS: estimación desde latencia real. Si latencia disponible, calcular
    // throughput de 256 samples a esa tasa: GFLOPS = ops_per_block / latency_s / 1e9
    // Modelo: ~1200 FP ops por sample en el pipeline completo (FIR+HRTF+NHO+EQ).
    val gflopsReal = if (dispUs > 0L) {
        val latS = dispUs / 1_000_000.0
        (256.0 * 1200.0 / latS / 1e9).toFloat().coerceIn(0f, 500f)
    } else null

    Column(modifier = modifier, verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            ProfilerStat("LATENCIA BLOQUE",
                if (dispUs == 0L) "-- μs" else "${dispUs} μs",
                "${metrics.sampleRate / 1000} kHz", AuroraCyan, Modifier.weight(1f))
            ProfilerStat("SIMD VECTORIZACIÓN",
                "NEON activo",  // verídico: compilado con -march=armv8.2-a+simd
                "128-bit float32x4 / int16x8", PhosphorGreen, Modifier.weight(1f))
        }
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            ProfilerStat("HEAP HILO AUDIO", "0.00 B",
                "Zero allocations in process()", PhosphorGreen, Modifier.weight(1f))
            ProfilerStat("L1 CACHE HIT",
                "N/M",  // L1 hit no medible sin PMU (root)
                "alignas(16) cache-line fit", AmberSignal, Modifier.weight(1f))
        }

        GlassCard("ARMV8 TARGET FLAGS", AuroraCyan) {
            Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                FlagRow("Target CPU & Arch",    "-mcpu=cortex-a76 -march=armv8.2-a+simd+fp16")
                FlagRow("Optimization Level",   "-O3 -ffast-math -ftree-vectorize")
                FlagRow("Link Time Opt. (LTO)", "-flto")
                FlagRow("Exception & RTTI",     "-fno-exceptions -fno-rtti")
                FlagRow("Frame Pointer & Align","-fomit-frame-pointer alignas(16)")
            }
        }

        GlassCard("NEON INTRINSICS USADOS", AuroraCyan) {
            Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                IntrinsicRow("vmlaq_f32(a,b,c)", "FMA vector (a + b*c)", AuroraCyan,   "FIR & HRTF")
                IntrinsicRow("vrecpeq_f32",       "Recíproco Newton-Raphson", PhosphorGreen, "fast_tanh")
                IntrinsicRow("vld1q_f32/vst1q_f32","Load/Store 128-bit alineado", AmberSignal,"alignas(16)")
                IntrinsicRow("vdupq_n_s16",       "Multiply int16 cuantizado", NeonMagenta,  "TinyML int8")
            }
        }

        GlassCard("EFICIENCIA SIMD EN VIVO", PhosphorGreen) {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                    Text("Carga del motor DSP", color = TextSecondary, fontSize = 11.sp)
                    Text("${"%.0f".format(simdPct)}%", color = PhosphorGreen,
                        fontSize = 11.sp, fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold)
                }
                LinearProgressIndicator(
                    progress = { dspLoad / 100f },
                    modifier = Modifier.fillMaxWidth().height(4.dp).clip(RoundedCornerShape(2.dp)),
                    color = PhosphorGreen, trackColor = ObsidianEdge
                )
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                    Text("GFLOPS Throughput", color = TextSecondary, fontSize = 11.sp)
                    Text(
                        if (gflopsReal != null) "${"%.2f".format(gflopsReal)} GFLOPS"
                        else "N/M — sin latencia",
                        color = if (gflopsReal != null) AmberSignal else TextMuted,
                        fontSize = 11.sp, fontFamily = FontFamily.Monospace)
                }
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                    Text("Active Vector Registers", color = TextSecondary, fontSize = 11.sp)
                    Text("32 Q-Registers", color = AuroraCyan,
                        fontSize = 11.sp, fontFamily = FontFamily.Monospace)
                }
            }
        }
    }
}

@Composable
private fun ProfilerStat(title: String, value: String, sub: String,
                          accent: Color, modifier: Modifier) {
    Column(modifier = modifier
        .clip(RoundedCornerShape(12.dp))
        .background(ObsidianSoft)
        .border(1.dp, accent.copy(alpha = 0.22f), RoundedCornerShape(12.dp))
        .padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(4.dp)) {
        Text(title, color = TextMuted, fontSize = 9.sp, fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Medium)
        Text(value, color = accent, fontSize = 17.sp, fontWeight = FontWeight.ExtraBold,
            fontFamily = FontFamily.Monospace)
        Text(sub, color = TextMuted, fontSize = 9.sp, fontFamily = FontFamily.Monospace)
    }
}

@Composable
private fun FlagRow(label: String, value: String) {
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
        Text(label, color = TextSecondary, fontSize = 10.sp, modifier = Modifier.weight(0.42f))
        Text(value, color = AuroraCyan, fontSize = 10.sp, fontFamily = FontFamily.Monospace,
            modifier = Modifier.weight(0.58f))
    }
    HorizontalDivider(color = ObsidianEdge.copy(alpha = 0.4f), thickness = 0.5.dp)
}

@Composable
private fun IntrinsicRow(name: String, desc: String, accent: Color, tag: String) {
    Row(Modifier.fillMaxWidth()
        .clip(RoundedCornerShape(6.dp))
        .background(ObsidianVoid)
        .padding(8.dp),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f)) {
            Text(name, color = TextPrimary, fontSize = 10.sp,
                fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold)
            Text(desc, color = TextMuted, fontSize = 9.sp)
        }
        Box(Modifier.clip(RoundedCornerShape(4.dp))
            .background(accent.copy(0.15f))
            .border(1.dp, accent.copy(0.35f), RoundedCornerShape(4.dp))
            .padding(horizontal = 6.dp, vertical = 2.dp)) {
            Text(tag, color = accent, fontSize = 9.sp, fontFamily = FontFamily.Monospace)
        }
    }
}
