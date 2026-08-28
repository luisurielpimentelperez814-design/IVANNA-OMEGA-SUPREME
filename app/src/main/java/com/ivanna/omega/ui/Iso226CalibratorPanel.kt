package com.ivanna.omega.ui

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.audio.Iso226Calibrator
import com.ivanna.omega.audio.IvannaGlobalEffectManager
import com.ivanna.omega.core.IVANNAApplication
import com.ivanna.omega.ui.theme.AuroraCyan
import com.ivanna.omega.ui.theme.ObsidianVoid
import com.ivanna.omega.ui.theme.ObsidianSoft
import com.ivanna.omega.ui.theme.TextMuted
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch

// Frecuencias de las 10 bandas EQ (etiquetas para la curva)
private val BAND_LABELS = listOf("31", "63", "125", "250", "500", "1k", "2k", "4k", "8k", "12.5k")

/**
 * Iso226CalibratorPanel — Calibrador perceptual ISO 226:2003 con UI completa.
 *
 * FIX (auditoría 2026-08-12): Iso226Calibrator.applyAll() existía y estaba
 * correctamente implementado (3 capas: Equalizer nativo, DSPBridge, socket daemon)
 * pero NINGÚN composable lo llamaba. No había slider de Phon, no había botón
 * de aplicar, no había visualización de la curva de compensación resultante.
 *
 * Este panel cierra el hueco:
 *   - Slider listenPhon [40, 85] Phon — nivel real de escucha del usuario
 *   - Slider refPhon   [70, 90] Phon — nivel de referencia de masterización
 *   - Preview de la curva de compensación (10 bandas) en tiempo real
 *   - Botón APLICAR → Iso226Calibrator.applyAll() → EQ + DSPBridge + OmegaBridge
 *   - Estado de cada capa (checkmarks de aplicación)
 *
 * Se puede incrustar en SoundScreen o usarse como pantalla standalone.
 */
@Composable
fun Iso226CalibratorPanel(
    modifier: Modifier = Modifier
) {
    val context = LocalContext.current
    val scope   = rememberCoroutineScope()

    // Estado local de los sliders — inicia desde la última calibración aplicada.
    // load() restaura las prefs ANTES del remember (se ejecuta primero en la
    // composición): sin esto los sliders volvían a 85/80 Phon en cada arranque
    // aunque persist() sí guardaba al aplicar.
    // restoreIfSaved() ya existe en Iso226Calibrator: lee listen_phon/ref_phon
    // de SharedPreferences y re-aplica la curva si estaba calibrada. Se llama
    // ANTES de los remember{} para que los sliders abran con el valor real.
    // effectManager puede ser null en previews — el restore es best-effort.
    remember {
        runCatching {
            val em = (context.applicationContext as? com.ivanna.omega.core.IVANNAApplication)?.globalEffectManager
            if (em != null) Iso226Calibrator.restoreIfSaved(context, em)
        }
        true
    }
    var listenPhon by remember { mutableFloatStateOf(Iso226Calibrator.listenPhon) }
    var refPhon    by remember { mutableFloatStateOf(Iso226Calibrator.refPhon)    }

    // Curva de compensación preview (recalculada en cada cambio de slider)
    val gains = remember(listenPhon, refPhon) {
        Iso226Calibrator.computeCompensation(listenPhon, refPhon)
    }

    // Estado de la última aplicación
    var lastResult by remember { mutableStateOf<Iso226Calibrator.CalibrationResult?>(null) }
    var applying   by remember { mutableStateOf(false) }

    val isCalibrated = Iso226Calibrator.isCalibrated

    // Colores del tema (realineado a Aurora Obsidiana — antes grises planos
    // 0D0D0F/111114/888899 que rompían la coherencia con las demás pantallas)
    val panelBg    = ObsidianVoid
    val cardBg     = ObsidianSoft
    val borderCol  = AuroraCyan.copy(alpha = 0.18f)
    val textMuted  = TextMuted

    Column(
        modifier = modifier
            .fillMaxWidth()
            .background(panelBg, RoundedCornerShape(16.dp))
            .border(1.dp, borderCol, RoundedCornerShape(16.dp))
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp)
    ) {
        // ── Header ─────────────────────────────────────────────────────────────
        Row(
            Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column {
                Text(
                    "ISO 226:2003  EQUAL-LOUDNESS",
                    color = AuroraCyan,
                    fontSize = 12.sp,
                    fontWeight = FontWeight.Bold,
                    letterSpacing = 1.5.sp,
                    fontFamily = FontFamily.Monospace
                )
                Text(
                    "Compensación perceptual · 29 frecuencias · 3 capas",
                    color = textMuted,
                    fontSize = 9.sp,
                    fontFamily = FontFamily.Monospace
                )
            }
            if (isCalibrated) {
                Text(
                    "✓",
                    color = AuroraCyan,
                    fontSize = 18.sp,
                    fontWeight = FontWeight.Bold
                )
            }
        }

        // ── Slider: listenPhon ─────────────────────────────────────────────────
        Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                Text("Nivel de escucha", color = Color.White, fontSize = 12.sp)
                Text(
                    "${listenPhon.toInt()} Phon",
                    color = AuroraCyan,
                    fontSize = 12.sp,
                    fontWeight = FontWeight.Bold,
                    fontFamily = FontFamily.Monospace
                )
            }
            Slider(
                value = listenPhon,
                onValueChange = { listenPhon = it },
                valueRange = 40f..85f,
                steps = 44,
                colors = SliderDefaults.colors(
                    thumbColor = AuroraCyan,
                    activeTrackColor = AuroraCyan,
                    inactiveTrackColor = AuroraCyan.copy(alpha = 0.2f)
                )
            )
            Text(
                "Volumen real en tus audífonos al escuchar",
                color = textMuted, fontSize = 9.sp, fontFamily = FontFamily.Monospace
            )
        }

        // ── Slider: refPhon ────────────────────────────────────────────────────
        Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                Text("Nivel de referencia", color = Color.White, fontSize = 12.sp)
                Text(
                    "${refPhon.toInt()} Phon",
                    color = AuroraCyan.copy(alpha = 0.75f),
                    fontSize = 12.sp,
                    fontWeight = FontWeight.Bold,
                    fontFamily = FontFamily.Monospace
                )
            }
            Slider(
                value = refPhon,
                onValueChange = { refPhon = it },
                valueRange = 70f..90f,
                steps = 19,
                colors = SliderDefaults.colors(
                    thumbColor = AuroraCyan.copy(alpha = 0.75f),
                    activeTrackColor = AuroraCyan.copy(alpha = 0.75f),
                    inactiveTrackColor = AuroraCyan.copy(alpha = 0.15f)
                )
            )
            Text(
                "Nivel de masterización (80 Phon = estudio, 85 Phon = broadcast)",
                color = textMuted, fontSize = 9.sp, fontFamily = FontFamily.Monospace
            )
        }

        // ── Preview de la curva de compensación (10 bandas) ───────────────────
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .background(cardBg, RoundedCornerShape(10.dp))
                .border(1.dp, borderCol, RoundedCornerShape(10.dp))
                .padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            Text(
                "CURVA DE COMPENSACIÓN",
                color = textMuted, fontSize = 9.sp,
                letterSpacing = 1.sp, fontFamily = FontFamily.Monospace
            )
            Row(
                Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(4.dp),
                verticalAlignment = Alignment.Bottom
            ) {
                val maxAbsGain = gains.maxOf { kotlin.math.abs(it) }.coerceAtLeast(0.1f)
                gains.forEachIndexed { i, g ->
                    val norm = (g / 12f).coerceIn(-1f, 1f)
                    val barH by animateFloatAsState(
                        targetValue = kotlin.math.abs(norm),
                        animationSpec = tween(180),
                        label = "bar$i"
                    )
                    val barColor = if (g >= 0)
                        Brush.verticalGradient(listOf(AuroraCyan, AuroraCyan.copy(alpha = 0.4f)))
                    else
                        Brush.verticalGradient(listOf(Color(0xFFFF6B6B), Color(0xFFFF6B6B).copy(alpha = 0.4f)))

                    Column(
                        modifier = Modifier.weight(1f),
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.Bottom
                    ) {
                        Box(
                            modifier = Modifier
                                .fillMaxWidth()
                                .height((barH * 40f).dp)
                                .background(barColor, RoundedCornerShape(2.dp))
                        )
                        Spacer(Modifier.height(2.dp))
                        Text(
                            BAND_LABELS[i],
                            fontSize = 6.sp, color = textMuted,
                            fontFamily = FontFamily.Monospace
                        )
                        Text(
                            "%+.1f".format(g),
                            fontSize = 6.sp,
                            color = if (g >= 0) AuroraCyan else Color(0xFFFF6B6B),
                            fontFamily = FontFamily.Monospace
                        )
                    }
                }
            }
        }

        // ── Botón APLICAR ──────────────────────────────────────────────────────
        val effectManager = remember {
            (context.applicationContext as? IVANNAApplication)?.globalEffectManager
        }

        Button(
            onClick = {
                if (!applying && effectManager != null) {
                    applying = true
                    scope.launch(Dispatchers.IO) {
                        val result = Iso226Calibrator.applyAll(listenPhon, refPhon, effectManager)
                        Iso226Calibrator.persist(context)
                        lastResult = result
                        applying = false
                    }
                }
            },
            enabled = !applying && effectManager != null,
            modifier = Modifier.fillMaxWidth(),
            shape = RoundedCornerShape(10.dp),
            colors = ButtonDefaults.buttonColors(
                containerColor = AuroraCyan,
                contentColor = Color.Black,
                disabledContainerColor = AuroraCyan.copy(alpha = 0.3f)
            )
        ) {
            if (applying) {
                CircularProgressIndicator(
                    modifier = Modifier.size(16.dp),
                    color = Color.Black,
                    strokeWidth = 2.dp
                )
                Spacer(Modifier.width(8.dp))
            }
            Text(
                if (applying) "APLICANDO…" else "APLICAR ISO 226",
                fontWeight = FontWeight.Bold,
                fontSize = 13.sp,
                letterSpacing = 1.sp
            )
        }

        // ── Estado de la última aplicación ─────────────────────────────────────
        lastResult?.let { r ->
            Row(
                Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                listOf(
                    "EQ" to r.eqApplied,
                    "DSP" to r.dspApplied,
                    "DAEMON" to r.socketApplied
                ).forEach { (label, ok) ->
                    Row(
                        modifier = Modifier
                            .weight(1f)
                            .background(
                                if (ok) AuroraCyan.copy(alpha = 0.08f)
                                else ObsidianSoft,
                                RoundedCornerShape(6.dp)
                            )
                            .padding(vertical = 6.dp, horizontal = 8.dp),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.Center
                    ) {
                        Text(
                            if (ok) "✓" else "○",
                            color = if (ok) AuroraCyan else textMuted,
                            fontSize = 12.sp,
                            fontWeight = FontWeight.Bold
                        )
                        Spacer(Modifier.width(4.dp))
                        Text(
                            label,
                            color = if (ok) AuroraCyan else textMuted,
                            fontSize = 9.sp,
                            fontFamily = FontFamily.Monospace,
                            letterSpacing = 0.5.sp
                        )
                    }
                }
            }
        }
    }
}
