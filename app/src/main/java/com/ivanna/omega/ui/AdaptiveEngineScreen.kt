package com.ivanna.omega.ui

import android.content.Context
import androidx.compose.animation.core.*
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.runtime.livedata.observeAsState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.audio.AdaptiveBackend
import com.ivanna.omega.audio.AdaptiveMode
import com.ivanna.omega.audio.AudioStateManager
import com.ivanna.omega.audio.VoiceProtectionManager
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.ui.theme.*
import kotlin.math.abs

@Composable
internal fun AdaptiveEngineScreen(
    voiceProtectionManager: VoiceProtectionManager,
    modifier: Modifier = Modifier
) {
    val context = LocalContext.current
    val backend = remember { AdaptiveBackend(context) }
    val telemetry by backend.telemetry.collectAsState()
    val audioState by AudioStateManager.audioState.collectAsState()
    var manualModeEnabled by remember { mutableStateOf(false) }

    // Iniciar/parar telemetría con el ciclo de vida de la pantalla
    DisposableEffect(Unit) {
        backend.startTelemetry()
        // Restaurar estado persistido
        backend.restoreState()?.let { saved ->
            AudioStateManager.updateState { saved }
        }
        onDispose {
            backend.stopTelemetry()
            if (manualModeEnabled) {
                try { IvannaNativeLib.nativeSetAdaptiveEngineEnabled(true) }
                catch (_: Throwable) { }
            }
        }
    }

    Column(
        modifier = modifier
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 16.dp, vertical = 12.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp)
    ) {
        // ── Encabezado con estado del motor ─────────────────────────────────
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                "Motor Adaptativo",
                style = MaterialTheme.typography.headlineSmall,
                color = TextPrimary,
                fontWeight = FontWeight.Bold
            )
            StatusPill(
                text = if (telemetry.motorRunning) "ACTIVO" else "INACTIVO",
                accent = if (telemetry.motorRunning) PhosphorGreen else TextMuted
            )
        }

        // ── VU / GR Meters en tiempo real ───────────────────────────────────
        GlassCard(title = "Telemetría en vivo", accent = AuroraCyan) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                StatBlock(
                    label = "RMS",
                    value = "%.1f".format(telemetry.rms),
                    accent = AuroraCyan,
                    modifier = Modifier.weight(1f)
                )
                StatBlock(
                    label = "Peak dBFS",
                    value = "%.1f".format(telemetry.peakDb),
                    accent = if (telemetry.peakDb > -3f) AmberSignal else AuroraCyan,
                    modifier = Modifier.weight(1f)
                )
                StatBlock(
                    label = "GR dB",
                    value = "%.1f".format(telemetry.grDb),
                    accent = if (abs(telemetry.grDb) > 6f) NeonMagenta else AuroraCyan,
                    modifier = Modifier.weight(1f)
                )
            }
            Spacer(Modifier.height(4.dp))
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                StatBlock(
                    label = "Comp",
                    value = "%.2f".format(telemetry.compAmount),
                    accent = AuroraCyan,
                    modifier = Modifier.weight(1f)
                )
                StatBlock(
                    label = "Espacial",
                    value = "%.2f".format(telemetry.spatialWidth),
                    accent = AuroraCyan,
                    modifier = Modifier.weight(1f)
                )
                StatBlock(
                    label = "Voz",
                    value = "%.0f%%".format(telemetry.voiceProtect * 100f),
                    accent = if (telemetry.voiceProtect > 0.5f) NeonMagenta else TextMuted,
                    modifier = Modifier.weight(1f)
                )
            }
            // Barra de GR visual
            Spacer(Modifier.height(8.dp))
            val grNorm = (abs(telemetry.grDb) / 20f).coerceIn(0f, 1f)
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(6.dp)
                    .clip(RoundedCornerShape(3.dp))
                    .background(ObsidianGlass)
            ) {
                Box(
                    modifier = Modifier
                        .fillMaxHeight()
                        .fillMaxWidth(grNorm)
                        .background(if (grNorm > 0.5f) NeonMagenta else AuroraCyan)
                )
            }
            Text(
                "Reducción de ganancia activa",
                style = MaterialTheme.typography.labelSmall,
                color = TextMuted,
                fontSize = 10.sp
            )
        }

        // ── Voice Protection ─────────────────────────────────────────────────
        val voiceActive by voiceProtectionManager.voiceProtectionActive.observeAsState(false)
        GlassCard(
            title = "Voice Protection",
            accent = NeonMagenta,
            subtitle = if (voiceActive)
                "Voces protegidas — exciter reducido automáticamente"
            else "Inactivo",
            rightSlot = {
                ToggleSwitch(
                    checked = voiceActive,
                    onCheckedChange = { voiceProtectionManager.toggle() },
                    accent = NeonMagenta
                )
            }
        ) {
            StatusPill(
                text = if (voiceActive) "PROTEGIDO" else "SIN PROTECCIÓN",
                accent = if (voiceActive) NeonMagenta else TextMuted
            )
        }

        // ── Modo y intensidad adaptativa ─────────────────────────────────────
        GlassCard(title = "Modo adaptativo", accent = AmberSignal) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                AdaptiveMode.entries.forEach { mode ->
                    val isSelected = audioState.adaptiveMode == mode
                    FlagToggle(
                        label = mode.name,
                        checked = isSelected,
                        accent = AmberSignal,
                        onCheckedChange = {
                            AudioStateManager.updateState { it.copy(adaptiveMode = mode) }
                            if (manualModeEnabled)
                                backend.applyManualState(AudioStateManager.audioState.value)
                        },
                        modifier = Modifier.weight(1f)
                    )
                }
            }
            AuroraSlider(
                label = "Intensidad",
                value = audioState.adaptiveIntensity,
                range = 0f..1f,
                displayValue = { "%.0f%%".format(it * 100f) },
                onValueChange = { v ->
                    AudioStateManager.updateState { it.copy(adaptiveIntensity = v) }
                    if (manualModeEnabled)
                        backend.applyManualState(AudioStateManager.audioState.value)
                }
            )
        }

        // ── Modo Manual ──────────────────────────────────────────────────────
        GlassCard(
            title = "Modo Manual",
            accent = PhosphorGreen,
            subtitle = if (manualModeEnabled)
                "Motor A pausado — pipeline: AudioState → Modulador → DSP"
            else
                "Motor A gobernando automáticamente",
            rightSlot = {
                ToggleSwitch(
                    checked = manualModeEnabled,
                    onCheckedChange = { enabled ->
                        manualModeEnabled = enabled
                        try {
                            IvannaNativeLib.nativeSetAdaptiveEngineEnabled(!enabled)
                        } catch (_: Throwable) { }
                        if (enabled) {
                            backend.resetModulator()
                            backend.forceManualState(audioState)
                        }
                    },
                    accent = PhosphorGreen
                )
            }
        ) {
            if (manualModeEnabled) {
                // Compresor
                AuroraSlider(
                    label = "Threshold (dB)",
                    value = audioState.compressorThreshold,
                    range = -40f..0f,
                    displayValue = { "%.0f dB".format(it) },
                    onValueChange = { v ->
                        AudioStateManager.updateState { it.copy(compressorThreshold = v) }
                        backend.applyManualState(AudioStateManager.audioState.value)
                    }
                )
                AuroraSlider(
                    label = "Ratio",
                    value = audioState.compressorRatio,
                    range = 1f..10f,
                    displayValue = { "%.1f:1".format(it) },
                    onValueChange = { v ->
                        AudioStateManager.updateState { it.copy(compressorRatio = v) }
                        backend.applyManualState(AudioStateManager.audioState.value)
                    }
                )
                AuroraSlider(
                    label = "Attack (ms)",
                    value = audioState.compressorAttack,
                    range = 1f..100f,
                    displayValue = { "%.0f ms".format(it) },
                    onValueChange = { v ->
                        AudioStateManager.updateState { it.copy(compressorAttack = v) }
                        backend.applyManualState(AudioStateManager.audioState.value)
                    }
                )
                AuroraSlider(
                    label = "Release (ms)",
                    value = audioState.compressorRelease,
                    range = 10f..500f,
                    displayValue = { "%.0f ms".format(it) },
                    onValueChange = { v ->
                        AudioStateManager.updateState { it.copy(compressorRelease = v) }
                        backend.applyManualState(AudioStateManager.audioState.value)
                    }
                )
                Spacer(Modifier.height(6.dp))
                // EQ dinámico — va vía nativeSetParams índices 8,9,10
                Text("EQ Dinámico",
                    style = MaterialTheme.typography.labelMedium,
                    color = AuroraCyan, fontWeight = FontWeight.SemiBold)
                AuroraSlider(
                    label = "Graves (dB)",
                    value = audioState.eqBass,
                    range = -12f..12f,
                    displayValue = { "%+.1f dB".format(it) },
                    onValueChange = { v ->
                        AudioStateManager.updateState { it.copy(eqBass = v) }
                        backend.applyManualState(AudioStateManager.audioState.value)
                    }
                )
                AuroraSlider(
                    label = "Medios (dB)",
                    value = audioState.eqMid,
                    range = -12f..12f,
                    displayValue = { "%+.1f dB".format(it) },
                    onValueChange = { v ->
                        AudioStateManager.updateState { it.copy(eqMid = v) }
                        backend.applyManualState(AudioStateManager.audioState.value)
                    }
                )
                AuroraSlider(
                    label = "Agudos (dB)",
                    value = audioState.eqTreble,
                    range = -12f..12f,
                    displayValue = { "%+.1f dB".format(it) },
                    onValueChange = { v ->
                        AudioStateManager.updateState { it.copy(eqTreble = v) }
                        backend.applyManualState(AudioStateManager.audioState.value)
                    }
                )
                Spacer(Modifier.height(6.dp))
                // Exciter y espacial
                AuroraSlider(
                    label = "Exciter",
                    value = audioState.exciterAmount,
                    range = 0f..1f,
                    displayValue = { "%.0f%%".format(it * 100f) },
                    onValueChange = { v ->
                        AudioStateManager.updateState { it.copy(exciterAmount = v) }
                        backend.applyManualState(AudioStateManager.audioState.value)
                    }
                )
                AuroraSlider(
                    label = "Ancho estéreo",
                    value = audioState.spatialWidth,
                    range = 0f..2f,
                    displayValue = { "%.2f".format(it) },
                    onValueChange = { v ->
                        AudioStateManager.updateState { it.copy(spatialWidth = v) }
                        backend.applyManualState(AudioStateManager.audioState.value)
                    }
                )
                AuroraSlider(
                    label = "Ganancia master",
                    value = audioState.masterGain,
                    range = 0.1f..2f,
                    displayValue = { "%.2fx".format(it) },
                    onValueChange = { v ->
                        AudioStateManager.updateState { it.copy(masterGain = v) }
                        backend.applyManualState(AudioStateManager.audioState.value)
                    }
                )
            } else {
                StatusPill(text = "AUTOMÁTICO", accent = AuroraCyan)
            }
        }
    }
}
