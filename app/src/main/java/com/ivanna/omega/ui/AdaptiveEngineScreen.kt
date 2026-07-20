package com.ivanna.omega.ui

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.ivanna.omega.audio.AudioStateManager
import com.ivanna.omega.audio.DspStateUpdater
import com.ivanna.omega.audio.VoiceProtectionManager
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.ui.theme.*

/**
 * AdaptiveEngineScreen — conecta a la UI la arquitectura "MAGISTRAL"
 * (AudioStateManager / DspStateUpdater / VoiceProtectionManager, commit
 * bb4fa6b) que llegó al repo compilando pero sin ningún punto de entrada
 * desde ninguna pantalla.
 *
 * DECISIÓN DE DISEÑO (seguridad de audio en tiempo real):
 * AdaptiveDecisionEngine (Motor A) ya escribe compressor/exciter/ancho
 * automáticamente cada 50ms en el hilo de audio real. DspStateUpdater
 * escribe A LOS MISMOS parámetros nativos. Sin coordinación, esta
 * pantalla pelearía con Motor A (mismo patrón que colisionaba con
 * "Motor B", ya reparado en df68877). Por eso el toggle "Modo Manual"
 * llama nativeSetAdaptiveEngineEnabled(false) al activarse — pausa el
 * hilo de control de Motor A mientras esta pantalla tiene el control — y
 * nativeSetAdaptiveEngineEnabled(true) al desactivarse o al salir de la
 * pantalla (DisposableEffect), para que Motor A siempre quede
 * gobernando salvo que el usuario pida explícitamente lo contrario.
 *
 * Voice Protection es independiente (booleano ortogonal, no toca
 * compressor/exciter/ancho) y se conecta sin ninguna de estas
 * precauciones.
 */
@Composable
internal fun AdaptiveEngineScreen(
    voiceProtectionManager: VoiceProtectionManager,
    modifier: Modifier = Modifier
) {
    val dspStateUpdater = remember { DspStateUpdater() }
    val audioState by AudioStateManager.audioState.collectAsState()
    var manualModeEnabled by remember { mutableStateOf(false) }

    // Al salir de la pantalla por cualquier vía (back, cierre), garantizar
    // que Motor A quede reanudado — nunca dejar la app en modo manual
    // "fantasma" sin que nadie lo sepa.
    DisposableEffect(Unit) {
        onDispose {
            if (manualModeEnabled) {
                try {
                    IvannaNativeLib.nativeSetAdaptiveEngineEnabled(true)
                } catch (_: Throwable) {
                    // Motor A puede no estar inicializado todavía (p.ej. sin
                    // reproducción activa) — no es un error, sólo no hay
                    // nada que reanudar.
                }
            }
        }
    }

    Column(
        modifier = modifier
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp)
    ) {
        Text(
            "Motor Adaptativo",
            style = MaterialTheme.typography.headlineSmall,
            color = TextPrimary,
            fontWeight = FontWeight.Bold
        )

        // ── Voice Protection ────────────────────────────────────────────
        val voiceActive by voiceProtectionManager.voiceProtectionActive.observeAsState(false)
        GlassCard(
            title = "Voice Protection",
            accent = NeonMagenta,
            subtitle = if (voiceActive) "Activo — voces protegidas de la compresión adaptativa"
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

        // ── Modo Manual (pausa Motor A mientras está activo) ────────────
        GlassCard(
            title = "Modo Manual",
            accent = PhosphorGreen,
            subtitle = if (manualModeEnabled)
                "Motor automático (Motor A) pausado — controlas tú"
            else
                "Motor A gobierna automáticamente (recomendado)",
            rightSlot = {
                ToggleSwitch(
                    checked = manualModeEnabled,
                    onCheckedChange = { enabled ->
                        manualModeEnabled = enabled
                        try {
                            IvannaNativeLib.nativeSetAdaptiveEngineEnabled(!enabled)
                        } catch (_: Throwable) { /* motor no inicializado aún */ }
                        if (enabled) {
                            // Al entrar a manual, envía el estado actual una
                            // vez para que el DSP no se quede en lo último
                            // que dejó Motor A sin confirmar el nuevo dueño.
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
                AuroraSlider(
                    label = "Exciter",
                    value = audioState.exciterAmount,
                    range = 0f..1f,
                    onValueChange = { v ->
                        AudioStateManager.updateState { it.copy(exciterAmount = v) }
                        dspStateUpdater.requestUpdate(AudioStateManager.audioState.value)
                    }
                )
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
