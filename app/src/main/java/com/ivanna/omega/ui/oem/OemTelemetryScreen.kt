package com.ivanna.omega.ui.oem

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
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.audio.AudioBackendSelector
import com.ivanna.omega.audio.ThermalGovernor
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.magisk.OmegaEngineBridge
import com.ivanna.omega.ui.theme.*
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

@Composable
fun OemTelemetryScreen(
    state: OemState,
    onBack: () -> Unit,
    onResetClips: () -> Unit,
    onMeasureLatency: () -> Unit,
) {
    val ctx = LocalContext.current
    var exportMsg by remember { mutableStateOf<String?>(null) }

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
                Icon(Icons.Default.ArrowBack, null, tint = AmberSignal)
            }
            Column(Modifier.weight(1f)) {
                Text("TELEMETRÍA OEM", color = AmberSignal,
                    fontWeight = FontWeight.ExtraBold, fontSize = 12.sp, letterSpacing = 1.5.sp)
                Text("Diagnóstico · FastRPC · HAL · Threads",
                    color = TextMuted, fontSize = 9.sp)
            }
            Surface(
                shape = RoundedCornerShape(4.dp),
                color = AmberSignal.copy(alpha = 0.15f)
            ) {
                Text("EXPERTO", color = AmberSignal, fontSize = 8.sp,
                    fontWeight = FontWeight.Bold,
                    modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp))
            }
        }

        Column(
            Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(14.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {

            // FastRPC / HAL / Backend
            OemCard(Modifier.fillMaxWidth()) {
                Text("BACKEND · HAL · FastRPC", color = TextMuted, fontSize = 8.sp,
                    fontWeight = FontWeight.Bold, letterSpacing = 1.sp)
                Spacer(Modifier.height(8.dp))
                TelRow("Backend activo", when (state.backend) {
                    OemState.AudioBackend.HEXAGON_DSP -> "Hexagon DSP (FastRPC)"
                    OemState.AudioBackend.NEON_ARM64  -> "NEON ARM64 (Kernel)"
                    OemState.AudioBackend.CPU_FALLBACK -> "CPU Fallback"
                    else -> "Desconocido"
                }, when (state.backend) {
                    OemState.AudioBackend.HEXAGON_DSP -> PhosphorGreen
                    OemState.AudioBackend.NEON_ARM64  -> AuroraCyan
                    else -> AmberSignal
                })
                TelRow("Daemon ivanna_omega", if (state.daemonAlive) "Corriendo · SHM activa" else "Caído", if (state.daemonAlive) PhosphorGreen else CoralWarn)
                TelRow("Lib nativa", if (state.nativeLoaded) "libivanna_omega_native.so ✓" else "NO CARGADA", if (state.nativeLoaded) PhosphorGreen else CoralWarn)
                TelRow("Motor adaptativo", if (state.adaptiveRunning) "Activo · AdaptiveDecisionEngine" else "Inactivo", if (state.adaptiveRunning) PhosphorGreen else TextMuted)
                TelRow("API térmica", if (state.thermalApiOk) "PowerManager API 29+ OK" else "No disponible (API < 29)", if (state.thermalApiOk) PhosphorGreen else AmberSignal)
            }

            // Pipeline unificado completo
            OemCard(Modifier.fillMaxWidth()) {
                Text("PIPELINE UNIFICADO · TIEMPO REAL", color = TextMuted, fontSize = 8.sp,
                    fontWeight = FontWeight.Bold, letterSpacing = 1.sp)
                Spacer(Modifier.height(8.dp))
                TelRow("RMS",            "${"%.4f".format(state.rms)}",        AuroraCyan)
                TelRow("Peak",           "${"%.4f".format(state.peak)}",       AuroraCyan)
                TelRow("GR (comp)",      "${"%.2f".format(state.grDb)} dB",    AmberSignal)
                TelRow("Target gain",    "${"%.4f".format(state.targetGain)}", TextSecondary)
                TelRow("Comp amount",    "${"%.0f".format(state.compAmount*100)}%", TextSecondary)
                TelRow("Exciter red.",   "${"%.0f".format(state.exciterRed*100)}%", NeonMagenta)
                TelRow("Spatial width",  "${"%.3f".format(state.spatialWidth)}",   AuroraCyan)
                TelRow("Adaptive active","${state.adaptiveActive}",            PhosphorGreen)
                TelRow("Voice protect",  "${"%.0f".format(state.voiceProtect*100)}%", PhosphorGreen)
                TelRow("Safety margin",  "${"%.0f".format(state.safetyMargin*100)}%", PhosphorGreen)
                TelRow("Applied",        "${"%.0f".format(state.applied*100)}%",       TextSecondary)
            }

            // Latencia y XRuns
            OemCard(Modifier.fillMaxWidth()) {
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically) {
                    Text("LATENCIA · CLIPPING", color = TextMuted, fontSize = 8.sp,
                        fontWeight = FontWeight.Bold, letterSpacing = 1.sp)
                    Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                        SmallAction("MEDIR", AuroraCyan, onMeasureLatency)
                        SmallAction("RESET", CoralWarn, onResetClips)
                    }
                }
                Spacer(Modifier.height(8.dp))
                TelRow("Latencia DSP (JNI)",
                    if (state.latencyUs > 0L) "${"%.2f".format(state.latencyUs / 1000.0)} ms (${state.latencyUs} µs)"
                    else "Pulsa MEDIR (n=100 corridas CLOCK_MONOTONIC)",
                    AuroraCyan)
                TelRow("Eventos clip (SafetyLimiter)",
                    "${state.clipCount} totales",
                    if (state.clipCount > 0) CoralWarn else PhosphorGreen)
                TelRow("Carga térmica DSP",
                    "${"%.0f".format(state.thermalLoad * 100)}% (headroom ${"%.0f".format((1f-state.thermalLoad)*100)}%)",
                    when (state.thermalLevel) {
                        OemState.ThermalLevel.NORMAL -> PhosphorGreen
                        OemState.ThermalLevel.LIGHT  -> AmberSignal
                        else -> CoralWarn
                    })
            }

            // Clasificador IA
            OemCard(Modifier.fillMaxWidth()) {
                Text("CLASIFICADOR IA · CARACTERÍSTICAS", color = TextMuted, fontSize = 8.sp,
                    fontWeight = FontWeight.Bold, letterSpacing = 1.sp)
                Spacer(Modifier.height(8.dp))
                TelRow("Clase dominante",   state.dominantLabel, NeonMagenta)
                TelRow("P(voz)",            "${"%.3f".format(state.probVoice)}", AuroraCyan)
                TelRow("P(música)",         "${"%.3f".format(state.probMusic)}", NeonMagenta)
                TelRow("P(bajos)",          "${"%.3f".format(state.probBass)}", AmberSignal)
                TelRow("P(silencio)",       "${"%.3f".format(state.probSilence)}", TextMuted)
                TelRow("Percusividad",      "${"%.3f".format(state.percussiveness)}", AuroraCyan)
                TelRow("Tonalidad",         "${"%.3f".format(state.tonality)}", AuroraCyan)
                TelRow("Reverb detectado",  "${"%.3f".format(state.reverbLevel)}", PhosphorGreen)
                TelRow("Rango dinámico",    "${"%.1f".format(state.dynRange)} dB", AuroraCyan)
                TelRow("Centroide espect.", "${state.spectralCentroid.toInt()} Hz", TextSecondary)
            }

            // Spatial / HRTF
            OemCard(Modifier.fillMaxWidth()) {
                Text("SPATIAL · HRTF · SOFA", color = TextMuted, fontSize = 8.sp,
                    fontWeight = FontWeight.Bold, letterSpacing = 1.sp)
                Spacer(Modifier.height(8.dp))
                TelRow("IvannaSpatialManager", if (state.hrtfReady) "Ready" else "No inicializado", if (state.hrtfReady) PhosphorGreen else AmberSignal)
                TelRow("Sujeto HRTF activo", state.hrtfSubject, AuroraCyan)
                TelRow("USB Pro streaming", if (state.usbStreaming) "Activo · 384kHz/32bit" else "En espera", if (state.usbStreaming) PhosphorGreen else TextMuted)
                TelRow("Evo EQ generación", "${state.evoGeneration}", AuroraCyan)
                TelRow("Evo EQ fitness", "${"%.5f".format(state.evoBestFitness)}", AuroraCyan)
            }

            // Export
            exportMsg?.let { msg ->
                Surface(shape = RoundedCornerShape(8.dp),
                    color = PhosphorGreen.copy(alpha = 0.1f),
                    border = BorderStroke(1.dp, PhosphorGreen.copy(alpha = 0.4f)),
                    modifier = Modifier.fillMaxWidth()) {
                    Text(msg, color = PhosphorGreen, fontSize = 9.sp,
                        fontFamily = FontFamily.Monospace,
                        modifier = Modifier.padding(10.dp))
                }
            }

            Button(
                onClick = {
                    val ts = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(Date())
                    val file = File(ctx.getExternalFilesDir(null), "ivanna_diag_$ts.txt")
                    file.writeText(buildDiagReport(state))
                    exportMsg = "✓ Exportado: ${file.absolutePath}"
                },
                modifier = Modifier.fillMaxWidth(),
                colors = ButtonDefaults.buttonColors(containerColor = AmberSignal.copy(alpha = 0.15f)),
                border = BorderStroke(1.dp, AmberSignal.copy(alpha = 0.5f))
            ) {
                Icon(Icons.Default.FileDownload, null, tint = AmberSignal,
                    modifier = Modifier.size(16.dp))
                Spacer(Modifier.width(8.dp))
                Text("EXPORTAR DIAGNÓSTICO", color = AmberSignal,
                    fontSize = 10.sp, fontWeight = FontWeight.Bold)
            }

            Spacer(Modifier.height(8.dp))
        }
    }
}

@Composable
private fun SmallAction(label: String, color: Color, onClick: () -> Unit) {
    Surface(onClick = onClick,
        shape = RoundedCornerShape(4.dp),
        color = color.copy(alpha = 0.15f),
        border = BorderStroke(1.dp, color.copy(alpha = 0.4f))
    ) {
        Text(label, color = color, fontSize = 8.sp, fontWeight = FontWeight.Bold,
            modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp))
    }
}

private fun buildDiagReport(s: OemState): String = buildString {
    val ts = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US).format(Date())
    appendLine("IVANNA OMEGA SUPREME — Diagnóstico OEM")
    appendLine("Timestamp: $ts")
    appendLine("=".repeat(50))
    appendLine("Motor: ${s.engineState} | Backend: ${s.backend}")
    appendLine("Daemon: ${s.daemonAlive} | Nativa: ${s.nativeLoaded} | Adaptivo: ${s.adaptiveRunning}")
    appendLine()
    appendLine("Pipeline: RMS=${s.rms} PEAK=${s.peak} GR=${s.grDb}dB")
    appendLine("  compAmount=${s.compAmount} exciterRed=${s.exciterRed} spatialWidth=${s.spatialWidth}")
    appendLine("  safetyMargin=${s.safetyMargin} applied=${s.applied} clips=${s.clipCount}")
    appendLine()
    appendLine("Térmico: load=${s.thermalLoad} nivel=${s.thermalLevel} apiOk=${s.thermalApiOk}")
    appendLine()
    appendLine("Clasificador: dominante=${s.dominantLabel}")
    appendLine("  voz=${s.probVoice} música=${s.probMusic} bajos=${s.probBass} silencio=${s.probSilence}")
    appendLine("  percusividad=${s.percussiveness} tonalidad=${s.tonality} reverb=${s.reverbLevel}")
    appendLine("  rango_dyn=${s.dynRange}dB centroide=${s.spectralCentroid}Hz")
    appendLine()
    appendLine("Spatial: HRTF=${s.hrtfReady} sujeto=${s.hrtfSubject} USB=${s.usbStreaming}")
    appendLine("Evo EQ: gen=${s.evoGeneration} fitness=${s.evoBestFitness}")
    appendLine()
    appendLine("Latencia: ${if (s.latencyUs > 0L) "${s.latencyUs}µs" else "no medida"}")
}
