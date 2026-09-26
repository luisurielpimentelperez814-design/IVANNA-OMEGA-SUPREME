package com.ivanna.omega.ui

import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.ivanna.omega.ui.theme.*
import com.ivanna.omega.ui.viewmodels.CochlearInverseViewModel

/**
 * CochlearInverseCard — UI de alta fidelidad para el Eje Supremo Neuroacústico.
 *
 * Conecta el pipeline C++20 [CochlearActiveInverseEngine] con la capa Compose
 * a través de [CochlearInverseViewModel] → [IvannaNativeLib] (JNI lock-free).
 *
 * Flujo de datos completo:
 *   Switch → vm.setCochlearEnabled(b) → nativeSetCochlearInverseEnabled(b)
 *     → g_cochlearEnabled (atomic<bool>) → CochlearActiveInverseEngine::process()
 *
 *   Slider → vm.setCochlearIntensity(w) → nativeSetCochlearIntensity(w)
 *     → g_cochlearIntensity (atomic<float>) → engine.setIntensity(w) por bloque
 *
 * Persistencia: SharedPreferences "ivanna_cochlear_prefs_v1"
 *   — sobrevive reinicios del servicio/daemon y del proceso.
 */
@Composable
fun CochlearInverseCard(
    modifier: Modifier = Modifier,
    initialEnabled: Boolean? = null,
    initialIntensity: Float? = null,
    onStateChanged: ((Boolean, Float) -> Unit)? = null,
    vm: CochlearInverseViewModel = viewModel(factory = CochlearInverseViewModel.Factory)
) {
    val enabled    by vm.cochlearEnabled.collectAsState()
    val intensity  by vm.cochlearIntensity.collectAsState()
    val haptic     = LocalHapticFeedback.current

    LaunchedEffect(initialEnabled) {
        if (initialEnabled != null && initialEnabled != enabled) {
            vm.setCochlearEnabled(initialEnabled)
        }
    }

    LaunchedEffect(initialIntensity) {
        if (initialIntensity != null && kotlin.math.abs(initialIntensity - intensity) > 0.001f) {
            vm.setCochlearIntensity(initialIntensity)
        }
    }

    // Color de acento animado: cian cuando activo, magenta tenue cuando inactivo
    val accentColor by animateColorAsState(
        targetValue = if (enabled) AuroraCyan else NeonMagenta.copy(alpha = 0.55f),
        animationSpec = tween(durationMillis = 350),
        label = "cochlearAccent"
    )

    Box(modifier = modifier) {
    GlassCard(
        title    = "INVERSIÓN BIOMECÁNICA COCLEAR (PINN)",
        accent   = accentColor,
        subtitle = "Descompresión OHC Activa  ·  Latencia 0.00 ms",
        rightSlot = {
            // Switch reactivo con feedback háptico al cambiar estado
            Switch(
                checked          = enabled,
                onCheckedChange  = { on ->
                    haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                    vm.setCochlearEnabled(on)
                    onStateChanged?.invoke(on, intensity)
                },
                colors = SwitchDefaults.colors(
                    checkedThumbColor        = AuroraCyan,
                    checkedTrackColor        = AuroraCyanGlow,
                    uncheckedThumbColor      = TextMuted,
                    uncheckedTrackColor      = ObsidianEdge
                ),
                modifier = Modifier.height(28.dp)
            )
        }
    ) {
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {

            // ── Badge técnico / estado ─────────────────────────────────────────
            BadgeRow(enabled = enabled, intensity = intensity)

            HorizontalDivider(
                color     = AuroraCyanGlow.copy(alpha = 0.20f),
                thickness = 0.5.dp
            )

            // ── Slider de intensidad de descompresión ──────────────────────────
            CochlearIntensitySlider(
                enabled        = enabled,
                intensity      = intensity,
                accentColor    = accentColor,
                onValueChange  = { w ->
                    vm.setCochlearIntensity(w)
                    onStateChanged?.invoke(enabled, w)
                    // Feedback háptico cada 10 % de recorrido del slider
                    val snapped = (w * 10).toInt()
                    val prevSnapped = (intensity * 10).toInt()
                    if (snapped != prevSnapped) {
                        haptic.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                    }
                }
            )

            // ── Info técnica compacta ─────────────────────────────────────────
            TechInfoRow(intensity = intensity)
        }
    }
    }
}

// ── Sub-componentes internos ────────────────────────────────────────────────

@Composable
private fun BadgeRow(enabled: Boolean, intensity: Float) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        // Indicador de estado
        val statusText  = if (enabled) "ACTIVO" else "BYPASS"
        val statusColor = if (enabled) PhosphorGreen else TextMuted
        Text(
            text     = statusText,
            color    = statusColor,
            fontSize = 9.sp,
            fontWeight = FontWeight.Bold,
            letterSpacing = 1.2.sp,
            modifier = Modifier
                .clip(RoundedCornerShape(4.dp))
                .background(statusColor.copy(alpha = 0.14f))
                .padding(horizontal = 6.dp, vertical = 2.dp)
        )
        // Indicador de intensidad
        if (enabled) {
            Text(
                text     = "%.0f%%  OHC".format(intensity * 100f),
                color    = AuroraCyan,
                fontSize = 10.sp,
                fontFamily = FontFamily.Monospace
            )
        }
        Spacer(modifier = Modifier.weight(1f))
        // Banda críticas (informativo: 8 bandas Greenwood 120 Hz – 16 kHz)
        Text(
            text     = "8 BANDAS GREENWOOD",
            color    = TextMuted,
            fontSize = 9.sp,
            letterSpacing = 0.8.sp
        )
    }
}

@Composable
private fun CochlearIntensitySlider(
    enabled       : Boolean,
    intensity     : Float,
    accentColor   : androidx.compose.ui.graphics.Color,
    onValueChange : (Float) -> Unit
) {
    Column(modifier = Modifier.fillMaxWidth()) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                text       = "INTENSIDAD DE DESCOMPRESIÓN",
                color      = TextSecondary,
                fontSize   = 10.sp,
                fontWeight = FontWeight.Medium,
                letterSpacing = 0.5.sp
            )
            Text(
                text       = "%.0f %%".format(intensity * 100f),
                color      = accentColor,
                fontSize   = 12.sp,
                fontWeight = FontWeight.Bold,
                fontFamily = FontFamily.Monospace
            )
        }
        Spacer(modifier = Modifier.height(2.dp))
        // Glow track detrás del slider
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .clip(RoundedCornerShape(6.dp))
                .background(
                    Brush.horizontalGradient(
                        listOf(
                            accentColor.copy(alpha = if (enabled) 0.12f else 0.04f),
                            accentColor.copy(alpha = 0.01f)
                        )
                    )
                )
        ) {
            Slider(
                value         = intensity,
                onValueChange = { if (enabled) onValueChange(it) },
                enabled       = enabled,
                valueRange    = 0f..1f,
                colors = SliderDefaults.colors(
                    thumbColor          = accentColor,
                    activeTrackColor    = accentColor,
                    inactiveTrackColor  = ObsidianEdge,
                    disabledThumbColor  = TextMuted,
                    disabledActiveTrackColor = ObsidianEdge,
                    disabledInactiveTrackColor = ObsidianEdge
                ),
                modifier = Modifier.fillMaxWidth()
            )
        }
        // Escala 0 % / 50 % / 100 %
        Row(
            modifier = Modifier.fillMaxWidth().padding(horizontal = 4.dp),
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            listOf("0 %", "50 %", "100 %").forEach { label ->
                Text(label, color = TextMuted, fontSize = 8.sp)
            }
        }
    }
}

@Composable
private fun TechInfoRow(intensity: Float) {
    // Alfa de prestina por banda media (banda 4 de 8 = ~2560 Hz)
    val alphaMid = 0.22f + 0.10f * kotlin.math.sin(kotlin.math.PI.toFloat() * 4.5f / 8f)
    val effectiveAlpha = alphaMid * intensity

    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        TechChip(label = "MODELO",    value = "Heun RK2 · NEON")
        TechChip(label = "α OHC",     value = "%.3f".format(effectiveAlpha))
        TechChip(label = "LATENCIA",  value = "0.00 ms")
    }
}

@Composable
private fun RowScope.TechChip(label: String, value: String) {
    Column(
        modifier = Modifier
            .weight(1f)
            .clip(RoundedCornerShape(6.dp))
            .background(ObsidianVoid.copy(alpha = 0.55f))
            .padding(horizontal = 6.dp, vertical = 4.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Text(label, color = TextMuted, fontSize = 8.sp, letterSpacing = 0.8.sp)
        Text(value, color = AuroraCyanDim, fontSize = 10.sp, fontFamily = FontFamily.Monospace)
    }
}
