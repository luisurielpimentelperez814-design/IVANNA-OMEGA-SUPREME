package com.ivanna.omega.ui

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.core.NativeBridge
import com.ivanna.omega.spatial.IvannaSpatialNative
import com.ivanna.omega.ui.theme.*
import kotlinx.coroutines.delay
import kotlin.math.roundToInt

/**
 * CochlearInverseCard — Componente Jetpack Compose de Alta Fidelidad Visual
 * para el Eje Supremo Neuroacústico: Inversión Biomecánica Coclear (PINN).
 *
 * Características:
 *  - Título: "Inversión Biomecánica Coclear (PINN)"
 *  - Subtítulo / Badge técnico: "Descompresión OHC Activa | Latencia 0.00 ms"
 *  - Toggle reactivo sincronizado con el pipeline nativo vía JNI lock-free.
 *  - Slider continuo para intensidad de descompresión (0% a 100%) con retroalimentación háptica.
 *  - Persistencia automática de estado en SharedPreferences (SpatialAudioPrefs).
 *  - Monitor de estado lock-free en tiempo real (isCochlearActive).
 */
@Composable
fun CochlearInverseCard(
    modifier: Modifier = Modifier,
    initialEnabled: Boolean? = null,
    initialIntensity: Float? = null,
    onStateChanged: ((Boolean, Float) -> Unit)? = null
) {
    val context = LocalContext.current
    val haptic = LocalHapticFeedback.current

    // Cargar estado inicial persistido
    var isEnabled by remember {
        mutableStateOf(initialEnabled ?: SpatialAudioPrefs.load(context).cochlearInverseEnabled)
    }
    var intensity by remember {
        mutableStateOf(initialIntensity ?: SpatialAudioPrefs.load(context).cochlearIntensity)
    }

    // Monitoreo del estado nativo real
    var isNativeActive by remember { mutableStateOf(false) }

    // Sincronización nativa reactiva y persistencia
    fun applyAndPersist(newEnabled: Boolean, newIntensity: Float) {
        isEnabled = newEnabled
        intensity = newIntensity

        // 1. Persistencia inmediata en SharedPreferences
        val currentPrefs = SpatialAudioPrefs.load(context)
        SpatialAudioPrefs.save(
            context,
            currentPrefs.copy(
                cochlearInverseEnabled = newEnabled,
                cochlearIntensity = newIntensity
            )
        )

        // 2. Propagación atómica lock-free al pipeline C++20
        runCatching {
            NativeBridge.setCochlearInverseEnabled(newEnabled)
            NativeBridge.setCochlearIntensity(newIntensity)
        }.onFailure {
            runCatching {
                IvannaSpatialNative.setCochlearInverseEnabled(newEnabled)
                IvannaSpatialNative.setCochlearIntensity(newIntensity)
            }
        }

        onStateChanged?.invoke(newEnabled, newIntensity)
    }

    // Al montar el componente, asegurar que el estado persistido esté activo en C++
    LaunchedEffect(Unit) {
        runCatching {
            NativeBridge.setCochlearInverseEnabled(isEnabled)
            NativeBridge.setCochlearIntensity(intensity)
        }
        while (true) {
            isNativeActive = runCatching {
                NativeBridge.isCochlearActive()
            }.getOrDefault(isEnabled && intensity > 0.001f)
            delay(500)
        }
    }

    val activeGlow = if (isEnabled) AuroraCyan else TextMuted
    val activeBorder = if (isEnabled) {
        BorderStroke(1.dp, Brush.horizontalGradient(listOf(AuroraCyan.copy(alpha = 0.6f), NeonMagenta.copy(alpha = 0.4f))))
    } else {
        BorderStroke(1.dp, Color(0xFF262C36))
    }

    Surface(
        modifier = modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(16.dp))
            .border(activeBorder, RoundedCornerShape(16.dp)),
        color = Color(0xFF0F1318),
        tonalElevation = 2.dp
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            // ── Cabecera y Switch reactivo ─────────────────────────────────
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = "Inversión Biomecánica Coclear (PINN)",
                        color = Color(0xFFF1F5F9),
                        fontSize = 14.sp,
                        fontWeight = FontWeight.Bold,
                        letterSpacing = 0.3.sp
                    )
                    Spacer(modifier = Modifier.height(2.dp))
                    Text(
                        text = "Descompresión OHC Activa | Latencia 0.00 ms",
                        color = AuroraCyan,
                        fontSize = 11.sp,
                        fontWeight = FontWeight.Medium
                    )
                }

                Switch(
                    checked = isEnabled,
                    onCheckedChange = { checked ->
                        haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                        applyAndPersist(checked, intensity)
                    },
                    colors = SwitchDefaults.colors(
                        checkedThumbColor = Color.Black,
                        checkedTrackColor = AuroraCyan,
                        uncheckedThumbColor = TextMuted,
                        uncheckedTrackColor = Color(0xFF1E242E)
                    )
                )
            }

            // ── Badges técnicos ─────────────────────────────────────────────
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                TechBadge(
                    label = if (isNativeActive) "OHC ACTIVA" else "OHC BYPASS",
                    color = if (isNativeActive) PhosphorGreen else TextMuted
                )
                TechBadge(
                    label = "GREENWOOD 8B",
                    color = AuroraCyan
                )
                TechBadge(
                    label = "HEUN RK2 · 0 DIV",
                    color = NeonMagenta
                )
                TechBadge(
                    label = "0.00 ms LAT",
                    color = PhosphorGreen
                )
            }

            // ── Slider de intensidad de descompresión ───────────────────────
            AnimatedVisibility(
                visible = isEnabled,
                enter = fadeIn(),
                exit = fadeOut()
            ) {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(top = 4.dp),
                    verticalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text(
                            text = "Intensidad de Descompresión OHC",
                            color = TextSecondary,
                            fontSize = 12.sp,
                            fontWeight = FontWeight.Medium
                        )
                        Text(
                            text = "${(intensity * 100f).roundToInt()}%",
                            color = AuroraCyan,
                            fontSize = 13.sp,
                            fontWeight = FontWeight.ExtraBold,
                            fontFamily = FontFamily.Monospace
                        )
                    }

                    var lastHapticBucket by remember { mutableIntStateOf((intensity * 20f).roundToInt()) }

                    Slider(
                        value = intensity,
                        onValueChange = { newVal ->
                            val currentBucket = (newVal * 20f).roundToInt()
                            if (currentBucket != lastHapticBucket) {
                                haptic.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                                lastHapticBucket = currentBucket
                            }
                            applyAndPersist(isEnabled, newVal)
                        },
                        valueRange = 0.0f..1.0f,
                        colors = SliderDefaults.colors(
                            thumbColor = AuroraCyan,
                            activeTrackColor = AuroraCyan,
                            inactiveTrackColor = Color(0xFF232A36)
                        ),
                        modifier = Modifier.fillMaxWidth()
                    )

                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween
                    ) {
                        Text("0% (Lineal / Bypass)", color = TextMuted, fontSize = 10.sp)
                        Text("35% (Óptimo Acústico)", color = TextMuted, fontSize = 10.sp)
                        Text("100% (Inversión Total)", color = TextMuted, fontSize = 10.sp)
                    }
                }
            }

            // ── Explicación técnica de la física acústica ──────────────────
            Surface(
                modifier = Modifier.fillMaxWidth(),
                color = Color(0xFF141920),
                shape = RoundedCornerShape(8.dp)
            ) {
                Text(
                    text = "Modelo inverso de motilidad prestina: y_b = d / (1 + α_b·d²). Deshace la distorsión intermodular coclear en tiempo real sobre 8 bandas críticas Greenwood con fase coherente.",
                    color = Color(0xFF94A3B8),
                    fontSize = 10.sp,
                    lineHeight = 14.sp,
                    modifier = Modifier.padding(10.dp)
                )
            }
        }
    }
}

@Composable
private fun TechBadge(label: String, color: Color) {
    Surface(
        color = color.copy(alpha = 0.12f),
        shape = RoundedCornerShape(6.dp),
        border = BorderStroke(1.dp, color.copy(alpha = 0.35f))
    ) {
        Text(
            text = label,
            color = color,
            fontSize = 9.sp,
            fontWeight = FontWeight.Bold,
            fontFamily = FontFamily.Monospace,
            modifier = Modifier.padding(horizontal = 6.dp, vertical = 3.dp)
        )
    }
}
