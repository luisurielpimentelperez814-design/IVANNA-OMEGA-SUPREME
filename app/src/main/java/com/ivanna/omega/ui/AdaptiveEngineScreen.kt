package com.ivanna.omega.ui

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.runtime.livedata.observeAsState
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.ivanna.omega.audio.AudioStateManager
import com.ivanna.omega.audio.DspStateUpdater
import com.ivanna.omega.audio.VoiceProtectionManager
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.ui.theme.*

/**
 * AdaptiveEngineScreen — conecta a la UI la arquitectura MAGISTRAL
 * (AudioStateManager / DspStateUpdater / VoiceProtectionManager, commit
 * bb4fa6b) que llegó al repo compilando pero sin ningún punto de entrada.
 *
 * COORDINACIÓN MOTOR A vs MODO MANUAL:
 * AdaptiveDecisionEngine (Motor A) escribe compressor/exciter/ancho
 * automáticamente cada 50ms. DspStateUpdater escribe LOS MISMOS params.
 * El toggle "Modo Manual" pausa Motor A vía nativeSetAdaptiveEngineEnabled(false)
 * mientras esta pantalla tiene el control, y lo reanuda al salir.
 * DisposableEffect garantiza que Motor A nunca quede pausado si se sale
 * de la pantalla sin desactivar el modo manual.
 */
@Composable
internal fun AdaptiveEngineScreen(
    voiceProtectionManager: VoiceProtectionManager,
    modifier: Modifier = Modifier
) {
    val dspStateUpdater = remember { DspStateUpdater() }
    val audioState by AudioStateManager.audioState.collectAsState()
    var manualModeEnabled by remember { mutableStateOf(false) }

    DisposableEffect(Unit) {
        onDispose {
            if (manualModeEnabled) {
                try {
                    IvannaNativeLib.nativeSetAdaptiveEngineEnabled(true)
                } catch (_: Throwable) { }
            }
        }
    }

    Column(
        modifier = modifier
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 16.dp, vertical = 12.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp)
    ) {
        Text(
            "Motor Adaptativo",
            style = MaterialTheme.typography.headlineSmall,
            color = TextPrimary,
            fontWeight = FontWeight.Bold
        )

        // ── Voice Protection ─────────────────────────────────────────────
        val voiceActive by voiceProtectionManager.voiceProtectionActive.observeAsState(false)

        GlassCard(
            title = "Voice Protection",
            accent = NeonMagenta,
            subtitle = if (voiceActive)
                "Activo — voces protegidas de la compresión adaptativa"
            else
                "Inactivo",
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

        // ── Modo Manual ──────────────────────────────────────────────────
        GlassCard(
            title = "Modo Manual",
            accent = PhosphorGreen,
            subtitle = if (manualModeEnabled)
                "Motor A pausado — controles manuales activos"
            else
                "Motor A gobernando automáticamente (recomendado)",
            rightSlot = {
                ToggleSwitch(
                    checked = manualModeEnabled,
                    onCheckedChange = { enabled ->
                        manualModeEnabled = enabled
                        try {
                            IvannaNativeLib.nativeSetAdaptiveEngineEnabled(!enabled)
                        } catch (_: Throwable) { }
                        if (enabled) {
                            dspStateUpdater.forceUpdate(audioState)
                        }
                    },
                    accent = PhosphorGreen
                )
            }
        ) {
            if (manualModeEnabled) {
                AuroraSlider(
                    label = "Compresor (ratio)",
                    value = audioState.compressorRatio,
                    range = 1f..10f,
                    displayValue = { "%.1f:1".format(it) },
                    onValueChange = { v ->
                        AudioStateManager.updateState { it.copy(compressorRatio = v) }
                        dspStateUpdater.requestUpdate(AudioStateManager.audioState.value)
                    }
                )
                Spacer(Modifier.height(4.dp))
                AuroraSlider(
                    label = "Exciter",
                    value = audioState.exciterAmount,
                    range = 0f..1f,
                    onValueChange = { v ->
                        AudioStateManager.updateState { it.copy(exciterAmount = v) }
                        dspStateUpdater.requestUpdate(AudioStateManager.audioState.value)
                    }
                )
                Spacer(Modifier.height(4.dp))
                AuroraSlider(
                    label = "Ancho estéreo",
                    value = audioState.spatialWidth,
                    range = 0f..2f,
                    onValueChange = { v ->
                        AudioStateManager.updateState { it.copy(spatialWidth = v) }
                        dspStateUpdater.requestUpdate(AudioStateManager.audioState.value)
                    }
                )
            } else {
                StatusPill(text = "AUTOMÁTICO", accent = AuroraCyan)
            }
        }
    }
}
