package com.ivanna.omega.ui.oem

import androidx.compose.animation.animateColorAsState
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
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.ui.theme.*

@Composable
fun OemDashboardScreen(
    state: OemState,
    expertMode: Boolean,
    onToggleExpert: () -> Unit,
    onMeasureLatency: () -> Unit,
    onResetClips: () -> Unit,
    onOpenSpatial: () -> Unit,
    onOpenAcoustic: () -> Unit,
    onOpenAi: () -> Unit,
    onOpenThermal: () -> Unit,
    onOpenTelemetry: () -> Unit,
) {
    Column(
        Modifier.fillMaxSize().background(ObsidianVoid)
            .windowInsetsPadding(WindowInsets.systemBars)
            .verticalScroll(rememberScrollState())
    ) {
        // ── Header ────────────────────────────────────────────────────────────
        OemHeader(state = state, expertMode = expertMode, onToggleExpert = onToggleExpert)

        Column(Modifier.padding(horizontal = 14.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {

            // ── Motor + Backend row ───────────────────────────────────────────
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                EngineStateCard(state, Modifier.weight(1f))
                BackendCard(state, Modifier.weight(1f))
            }

            // ── Live signal meters ────────────────────────────────────────────
            SignalMetersCard(state)

            // ── Latency + Clips row ───────────────────────────────────────────
            if (expertMode) {
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    LatencyCard(state = state, onMeasure = onMeasureLatency, modifier = Modifier.weight(1f))
                    ClipCard(state = state, onReset = onResetClips, modifier = Modifier.weight(1f))
                }
            }

            // ── Navigation cards ──────────────────────────────────────────────
            Spacer(Modifier.height(2.dp))
            Text("MÓDULOS", color = TextMuted, fontSize = 9.sp,
                fontWeight = FontWeight.Bold, letterSpacing = 2.sp)
            Spacer(Modifier.height(4.dp))

            NavModuleCard("AUDIO ESPACIAL · HRTF · SOFA",
                "Posición 3D · Sujeto HRTF · Validación SHA256",
                Icons.Default.SurroundSound, AuroraCyan,
                indicator = if (state.hrtfReady) "ACTIVO" else "INACTIVO",
                indicatorOk = state.hrtfReady, onClick = onOpenSpatial)

            NavModuleCard("ACÚSTICA · SAF · RIR",
                "Sala activa · RT60 · Difusión · Ambiente",
                Icons.Default.Waves, PhosphorGreen,
                indicator = if (state.reverbLevel > 0.05f) "PROCESANDO" else "EN ESPERA",
                indicatorOk = state.reverbLevel > 0.05f, onClick = onOpenAcoustic)

            NavModuleCard("IA ADAPTATIVA",
                state.dominantLabel.let { if (it == "—") "Sin señal activa" else "Detectado: $it · Confianza ${(maxOf(state.probVoice, state.probMusic, state.probBass) * 100).toInt()}%" },
                Icons.Default.Psychology, NeonMagenta,
                indicator = if (state.adaptiveRunning) "ACTIVA" else "FALLBACK",
                indicatorOk = state.adaptiveRunning, onClick = onOpenAi)

            NavModuleCard("CONTROL TÉRMICO",
                when (state.thermalLevel) {
                    OemState.ThermalLevel.NORMAL   -> "SoC dentro de límites normales"
                    OemState.ThermalLevel.LIGHT    -> "Reducción leve del excitador"
                    OemState.ThermalLevel.MODERATE -> "Reducción de espacialidad y compresor"
                    OemState.ThermalLevel.SEVERE   -> "Protección térmica agresiva activa"
                },
                Icons.Default.Thermostat,
                when (state.thermalLevel) {
                    OemState.ThermalLevel.NORMAL   -> PhosphorGreen
                    OemState.ThermalLevel.LIGHT    -> AmberSignal
                    OemState.ThermalLevel.MODERATE -> Color(0xFFFF8C00)
                    OemState.ThermalLevel.SEVERE   -> CoralWarn
                },
                indicator = "${(state.thermalLoad * 100).toInt()}% carga",
                indicatorOk = state.thermalLevel == OemState.ThermalLevel.NORMAL,
                onClick = onOpenThermal)

            if (expertMode) {
                NavModuleCard("TELEMETRÍA OEM · DIAGNÓSTICO",
                    "FastRPC · HAL · XRuns · Watchdog · Threads DSP",
                    Icons.Default.Analytics, AmberSignal,
                    indicator = "EXPERTO", indicatorOk = true, onClick = onOpenTelemetry)
            }

            Spacer(Modifier.height(16.dp))
        }
    }
}

// ── Subcomponentes ────────────────────────────────────────────────────────────

@Composable
private fun OemHeader(state: OemState, expertMode: Boolean, onToggleExpert: () -> Unit) {
    val engineColor by animateColorAsState(
        targetValue = when (state.engineState) {
            OemState.EngineState.ACTIVE     -> PhosphorGreen
            OemState.EngineState.SUSPENDED  -> TextMuted
            OemState.EngineState.POWER_SAVE -> AmberSignal
            OemState.EngineState.RECOVERY   -> CoralWarn
            else                            -> TextMuted
        }, animationSpec = tween(600), label = "engineColor"
    )

    Box(
        Modifier.fillMaxWidth()
            .background(Brush.verticalGradient(listOf(Color(0xFF08101E), ObsidianVoid)))
            .padding(horizontal = 16.dp, vertical = 14.dp)
    ) {
        Column {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text("IVANNA OMEGA SUPREME",
                        color = AuroraCyan, fontSize = 12.sp,
                        fontWeight = FontWeight.ExtraBold, letterSpacing = 1.5.sp)
                    Row(verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                        PulseDot(engineColor)
                        Text(
                            when (state.engineState) {
                                OemState.EngineState.ACTIVE     -> "Motor activo"
                                OemState.EngineState.SUSPENDED  -> "Motor suspendido"
                                OemState.EngineState.POWER_SAVE -> "Modo ahorro"
                                OemState.EngineState.RECOVERY   -> "Modo recuperación"
                                else                            -> "Inicializando…"
                            },
                            color = engineColor, fontSize = 11.sp, fontFamily = FontFamily.Monospace
                        )
                    }
                }
                // Toggle experto
                Surface(
                    onClick = onToggleExpert,
                    color = if (expertMode) AmberSignal.copy(alpha = 0.15f) else ObsidianSoft,
                    shape = RoundedCornerShape(20.dp),
                    border = BorderStroke(1.dp, if (expertMode) AmberSignal else ObsidianEdge)
                ) {
                    Row(Modifier.padding(horizontal = 10.dp, vertical = 5.dp),
                        horizontalArrangement = Arrangement.spacedBy(4.dp),
                        verticalAlignment = Alignment.CenterVertically) {
                        Icon(Icons.Default.Tune, null, tint = if (expertMode) AmberSignal else TextMuted,
                            modifier = Modifier.size(13.dp))
                        Text(if (expertMode) "EXPERTO" else "USUARIO",
                            color = if (expertMode) AmberSignal else TextMuted,
                            fontSize = 9.sp, fontWeight = FontWeight.Bold, letterSpacing = 0.8.sp)
                    }
                }
            }
        }
    }
}

@Composable
private fun EngineStateCard(state: OemState, modifier: Modifier) {
    OemCard(modifier) {
        Text("MOTOR", color = TextMuted, fontSize = 8.sp, fontWeight = FontWeight.Bold,
            letterSpacing = 1.sp)
        Spacer(Modifier.height(4.dp))
        Text(
            when (state.backend) {
                OemState.AudioBackend.HEXAGON_DSP -> "Hexagon DSP"
                OemState.AudioBackend.NEON_ARM64  -> "NEON ARM64"
                OemState.AudioBackend.CPU_FALLBACK -> "CPU Fallback"
                else                              -> "Detectando…"
            },
            color = when (state.backend) {
                OemState.AudioBackend.HEXAGON_DSP -> PhosphorGreen
                OemState.AudioBackend.NEON_ARM64  -> AuroraCyan
                else                              -> AmberSignal
            },
            fontSize = 12.sp, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace
        )
        Text(if (state.daemonAlive) "Daemon RT ✓" else "Sin daemon",
            color = if (state.daemonAlive) PhosphorGreen.copy(alpha = 0.8f) else AmberSignal,
            fontSize = 9.sp)
    }
}

@Composable
private fun BackendCard(state: OemState, modifier: Modifier) {
    OemCard(modifier) {
        Text("RUTA DE AUDIO", color = TextMuted, fontSize = 8.sp, fontWeight = FontWeight.Bold,
            letterSpacing = 1.sp)
        Spacer(Modifier.height(4.dp))
        val routeName = when (state.activeRoute.toInt()) {
            1 -> "AURICULARES"; 2 -> "ALTAVOZ"; 3 -> "USB OTG"; 4 -> "BLUETOOTH"; else -> "DETECCIÓN…"
        }
        Text(routeName, color = AuroraCyan, fontSize = 12.sp,
            fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace)
        if (state.usbStreaming)
            Text("USB Pro 384kHz/32bit", color = PhosphorGreen, fontSize = 9.sp)
    }
}

@Composable
private fun SignalMetersCard(state: OemState) {
    OemCard(Modifier.fillMaxWidth()) {
        Text("SEÑAL EN VIVO", color = TextMuted, fontSize = 8.sp, fontWeight = FontWeight.Bold,
            letterSpacing = 1.sp)
        Spacer(Modifier.height(8.dp))
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            MiniMeter("RMS",  state.rms,  AuroraCyan,    Modifier.weight(1f))
            MiniMeter("PEAK", state.peak, NeonMagenta,   Modifier.weight(1f))
            MiniMeter("GR",   (state.grDb / 30f).coerceIn(0f,1f), AmberSignal, Modifier.weight(1f), label2 = "${"%.1f".format(state.grDb)}dB")
            MiniMeter("SAF",  state.reverbLevel, PhosphorGreen, Modifier.weight(1f))
        }
        Spacer(Modifier.height(6.dp))
        // DSP chain bar
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(3.dp)) {
            DspChiplet("EQ",   state.signalActive, AuroraCyan)
            DspChiplet("COMP", state.compAmount > 0.1f, AmberSignal)
            DspChiplet("EXC",  state.exciterRed < 0.9f, NeonMagenta)
            DspChiplet("WIDE", state.spatialWidth > 0.5f, PhosphorGreen)
            DspChiplet("HRTF", state.hrtfReady, AuroraCyan)
            DspChiplet("LIM",  state.safetyMargin < 0.1f, CoralWarn)
        }
    }
}

@Composable
private fun LatencyCard(state: OemState, onMeasure: () -> Unit, modifier: Modifier) {
    OemCard(modifier) {
        Text("LATENCIA DSP", color = TextMuted, fontSize = 8.sp, fontWeight = FontWeight.Bold,
            letterSpacing = 1.sp)
        Spacer(Modifier.height(4.dp))
        if (state.latencyUs > 0L) {
            Text("${"%.2f".format(state.latencyUs / 1000.0)} ms",
                color = AuroraCyan, fontSize = 14.sp, fontWeight = FontWeight.ExtraBold,
                fontFamily = FontFamily.Monospace)
            Text("${state.latencyUs} µs", color = TextMuted, fontSize = 8.sp)
        } else {
            Text("—", color = TextMuted, fontSize = 14.sp, fontFamily = FontFamily.Monospace)
        }
        Spacer(Modifier.height(6.dp))
        OutlinedButton(onClick = onMeasure, modifier = Modifier.fillMaxWidth().height(28.dp),
            contentPadding = PaddingValues(0.dp),
            colors = ButtonDefaults.outlinedButtonColors(contentColor = AuroraCyan),
            border = BorderStroke(1.dp, AuroraCyan.copy(alpha = 0.5f))
        ) { Text("MEDIR", fontSize = 9.sp, fontWeight = FontWeight.Bold, letterSpacing = 0.8.sp) }
    }
}

@Composable
private fun ClipCard(state: OemState, onReset: () -> Unit, modifier: Modifier) {
    OemCard(modifier) {
        Text("CLIPPING", color = TextMuted, fontSize = 8.sp, fontWeight = FontWeight.Bold,
            letterSpacing = 1.sp)
        Spacer(Modifier.height(4.dp))
        Text("${state.clipCount}",
            color = if (state.clipCount > 0) CoralWarn else PhosphorGreen,
            fontSize = 14.sp, fontWeight = FontWeight.ExtraBold, fontFamily = FontFamily.Monospace)
        Text("eventos totales", color = TextMuted, fontSize = 8.sp)
        Spacer(Modifier.height(6.dp))
        OutlinedButton(onClick = onReset, modifier = Modifier.fillMaxWidth().height(28.dp),
            contentPadding = PaddingValues(0.dp),
            colors = ButtonDefaults.outlinedButtonColors(contentColor = CoralWarn),
            border = BorderStroke(1.dp, CoralWarn.copy(alpha = 0.5f))
        ) { Text("RESET", fontSize = 9.sp, fontWeight = FontWeight.Bold, letterSpacing = 0.8.sp) }
    }
}

@Composable
private fun NavModuleCard(title: String, subtitle: String, icon: ImageVector, color: Color,
    indicator: String, indicatorOk: Boolean, onClick: () -> Unit) {
    Surface(
        onClick = onClick,
        modifier = Modifier.fillMaxWidth(),
        color = ObsidianSoft,
        shape = RoundedCornerShape(10.dp),
        border = BorderStroke(1.dp, color.copy(alpha = 0.25f))
    ) {
        Row(Modifier.padding(14.dp), verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            Box(Modifier.size(38.dp).clip(RoundedCornerShape(8.dp)).background(color.copy(alpha = 0.12f)),
                contentAlignment = Alignment.Center) {
                Icon(icon, null, tint = color, modifier = Modifier.size(20.dp))
            }
            Column(Modifier.weight(1f)) {
                Text(title, color = TextPrimary, fontWeight = FontWeight.Bold, fontSize = 11.sp,
                    letterSpacing = 0.4.sp)
                Text(subtitle, color = TextMuted, fontSize = 9.sp, lineHeight = 13.sp)
            }
            Column(horizontalAlignment = Alignment.End) {
                Row(verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                    Box(Modifier.size(6.dp).clip(CircleShape)
                        .background(if (indicatorOk) color else TextMuted))
                    Text(indicator, color = if (indicatorOk) color else TextMuted,
                        fontSize = 8.sp, fontWeight = FontWeight.Bold)
                }
                Icon(Icons.Default.ChevronRight, null, tint = TextMuted.copy(alpha = 0.5f),
                    modifier = Modifier.size(16.dp))
            }
        }
    }
}

// ── Atoms ─────────────────────────────────────────────────────────────────────

@Composable
fun OemCard(modifier: Modifier = Modifier, content: @Composable ColumnScope.() -> Unit) {
    Surface(modifier = modifier, color = ObsidianSoft, shape = RoundedCornerShape(10.dp),
        border = BorderStroke(1.dp, ObsidianEdge)) {
        Column(Modifier.padding(12.dp), content = content)
    }
}

@Composable
private fun MiniMeter(label: String, value: Float, color: Color, modifier: Modifier,
    label2: String? = null) {
    Column(modifier, horizontalAlignment = Alignment.CenterHorizontally) {
        Text(label, color = TextMuted, fontSize = 7.sp, fontWeight = FontWeight.Bold)
        Spacer(Modifier.height(3.dp))
        Box(Modifier.fillMaxWidth().height(3.dp).clip(RoundedCornerShape(2.dp))
            .background(ObsidianEdge)) {
            Box(Modifier.fillMaxWidth(value.coerceIn(0f, 1f)).fillMaxHeight()
                .clip(RoundedCornerShape(2.dp)).background(color))
        }
        Spacer(Modifier.height(2.dp))
        Text(label2 ?: "${"%.3f".format(value)}", color = color, fontSize = 7.sp,
            fontFamily = FontFamily.Monospace)
    }
}

@Composable
private fun DspChiplet(label: String, active: Boolean, color: Color) {
    Box(Modifier.clip(RoundedCornerShape(3.dp))
        .background(if (active) color.copy(alpha = 0.18f) else ObsidianEdge)
        .padding(horizontal = 5.dp, vertical = 2.dp)) {
        Text(label, color = if (active) color else TextMuted,
            fontSize = 7.sp, fontWeight = FontWeight.Bold)
    }
}

@Composable
fun PulseDot(color: Color, size: Dp = 7.dp) {
    val inf = rememberInfiniteTransition(label = "pulse")
    val alpha by inf.animateFloat(initialValue = 1f, targetValue = 0.3f,
        animationSpec = infiniteRepeatable(tween(900), RepeatMode.Reverse), label = "alpha")
    Box(Modifier.size(size).clip(CircleShape).background(color.copy(alpha = alpha)))
}
