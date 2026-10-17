package com.ivanna.omega.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.core.Ivanna4DNative
import kotlinx.coroutines.delay
import org.json.JSONObject
import java.util.Locale

private data class FourDState(
    val enabled: Boolean = false,
    val armed: Boolean = false,
    val anticipation: Float = 0f,
    val confidence: Float = 0f,
    val periodMs: Float = 0f,
    val leadMs: Float = 7.5f,
    val hapticTotal: Float = 0f,
    val hapticAmp: Int = 0,
    val dose: Float = 0f,
    val laeq: Float = 0f,
    val attenDb: Float = 0f,
    val yaw: Float = 0f,
    val itdUs: Float = 0f,
    val scenes: Int = 0,
    val arcPhase: Int = 0,
    val arcArousal: Float = 0f,
    val arcTension: Float = 0f,
    val arcImpact: Float = 0f,
)

/**
 * FourDPredictionPanel — control y telemetría de la Cadena 4D.
 *
 * El "deslizador de predicción 4D" que cierra el encargo: anticipación del campo sonoro
 * (5–10 ms antes del golpe), háptica multibanda, dosímetro de exposición y cancelación de
 * diafonía, todo desde el dispositivo.
 */
@Composable
fun FourDPredictionPanel(modifier: Modifier = Modifier) {
    var st by remember { mutableStateOf(FourDState()) }
    var haptics by remember { mutableStateOf(false) }
    var crosstalk by remember { mutableStateOf(false) }
    var leadMs by remember { mutableStateOf(7.5f) }
    var depth by remember { mutableStateOf(0.7f) }
    var arc by remember { mutableStateOf(false) }
    var arcDepth by remember { mutableStateOf(0.5f) }
    var hapticScale by remember { mutableStateOf(0.6f) }
    var dosMode by remember { mutableStateOf(0) }

    val cyan = Color(0xFF00E5FF)
    val magenta = Color(0xFFFF3D9A)
    val emerald = Color(0xFF00E676)
    val gold = Color(0xFFFFD54F)
    val bgCard = Color(0xFF0B101B)
    val bgSub = Color(0xFF131B2E)

    LaunchedEffect(Unit) {
        while (true) {
            if (Ivanna4DNative.isAvailable) {
                val json = runCatching { Ivanna4DNative.nativeFourDStatusJson() }.getOrNull()
                val o = json?.let { runCatching { JSONObject(it) }.getOrNull() }
                if (o != null) {
                    st = FourDState(
                        enabled = o.optBoolean("enabled", false),
                        armed = o.optBoolean("armed", false),
                        anticipation = o.optDouble("anticipation", 0.0).toFloat(),
                        confidence = o.optDouble("confidence", 0.0).toFloat(),
                        periodMs = o.optDouble("periodMs", 0.0).toFloat(),
                        leadMs = o.optDouble("leadMs", 7.5).toFloat(),
                        hapticTotal = o.optDouble("hapticTotal", 0.0).toFloat(),
                        hapticAmp = o.optInt("hapticAmp", 0),
                        dose = o.optDouble("dose", 0.0).toFloat(),
                        laeq = o.optDouble("laeq", 0.0).toFloat(),
                        attenDb = o.optDouble("attenDb", 0.0).toFloat(),
                        yaw = o.optDouble("yaw", 0.0).toFloat(),
                        itdUs = o.optDouble("itdUs", 0.0).toFloat(),
                        scenes = o.optInt("scenes", 0),
                        arcPhase = o.optInt("arcPhase", 0),
                        arcArousal = o.optDouble("arcArousal", 0.0).toFloat(),
                        arcTension = o.optDouble("arcTension", 0.0).toFloat(),
                        arcImpact = o.optDouble("arcImpact", 0.0).toFloat(),
                    )
                }
            }
            delay(400)
        }
    }

    Card(
        modifier = modifier.fillMaxWidth().padding(vertical = 6.dp),
        colors = CardDefaults.cardColors(containerColor = bgCard),
        shape = RoundedCornerShape(16.dp)
    ) {
        Column(modifier = Modifier.padding(14.dp)) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        "PREDICCIÓN 4D · EXCITACIÓN ANTICIPADA",
                        color = magenta, fontWeight = FontWeight.ExtraBold, fontSize = 13.sp
                    )
                    Text(
                        "El campo se abre 5–10 ms ANTES del golpe · háptica multibanda · dosímetro",
                        color = Color(0xFF90A4AE), fontSize = 10.sp
                    )
                }
                Switch(
                    checked = st.enabled,
                    onCheckedChange = { on ->
                        if (Ivanna4DNative.isAvailable) Ivanna4DNative.nativeFourDSetEnabled(on)
                    }
                )
            }

            Spacer(Modifier.height(10.dp))

            Row(
                modifier = Modifier.fillMaxWidth().background(bgSub, RoundedCornerShape(10.dp)).padding(10.dp),
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Text(
                    if (st.armed) "ARMADO" else "ESCUCHANDO",
                    color = if (st.armed) emerald else Color.LightGray,
                    fontWeight = FontWeight.Bold, fontSize = 14.sp
                )
                Text(
                    String.format(
                        Locale.US,
                        "Anticip %.0f%% · Conf %.0f%% · Periodo %.0f ms · Lead %.1f ms",
                        st.anticipation * 100f, st.confidence * 100f, st.periodMs, st.leadMs
                    ),
                    color = Color(0xFFB0BEC5), fontSize = 11.sp, fontFamily = FontFamily.Monospace
                )
            }

            Spacer(Modifier.height(8.dp))

            Text(
                String.format(
                    Locale.US,
                    "Háptica %.3f (amp %d/255) · Dosis %.3f%% · LAeq %.1f dB(A) · Atenuación reco. %.1f dB",
                    st.hapticTotal, st.hapticAmp, st.dose * 100f, st.laeq, st.attenDb
                ),
                color = gold, fontSize = 11.sp, fontFamily = FontFamily.Monospace
            )
            Text(
                String.format(
                    Locale.US,
                    "Guiñada %.1f° · ITD %.1f µs · Escenas publicadas %d",
                    st.yaw, st.itdUs, st.scenes
                ),
                color = cyan, fontSize = 11.sp, fontFamily = FontFamily.Monospace
            )

            Spacer(Modifier.height(10.dp))
            HorizontalDivider(color = Color(0xFF1E2A44))
            Spacer(Modifier.height(10.dp))

            // Anticipación (5–10 ms)
            Text("Antelación de apertura del campo (lead)", color = Color(0xFFB0BEC5), fontSize = 11.sp)
            Slider(
                value = leadMs, valueRange = 5f..10f,
                onValueChange = {
                    leadMs = it
                    if (Ivanna4DNative.isAvailable) Ivanna4DNative.nativeFourDSetLeadMs(it)
                }
            )

            // Profundidad
            Text("Profundidad de modulación", color = Color(0xFFB0BEC5), fontSize = 11.sp)
            Slider(
                value = depth, valueRange = 0f..1f,
                onValueChange = {
                    depth = it
                    if (Ivanna4DNative.isAvailable) Ivanna4DNative.nativeFourDSetDepth(it)
                }
            )

            // Arco narrativo (escala de segundos): subida, hueco de tensión e impacto del drop.
            Row(verticalAlignment = Alignment.CenterVertically) {
                Switch(checked = arc, onCheckedChange = {
                    arc = it
                    if (Ivanna4DNative.isAvailable) {
                        // La profundidad nativa arranca en 0: se envía junto con el interruptor.
                        Ivanna4DNative.nativeFourDSetArcDepth(arcDepth)
                        Ivanna4DNative.nativeFourDSetArc(it)
                    }
                })
                Spacer(Modifier.width(8.dp))
                Column {
                    Text("Arco narrativo (subida · hueco · impacto)", color = Color(0xFFB0BEC5), fontSize = 12.sp)
                    Text(
                        String.format(
                            Locale.US,
                            "%s · excitación %.0f%% · tensión %.0f%% · impacto %.0f%%",
                            when (st.arcPhase) { 1 -> "SUBIDA"; 2 -> "CIMA"; 3 -> "RELAJACIÓN"; else -> "CALMA" },
                            st.arcArousal * 100f, st.arcTension * 100f, st.arcImpact * 100f
                        ),
                        color = cyan, fontSize = 10.sp, fontFamily = FontFamily.Monospace
                    )
                }
            }
            Text("Profundidad del arco", color = Color(0xFFB0BEC5), fontSize = 11.sp)
            Slider(
                value = arcDepth, valueRange = 0f..1f,
                onValueChange = {
                    arcDepth = it
                    if (Ivanna4DNative.isAvailable) Ivanna4DNative.nativeFourDSetArcDepth(it)
                }
            )

            Row(verticalAlignment = Alignment.CenterVertically) {
                Switch(checked = haptics, onCheckedChange = {
                    haptics = it
                    if (Ivanna4DNative.isAvailable) Ivanna4DNative.nativeFourDSetHaptics(it)
                })
                Spacer(Modifier.width(8.dp))
                Text("Háptica multibanda predictiva", color = Color(0xFFB0BEC5), fontSize = 12.sp)
            }
            Text("Escala háptica", color = Color(0xFFB0BEC5), fontSize = 11.sp)
            Slider(
                value = hapticScale, valueRange = 0f..0.85f,
                onValueChange = {
                    hapticScale = it
                    if (Ivanna4DNative.isAvailable) Ivanna4DNative.nativeFourDSetHapticScale(it)
                }
            )

            Row(verticalAlignment = Alignment.CenterVertically) {
                Switch(checked = crosstalk, onCheckedChange = {
                    crosstalk = it
                    if (Ivanna4DNative.isAvailable) Ivanna4DNative.nativeFourDSetCrosstalk(it)
                })
                Spacer(Modifier.width(8.dp))
                Text("Cancelación de diafonía (por altavoces)", color = Color(0xFFB0BEC5), fontSize = 12.sp)
            }

            Spacer(Modifier.height(6.dp))
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("Criterio dosímetro", color = Color(0xFFB0BEC5), fontSize = 11.sp)
                Spacer(Modifier.width(10.dp))
                listOf("WHO", "NIOSH", "OSHA").forEachIndexed { idx, label ->
                    TextButton(onClick = {
                        dosMode = idx
                        if (Ivanna4DNative.isAvailable) Ivanna4DNative.nativeFourDSetDosimeterMode(idx)
                    }) {
                        Text(
                            label,
                            color = if (dosMode == idx) gold else Color.Gray,
                            fontWeight = if (dosMode == idx) FontWeight.Bold else FontWeight.Normal,
                            fontSize = 11.sp
                        )
                    }
                }
            }
        }
    }
}
