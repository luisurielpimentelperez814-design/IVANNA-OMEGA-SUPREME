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
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.withContext

/** Features de CPU reales del dispositivo (línea "Features" de /proc/cpuinfo); null si no se puede leer. */
private fun readCpuFeatures(): Set<String>? = runCatching {
    java.io.File("/proc/cpuinfo").useLines { lines ->
        lines.firstOrNull { it.startsWith("Features") }
            ?.substringAfter(':')?.trim()?.split(' ')?.filter { it.isNotEmpty() }?.toSet()
    }
}.getOrNull()

private fun mb(bytes: Long) = "%.1f MB".format(bytes / 1048576.0)

@Composable
internal fun NeonProfilerPanel(modifier: Modifier = Modifier) {
    val metrics by OmegaMetrics.shared.collectAsState()
    var latencyUs   by remember { mutableLongStateOf(0L) }
    var appCpuPct   by remember { mutableStateOf<Float?>(null) }
    var nativeHeap  by remember { mutableLongStateOf(0L) }
    var javaHeap    by remember { mutableLongStateOf(0L) }

    // Todo lo de abajo es medición real: nada de cifras fijas ni modelos estimados.
    val cpuFeatures = remember { readCpuFeatures() }
    val buildFlags = remember {
        if (IvannaNativeLib.isLoaded) runCatching { IvannaNativeLib.nativeGetBuildFlags() }.getOrNull() else null
    }

    LaunchedEffect(Unit) {
        var lastCpu  = android.os.Process.getElapsedCpuTime()
        var lastWall = android.os.SystemClock.elapsedRealtime()
        while (isActive) {
            if (IvannaNativeLib.isLoaded) {
                val us = withContext(Dispatchers.Default) {
                    runCatching { IvannaNativeLib.nativeMeasureRoundTripLatencyUs() }.getOrDefault(0L)
                }
                latencyUs = us
            }
            val cpu = android.os.Process.getElapsedCpuTime()
            val wall = android.os.SystemClock.elapsedRealtime()
            val dw = wall - lastWall
            if (dw > 0L) appCpuPct = ((cpu - lastCpu).toFloat() / dw * 100f).coerceAtLeast(0f)
            lastCpu = cpu; lastWall = wall
            nativeHeap = android.os.Debug.getNativeHeapAllocatedSize()
            val rt = Runtime.getRuntime()
            javaHeap = rt.totalMemory() - rt.freeMemory()
            delay(1000L)
        }
    }

    val dispUs = if (latencyUs > 0L) latencyUs else (metrics.latencyMs * 1000f).toLong()
    val dspLoad: Float? = if (metrics.dspActive && metrics.dspLoadPercent > 0f) metrics.dspLoadPercent else null
    val neon = cpuFeatures?.contains("asimd")

    Column(modifier = modifier, verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            ProfilerStat("LATENCIA IDA Y VUELTA",
                if (dispUs <= 0L) "—" else "${dispUs} μs",
                "${metrics.sampleRate / 1000} kHz", AuroraCyan, Modifier.weight(1f))
            ProfilerStat("CPU DE LA APP",
                appCpuPct?.let { "%.0f %%".format(it) } ?: "—",
                "de 1 núcleo · ${Runtime.getRuntime().availableProcessors()} núcleos", PhosphorGreen, Modifier.weight(1f))
        }
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            ProfilerStat("HEAP NATIVO", if (nativeHeap > 0L) mb(nativeHeap) else "—",
                "asignado por el proceso", AmberSignal, Modifier.weight(1f))
            ProfilerStat("HEAP JAVA", if (javaHeap > 0L) mb(javaHeap) else "—",
                "ART en uso", AuroraCyan, Modifier.weight(1f))
        }

        GlassCard("CPU DEL DISPOSITIVO", AuroraCyan, "Detectado en tiempo de ejecución (/proc/cpuinfo)") {
            Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                if (cpuFeatures == null) {
                    FlagRow("Features", "no disponible")
                } else {
                    FlagRow("NEON / ASIMD", if (neon == true) "sí" else "no")
                    FlagRow("FP16 aritmético (asimdhp)", if (cpuFeatures.contains("asimdhp")) "sí" else "no")
                    FlagRow("Dot product int8 (asimddp)", if (cpuFeatures.contains("asimddp")) "sí" else "no")
                    FlagRow("Int8 matmul (i8mm)", if (cpuFeatures.contains("i8mm")) "sí" else "no")
                    FlagRow("BFloat16 (bf16)", if (cpuFeatures.contains("bf16")) "sí" else "no")
                }
                FlagRow("ABI", android.os.Build.SUPPORTED_ABIS.firstOrNull() ?: "—")
            }
        }

        GlassCard("FLAGS DE COMPILACIÓN", AuroraCyan, "Leídos del binario nativo cargado") {
            Text(
                buildFlags?.takeIf { it.isNotBlank() && it != "unknown" } ?: "no disponible (motor nativo sin cargar)",
                color = TextSecondary, fontSize = 10.sp, fontFamily = FontFamily.Monospace
            )
        }

        GlassCard("NEON INTRINSICS EN EL CÓDIGO", AuroraCyan, "Módulos que los usan (verificado en el fuente)") {
            Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                IntrinsicRow("vmlaq_f32(a,b,c)", "Multiplicar-acumular vectorial (a + b·c)", AuroraCyan, "TinyML · Cochlear · Gammatone")
                IntrinsicRow("vrecpeq_f32", "Estimación de recíproco", PhosphorGreen, "AntiDolbyAI · FusionCore")
                IntrinsicRow("vld1q_f32 / vst1q_f32", "Carga/almacén de 128 bits", AmberSignal, "HRTF · room_model · spatial")
                IntrinsicRow("vdupq_n_s16", "Difusión de escalar int16 a vector", NeonMagenta, "Psychoacoustics")
            }
        }

        GlassCard("MOTOR EN VIVO", PhosphorGreen, "Telemetría del hilo de audio") {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                    Text("Presupuesto del hilo de audio (DSP)", color = TextSecondary, fontSize = 11.sp)
                    Text(dspLoad?.let { "%.0f %%".format(it) } ?: "—", color = PhosphorGreen,
                        fontSize = 11.sp, fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold)
                }
                LinearProgressIndicator(
                    progress = { ((dspLoad ?: 0f) / 100f).coerceIn(0f, 1f) },
                    modifier = Modifier.fillMaxWidth().height(4.dp).clip(RoundedCornerShape(2.dp)),
                    color = PhosphorGreen, trackColor = ObsidianEdge
                )
                LiveRow("Salud del buffer", if (metrics.dspActive) "%.0f %%".format(metrics.bufferHealthPercent) else "—")
                LiveRow("Jitter", if (metrics.dspActive && metrics.jitterMs > 0f) "%.2f ms".format(metrics.jitterMs) else "—")
                LiveRow("Underruns", if (metrics.dspActive) metrics.underrunCount.toString() else "—")
                LiveRow("Bypass por presupuesto", if (metrics.dspActive) metrics.budgetBypasses.toString() else "—")
                LiveRow("Eventos anti-pop", if (metrics.dspActive) metrics.antiPopEvents.toString() else "—")
                LiveRow("Resincronizaciones", if (metrics.dspActive) metrics.resyncCount.toString() else "—")
                LiveRow("Códec / ruta", if (metrics.activeCodec != "—") "${metrics.activeCodec} · ${metrics.audioRoute}" else metrics.audioRoute)
            }
        }
    }
}

@Composable
private fun LiveRow(label: String, value: String) {
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
        Text(label, color = TextSecondary, fontSize = 11.sp)
        Text(value, color = AuroraCyan, fontSize = 11.sp, fontFamily = FontFamily.Monospace)
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
