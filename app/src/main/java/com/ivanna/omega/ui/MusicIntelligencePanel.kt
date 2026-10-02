package com.ivanna.omega.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
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
import com.ivanna.omega.audio.MusicIntelligenceWorker
import java.util.Locale

@Composable
fun MusicIntelligencePanel(modifier: Modifier = Modifier) {
    LaunchedEffect(Unit) { MusicIntelligenceWorker.start() }
    val snap by MusicIntelligenceWorker.state.collectAsState()
    var autoApply by remember { mutableStateOf(MusicIntelligenceWorker.autoApply) }
    var showAdvancedLab by remember { mutableStateOf(true) }

    val cyan = Color(0xFF00E5FF)
    val emerald = Color(0xFF00E676)
    val gold = Color(0xFFFFD54F)
    val coral = Color(0xFFFF6E40)
    val bgCard = Color(0xFF0B101B)
    val bgSub = Color(0xFF131B2E)

    Card(
        modifier = modifier
            .fillMaxWidth()
            .padding(vertical = 6.dp),
        colors = CardDefaults.cardColors(containerColor = bgCard),
        shape = RoundedCornerShape(16.dp)
    ) {
        Column(modifier = Modifier.padding(14.dp)) {
            // ── Cabecera Maestra: Singularidad Acústica Atlas-Escena 12D ──
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        "ATLAS-ESCENA 12D · SINGULARIDAD ACÚSTICA",
                        color = cyan,
                        fontWeight = FontWeight.ExtraBold,
                        fontSize = 13.sp
                    )
                    Text(
                        "Inferencia Bayesiana 12 Estilos · Lebart/Habets · Woodworth ITD · Chebyshev T2/T3",
                        color = Color(0xFF90A4AE),
                        fontSize = 10.sp
                    )
                }
                Switch(
                    checked = snap.sceneEnabled,
                    onCheckedChange = {
                        MusicIntelligenceWorker.setSceneReconstructionEnabled(it)
                    }
                )
            }

            Spacer(Modifier.height(10.dp))

            // ── Estado de Identificación en Vivo + Compuerta de Confianza + Botón Re-Sync ──
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(10.dp))
                    .background(bgSub)
                    .padding(10.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    val activeLabel = if (snap.index in 0..11) {
                        MusicIntelligenceWorker.STYLE_LABELS[snap.index]
                    } else {
                        "NEUTRAL / SILENCIO"
                    }
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text(
                            text = activeLabel,
                            color = if (snap.gate > 0.05f) emerald else Color.LightGray,
                            fontWeight = FontWeight.Bold,
                            fontSize = 15.sp
                        )
                        Spacer(Modifier.width(8.dp))
                        Text(
                            text = if (snap.manualStyle >= 0) "[MANUAL]" else "[AUTO BAYES]",
                            color = if (snap.manualStyle >= 0) gold else cyan,
                            fontSize = 10.sp,
                            fontFamily = FontFamily.Monospace
                        )
                    }
                    Spacer(Modifier.height(2.dp))
                    Text(
                        text = String.format(
                            Locale.US,
                            "Conf: %.0f%% · Gate: %.0f%% · Cola Difusa: %.0f%% · Q(M10): %.2f",
                            snap.confidence * 100f,
                            snap.gate * 100f,
                            snap.lateRatio * 100f,
                            snap.qScore
                        ),
                        color = Color(0xFFB0BEC5),
                        fontSize = 11.sp,
                        fontFamily = FontFamily.Monospace
                    )
                }
                TextButton(
                    onClick = { MusicIntelligenceWorker.triggerSoftReset() },
                    contentPadding = PaddingValues(horizontal = 8.dp, vertical = 4.dp)
                ) {
                    Text("RE-IDENTIFICAR", color = cyan, fontSize = 11.sp, fontWeight = FontWeight.Bold)
                }
            }

            Spacer(Modifier.height(10.dp))

            // ── Selector de los 12 Arquetipos del Atlas (Auto Bayesiano vs Fijación Manual) ──
            Text(
                "ARQUETIPO DE ESCENA (AUTO BAYESIANO O SELECCIÓN DIRECTA)",
                color = Color(0xFF90A4AE),
                fontSize = 10.sp,
                fontWeight = FontWeight.SemiBold
            )
            Spacer(Modifier.height(6.dp))
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .horizontalScroll(rememberScrollState()),
                horizontalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                StyleChip(
                    label = "AUTO (12D)",
                    prob = snap.confidence,
                    selected = snap.manualStyle == -1,
                    accent = cyan,
                    onClick = { MusicIntelligenceWorker.setManualStyleOverride(-1) }
                )
                MusicIntelligenceWorker.STYLE_LABELS.forEachIndexed { idx, label ->
                    val p = snap.probs.getOrElse(idx) { 0f }
                    val isCurrent = (snap.manualStyle == idx) || (snap.manualStyle == -1 && snap.index == idx)
                    StyleChip(
                        label = label,
                        prob = p,
                        selected = isCurrent,
                        accent = if (snap.manualStyle == idx) gold else emerald,
                        onClick = { MusicIntelligenceWorker.setManualStyleOverride(idx) }
                    )
                }
            }

            Spacer(Modifier.height(10.dp))

            // ── Partición Espectral de 5 Bandas del Atlas + Características 12D ──
            Text(
                "PARTICIÓN ESPECTRAL COMPLEMENTARIA DEL ATLAS (5 BANDAS + VECTOR 12D)",
                color = Color(0xFF90A4AE),
                fontSize = 10.sp,
                fontWeight = FontWeight.SemiBold
            )
            Spacer(Modifier.height(6.dp))
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                BandPill("SUB", snap.subBandRatio, Color(0xFF7C4DFF), Modifier.weight(1f))
                BandPill("CUERPO", snap.bodyBandRatio, Color(0xFF00B0FF), Modifier.weight(1f))
                BandPill("DEF", snap.defBandRatio, emerald, Modifier.weight(1f))
                BandPill("PRES f6", snap.presenceRatio, gold, Modifier.weight(1f))
                BandPill("AIRE f7", snap.airRatio, coral, Modifier.weight(1f))
            }

            Spacer(Modifier.height(6.dp))
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Text(
                    String.format(
                        Locale.US,
                        "Crest: %.1fdB · Width: %.2f · Flat(f8): %.2f · Groove(f9): %.2f · LRA(f10): %.2f · S/M(f11): %.2f",
                        snap.crestDb, snap.stereoWidth, snap.flatness1m,
                        snap.onsetRegularity, snap.lraProxy12, snap.sideMid
                    ),
                    color = Color(0xFF90A4AE),
                    fontSize = 10.sp,
                    fontFamily = FontFamily.Monospace
                )
            }

            Spacer(Modifier.height(10.dp))

            // ── Objetivos Holográficos M4 e Hiper-Vectores en Vivo ──
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(8.dp))
                    .background(bgSub)
                    .padding(8.dp),
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                MetricCell("WFS Spread", String.format(Locale.US, "%.2f", snap.wfsSpread), cyan)
                MetricCell("HRTF Depth", String.format(Locale.US, "%.2f", snap.hrtfDepth), cyan)
                MetricCell("Env Depth", String.format(Locale.US, "%.2f", snap.envDepth), emerald)
                MetricCell("Warmth T2/T3", String.format(Locale.US, "%.2f", snap.warmth), gold)
                MetricCell("Elevación Z", String.format(Locale.US, "+%.2fm", snap.stageElevation), coral)
            }

            Spacer(Modifier.height(8.dp))

            // ── Guarda Cibernética de Realismo Acústico M10 ──
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(8.dp))
                    .border(
                        1.dp,
                        if (snap.guardActive) coral else Color(0xFF1E293B),
                        RoundedCornerShape(8.dp)
                    )
                    .padding(8.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                MetricCell("C_t Trans", String.format(Locale.US, "%.2f", snap.cTransient), emerald)
                MetricCell("C_s IACC", String.format(Locale.US, "%.2f", snap.cSpatial), cyan)
                MetricCell("C_d Crest", String.format(Locale.US, "%.2f", snap.cDynamic), gold)
                MetricCell("Q Global", String.format(Locale.US, "%.2f", snap.qScore), emerald)
                MetricCell(
                    "Guarda M10",
                    if (snap.guardActive) String.format(Locale.US, "ACTIVA x%.2f", snap.guardScale) else "NOMINAL 1.0",
                    if (snap.guardActive) coral else emerald
                )
            }

            Spacer(Modifier.height(8.dp))

            // ── Conmutadores A/B Instantáneos (R3 / R9) y Controles de Techo M4 ──
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    "CONMUTADORES A/B DE MOTORES FÍSICOS Y CALIBRACIÓN",
                    color = cyan,
                    fontSize = 10.sp,
                    fontWeight = FontWeight.Bold
                )
                TextButton(onClick = { showAdvancedLab = !showAdvancedLab }) {
                    Text(if (showAdvancedLab) "OCULTAR" else "MOSTRAR", color = cyan, fontSize = 10.sp)
                }
            }

            if (showAdvancedLab) {
                // Switch 1: Lebart/Habets Statistical Dereverb vs WPE Legacy
                EngineToggleRow(
                    title = "De-Reverberación Estadística (Lebart/Habets M5)",
                    subtitle = if (snap.useStatDereverb) "ACTIVO: Partición 3 polos 250 Hz + supresión de cola >50 ms"
                               else "FALLBACK: Predictor WPE Legacy",
                    checked = snap.useStatDereverb,
                    onCheckedChange = { MusicIntelligenceWorker.setUseStatDereverb(it) }
                )

                // Switch 2: Physical Early Reflections (6 Image Sources) vs Legacy Taps
                EngineToggleRow(
                    title = "Reflexiones Tempranas Físicas 3D (6 Fuentes Imagen M7)",
                    subtitle = if (snap.usePhysicalEr) "ACTIVO: Sabine-Eyring + paredes L/R/F/B + suelo/techo"
                               else "FALLBACK: EarlyReflectionCluster Legacy",
                    checked = snap.usePhysicalEr,
                    onCheckedChange = { MusicIntelligenceWorker.setUsePhysicalEr(it) }
                )

                // Switch 3: Chebyshev T2+T3 Shaper vs SoftClip Padé Legacy
                EngineToggleRow(
                    title = "Excitador Armónico Polinomial Chebyshev T2+T3 (M9)",
                    subtitle = if (snap.shaperMode == 1) "ACTIVO: Armónico par T2 (triodo) + impar T3 (cinta) + Anti-IMD"
                               else "FALLBACK: SoftClip Padé [3/2] Legacy",
                    checked = snap.shaperMode == 1,
                    onCheckedChange = { MusicIntelligenceWorker.setShaperMode(if (it) 1 else 0) }
                )

                // Switch 4: Auto-Apply WFS / Harmonic Gain
                EngineToggleRow(
                    title = "Acoplamiento Automático a WFS Spread & Ganancia Armónica",
                    subtitle = "Sincroniza la apertura de campo de ondas con el arquetipo activo",
                    checked = autoApply,
                    onCheckedChange = {
                        autoApply = it
                        MusicIntelligenceWorker.autoApply = it
                    }
                )

                Spacer(Modifier.height(6.dp))

                // Slider de Calidez Armónica (Auto por Estilo vs Manual T2/T3)
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text(
                        text = if (snap.userWarmth < 0f)
                            String.format(Locale.US, "Balance Armónico Par/Impar (Auto: %.0f%% Par T2)", snap.warmth * 100f)
                        else
                            String.format(Locale.US, "Balance Armónico Manual: %.0f%% Par T2 / %.0f%% Impar T3",
                                snap.userWarmth * 100f, (1f - snap.userWarmth) * 100f),
                        color = Color.White,
                        fontSize = 11.sp
                    )
                    if (snap.userWarmth >= 0f) {
                        TextButton(
                            onClick = { MusicIntelligenceWorker.setUserWarmthOverride(-1f) },
                            contentPadding = PaddingValues(horizontal = 6.dp, vertical = 0.dp)
                        ) {
                            Text("AUTO", color = cyan, fontSize = 10.sp)
                        }
                    }
                }
                Slider(
                    value = if (snap.userWarmth >= 0f) snap.userWarmth else snap.warmth,
                    onValueChange = { MusicIntelligenceWorker.setUserWarmthOverride(it) },
                    valueRange = 0f..1f,
                    colors = SliderDefaults.colors(thumbColor = gold, activeTrackColor = gold)
                )

                // Slider de Techo de Supresión de Cola Difusa (maxInvGain M4)
                Text(
                    String.format(Locale.US, "Techo de Limpieza de Sala (maxInvGain): %.0f%%", snap.maxInvGain * 100f),
                    color = Color.White,
                    fontSize = 11.sp
                )
                Slider(
                    value = snap.maxInvGain,
                    onValueChange = {
                        MusicIntelligenceWorker.setMaxCeilings(it, snap.maxProjWet, snap.maxExcWet)
                    },
                    valueRange = 0.10f..0.45f,
                    colors = SliderDefaults.colors(thumbColor = emerald, activeTrackColor = emerald)
                )

                // Slider de Techo de Proyección Espacial 3D (maxProjWet M4)
                Text(
                    String.format(Locale.US, "Techo de Proyección de Sala 3D (maxProjWet): %.0f%%", snap.maxProjWet * 100f),
                    color = Color.White,
                    fontSize = 11.sp
                )
                Slider(
                    value = snap.maxProjWet,
                    onValueChange = {
                        MusicIntelligenceWorker.setMaxCeilings(snap.maxInvGain, it, snap.maxExcWet)
                    },
                    valueRange = 0.08f..0.40f,
                    colors = SliderDefaults.colors(thumbColor = cyan, activeTrackColor = cyan)
                )
            }
        }
    }
}

@Composable
private fun StyleChip(
    label: String,
    prob: Float,
    selected: Boolean,
    accent: Color,
    onClick: () -> Unit
) {
    Column(
        modifier = Modifier
            .clip(RoundedCornerShape(8.dp))
            .background(if (selected) accent.copy(alpha = 0.18f) else Color(0xFF131B2E))
            .border(
                width = if (selected) 1.5.dp else 1.dp,
                color = if (selected) accent else Color(0xFF263238),
                shape = RoundedCornerShape(8.dp)
            )
            .clickable(onClick = onClick)
            .padding(horizontal = 10.dp, vertical = 6.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Text(
            text = label,
            color = if (selected) accent else Color.White,
            fontWeight = if (selected) FontWeight.Bold else FontWeight.Normal,
            fontSize = 11.sp
        )
        Text(
            text = String.format(Locale.US, "%.0f%%", prob * 100f),
            color = if (selected) accent else Color(0xFF90A4AE),
            fontSize = 9.sp,
            fontFamily = FontFamily.Monospace
        )
    }
}

@Composable
private fun BandPill(
    name: String,
    ratio: Float,
    color: Color,
    modifier: Modifier = Modifier
) {
    Column(
        modifier = modifier
            .clip(RoundedCornerShape(6.dp))
            .background(Color(0xFF131B2E))
            .padding(vertical = 5.dp, horizontal = 4.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Text(name, color = Color(0xFF90A4AE), fontSize = 9.sp, fontWeight = FontWeight.SemiBold)
        Spacer(Modifier.height(2.dp))
        Text(
            String.format(Locale.US, "%.0f%%", (ratio * 100f).coerceIn(0f, 100f)),
            color = color,
            fontSize = 11.sp,
            fontWeight = FontWeight.Bold,
            fontFamily = FontFamily.Monospace
        )
    }
}

@Composable
private fun MetricCell(label: String, value: String, color: Color) {
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        Text(label, color = Color(0xFF90A4AE), fontSize = 9.sp)
        Text(
            value,
            color = color,
            fontSize = 11.sp,
            fontWeight = FontWeight.Bold,
            fontFamily = FontFamily.Monospace
        )
    }
}

@Composable
private fun EngineToggleRow(
    title: String,
    subtitle: String,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 3.dp),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Column(modifier = Modifier.weight(1f).padding(end = 8.dp)) {
            Text(title, color = Color.White, fontSize = 11.sp, fontWeight = FontWeight.SemiBold)
            Text(subtitle, color = Color(0xFF90A4AE), fontSize = 9.sp)
        }
        Switch(checked = checked, onCheckedChange = onCheckedChange)
    }
}
