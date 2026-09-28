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
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.ui.theme.*

@Composable
fun OemAiScreen(state: OemState, expertMode: Boolean, onBack: () -> Unit) {
    Column(
        Modifier.fillMaxSize().background(ObsidianVoid)
            .windowInsetsPadding(WindowInsets.systemBars)
    ) {
        Row(Modifier.fillMaxWidth().background(ObsidianSoft).padding(14.dp),
            verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            IconButton(onClick = onBack, modifier = Modifier.size(32.dp)) {
                Icon(Icons.Default.ArrowBack, null, tint = NeonMagenta)
            }
            Column(Modifier.weight(1f)) {
                Text("IA ADAPTATIVA", color = NeonMagenta, fontWeight = FontWeight.ExtraBold,
                    fontSize = 12.sp, letterSpacing = 1.5.sp)
                Text("Clasificador · Decisiones · Aprendizaje", color = TextMuted, fontSize = 9.sp)
            }
            PulseDot(if (state.adaptiveRunning) NeonMagenta else TextMuted)
        }

        Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(14.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)) {

            // Classifier probabilities
            OemCard(Modifier.fillMaxWidth()) {
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically) {
                    Text("CLASIFICADOR DE CONTENIDO", color = TextMuted, fontSize = 8.sp,
                        fontWeight = FontWeight.Bold, letterSpacing = 1.sp)
                    Surface(shape = RoundedCornerShape(4.dp),
                        color = if (state.adaptiveRunning) NeonMagenta.copy(0.15f) else ObsidianEdge) {
                        Text(if (state.adaptiveRunning) "ADAPTANDO" else "FALLBACK",
                            color = if (state.adaptiveRunning) NeonMagenta else TextMuted,
                            fontSize = 8.sp, fontWeight = FontWeight.Bold,
                            modifier = Modifier.padding(horizontal = 8.dp, vertical = 3.dp))
                    }
                }
                Spacer(Modifier.height(10.dp))
                ClassBar("VOZ",    state.probVoice,   AuroraCyan,    state.dominantClass == 0)
                Spacer(Modifier.height(5.dp))
                ClassBar("MÚSICA", state.probMusic,   NeonMagenta,   state.dominantClass == 1)
                Spacer(Modifier.height(5.dp))
                ClassBar("BAJOS",  state.probBass,    AmberSignal,   state.dominantClass == 2)
                Spacer(Modifier.height(5.dp))
                ClassBar("SILENCIO",state.probSilence,TextMuted,     state.dominantClass == 3)
                Spacer(Modifier.height(8.dp))
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                    Text("Clase dominante:", color = TextMuted, fontSize = 9.sp)
                    Text(state.dominantLabel, color = NeonMagenta, fontWeight = FontWeight.Bold,
                        fontSize = 9.sp, fontFamily = FontFamily.Monospace)
                }
            }

            // Decisiones IA
            OemCard(Modifier.fillMaxWidth()) {
                Text("AJUSTES AUTOMÁTICOS ACTIVOS", color = TextMuted, fontSize = 8.sp,
                    fontWeight = FontWeight.Bold, letterSpacing = 1.sp)
                Spacer(Modifier.height(8.dp))
                TelRow("Ancho espacial IA", "${"%.2f".format(state.spatialWidth)}×", NeonMagenta)
                TelRow("Reducción excitador", "${"%.0f".format(state.exciterRed * 100)}%",
                    if (state.exciterRed > 0.5f) AmberSignal else PhosphorGreen)
                TelRow("Compresor adaptativo", "${"%.0f".format(state.compAmount * 100)}%", AuroraCyan)
                TelRow("Voz protegida", if (state.voiceProtect > 0.5f) "Sí" else "No",
                    if (state.voiceProtect > 0.5f) PhosphorGreen else TextMuted)
                TelRow("Margen de seguridad",
                    if (state.signalActive) "${"%.0f".format(state.safetyMargin * 100)}%" else "Sin señal",
                    PhosphorGreen)
            }

            // Evolutionary EQ
            if (expertMode) {
                OemCard(Modifier.fillMaxWidth()) {
                    Text("EVOLUTIONARY EQ (CMA-ES)", color = TextMuted, fontSize = 8.sp,
                        fontWeight = FontWeight.Bold, letterSpacing = 1.sp)
                    Spacer(Modifier.height(8.dp))
                    TelRow("Mejor fitness", "${"%.4f".format(state.evoBestFitness)}", AuroraCyan)
                    TelRow("Generación", "${state.evoGeneration}", NeonMagenta)
                    Text("El EQ evolutivo optimiza los coeficientes de filtro minimizando " +
                         "la desviación perceptual desde la respuesta de referencia HRTF.",
                        color = TextMuted, fontSize = 8.sp, lineHeight = 12.sp)
                }
            }

            // Learning mode info
            OemCard(Modifier.fillMaxWidth()) {
                Text("APRENDIZAJE · BIAS EMA", color = TextMuted, fontSize = 8.sp,
                    fontWeight = FontWeight.Bold, letterSpacing = 1.sp)
                Spacer(Modifier.height(6.dp))
                Text(
                    "Cuando el usuario corrige un parámetro que la IA ajustó automáticamente, " +
                    "el sistema registra la corrección en learning_bias.jsonl. La próxima vez " +
                    "que detecte el mismo contenido, incorpora el sesgo aprendido. Sin modelo " +
                    "TFLite externo: todo el aprendizaje ocurre on-device.",
                    color = TextSecondary, fontSize = 9.sp, lineHeight = 14.sp
                )
            }

            if (!state.adaptiveRunning) {
                Surface(shape = RoundedCornerShape(10.dp),
                    border = BorderStroke(1.dp, AmberSignal.copy(0.3f)),
                    color = AmberSignal.copy(0.07f), modifier = Modifier.fillMaxWidth()) {
                    Row(Modifier.padding(12.dp), horizontalArrangement = Arrangement.spacedBy(8.dp),
                        verticalAlignment = Alignment.CenterVertically) {
                        Icon(Icons.Default.Warning, null, tint = AmberSignal, modifier = Modifier.size(16.dp))
                        Text("Motor adaptativo en modo fallback. Activa el módulo Magisk " +
                             "para habilitar el pipeline completo.",
                            color = AmberSignal, fontSize = 9.sp)
                    }
                }
            }
        }
    }
}

@Composable
private fun ClassBar(label: String, value: Float, color: Color, dominant: Boolean) {
    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        Text(label, color = if (dominant) color else TextMuted, fontSize = 8.sp,
            fontWeight = if (dominant) FontWeight.Bold else FontWeight.Normal,
            modifier = Modifier.width(52.dp))
        Box(Modifier.weight(1f).height(8.dp).clip(RoundedCornerShape(4.dp)).background(ObsidianEdge)) {
            Box(Modifier.fillMaxWidth(value.coerceIn(0f, 1f)).fillMaxHeight()
                .clip(RoundedCornerShape(4.dp))
                .background(if (dominant) color else color.copy(alpha = 0.4f)))
        }
        Text("${"%.0f".format(value * 100)}%",
            color = if (dominant) color else TextMuted, fontSize = 8.sp,
            fontFamily = FontFamily.Monospace, modifier = Modifier.width(34.dp))
    }
}
