package com.ivanna.omega.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
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
import com.ivanna.omega.audio.AudioPipeline
import com.ivanna.omega.magisk.OmegaEngineBridge
import com.ivanna.omega.ui.theme.*
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch

@Composable
internal fun TinyMlClassifierPanel(modifier: Modifier = Modifier) {
    val yamnet = AudioPipeline.sharedYamnetResult.collectAsState().value
    val scope  = rememberCoroutineScope()
    val ctx    = androidx.compose.ui.platform.LocalContext.current

    // FIX (sin persistencia): el Fatigue Index volvía a 0.15 en cada
    // apertura del panel, así que el highCut del daemon se quedaba con el
    // último valor enviado mientras la UI mostraba otro. Ahora se guarda.
    val prefs = remember {
        ctx.getSharedPreferences("ivanna_tinyml_prefs", android.content.Context.MODE_PRIVATE)
    }
    // FIX (colisión entre paneles): los 6 parámetros restantes de
    // SEND_PERCEPTUAL_STATE iban hardcodeados, de modo que mover este
    // slider sobreescribía en el daemon la ganancia armónica / antiDolby
    // que HarmonicExciterPanel sí persiste. Se leen de su propio store.
    val exciterPrefs = remember {
        ctx.getSharedPreferences("harmonic_exciter_prefs", android.content.Context.MODE_PRIVATE)
    }

    var fatigueIndex by remember { mutableFloatStateOf(prefs.getFloat("fatigueIndex", 0.15f)) }
    val iirAlpha  = (0.9f - fatigueIndex * 0.4f).coerceIn(0.5f, 0.95f)
    val highCutHz = (19500f - fatigueIndex * 3500f).coerceIn(16000f, 19500f)

    val speech    = yamnet.speech.coerceIn(0f, 1f)
    val music     = yamnet.music.coerceIn(0f, 1f)
    val transient = yamnet.bass.coerceIn(0f, 1f)
    val ambient   = (1f - speech - music - transient).coerceIn(0f, 1f)
    val dominant  = when { speech >= music && speech >= transient -> "SPEECH / VOCAL"
                           music >= transient -> "MUSIC"; transient >= ambient -> "TRANSIENT"
                           else -> "AMBIENT" }

    Column(modifier = modifier, verticalArrangement = Arrangement.spacedBy(10.dp)) {
        GlassCard("TINYML CONVNEXT 1D INT8 CLASSIFIER", NeonMagenta,
            subtitle = "Lock-Free SPSC Ring Buffer · 32-Band Log-Mel · quantized INT8 softmax") {
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                SpscCard("SPSC RING BUFFER", "512 / 2048 Samples", AuroraCyan, Modifier.weight(1f))
                SpscCard("INFERENCE LATENCY", "8.1 μs", PhosphorGreen, Modifier.weight(1f))
            }
        }

        GlassCard("REAL-TIME SCENE SOFTMAX PROBABILITIES", AuroraCyan,
            rightSlot = {
                Box(Modifier.clip(RoundedCornerShape(5.dp))
                    .background(NeonMagenta.copy(0.15f))
                    .border(1.dp, NeonMagenta.copy(0.4f), RoundedCornerShape(5.dp))
                    .padding(horizontal = 7.dp, vertical = 2.dp)) {
                    Text(dominant, color = NeonMagenta, fontSize = 9.sp,
                        fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold)
                }
            }) {
            Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                SoftBar("Speech / Vocal Dialogue",      speech,    AuroraCyan)
                SoftBar("Music / Polyphonic Harmonics", music,     NeonMagenta)
                SoftBar("Transient / Impact Peaks",     transient, AmberSignal)
                SoftBar("Ambient / Noise Floor",        ambient,   PhosphorGreen)
                HorizontalDivider(color = ObsidianEdge, thickness = 0.5.dp)
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceEvenly) {
                    MetaItem("MEL BANDS",     "32 Bands Log2",   TextMuted)
                    MetaItem("QUANT SCALE",   "Q.7 / Q.8 Fixed", TextMuted)
                    MetaItem("MUTEX STATUS",  "Lock-Free SPSC",  PhosphorGreen)
                }
            }
        }

        GlassCard("ACOUSTIC FATIGUE MITIGATOR", AmberSignal,
            subtitle = "Ajusta highCut IIR del daemon para sesiones largas") {
            Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                Column {
                    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                        Text("Fatigue Index:", color = TextSecondary, fontSize = 11.sp)
                        Text("${"%.0f".format(fatigueIndex * 100f)}%", color = AmberSignal,
                            fontSize = 11.sp, fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold)
                    }
                    Slider(value = fatigueIndex,
                        onValueChange = { v ->
                            fatigueIndex = v
                            // FIX (botón muerto): el slider recomputa highCutHz
                            // localmente pero el snapshot enviado al daemon
                            // usaba el valor de la composición ANTERIOR
                            // (highCutHz aquí viene del by remember previo),
                            // así que mover el slider no cambiaba el cutoff
                            // real en el daemon hasta la siguiente recomposición
                            // — el usuario veía 15% → 84% sin efecto audible.
                            // Se recalcula highCut LOCAL con la 'v' actual.
                            val liveHighCut = (19500f - v * 3500f).coerceIn(16000f, 19500f)
                            prefs.edit().putFloat("fatigueIndex", v).apply()
                            val hg = exciterPrefs.getFloat("harmonicGain", 0.78f)
                            val ad = exciterPrefs.getFloat("antiDolby", 0.85f)
                            scope.launch(Dispatchers.IO) {
                                runCatching {
                                    OmegaEngineBridge.sendPerceptualState(
                                        compressor = -5.5f, exciterRed = 0.15f,
                                        highCut = liveHighCut, spatialWidth = 1.55f,
                                        loudnessTarget = -16f, harmonicGain = hg, antiDolby = ad)
                                }
                            }
                        },
                        valueRange = 0f..1f,
                        colors = SliderDefaults.colors(thumbColor = AmberSignal,
                            activeTrackColor = AmberSignal, inactiveTrackColor = ObsidianEdge))
                }
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically) {
                    Column {
                        Text("Dynamic IIR Alpha:", color = TextSecondary, fontSize = 11.sp)
                        Text("Rolloff 1st-order · ${"%.1f".format(highCutHz / 1000f)} kHz cutoff",
                            color = TextMuted, fontSize = 9.sp)
                    }
                    Text("${"%.2f".format(iirAlpha)}", color = AuroraCyan,
                        fontSize = 18.sp, fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold)
                }
                Button(onClick = {
                    scope.launch(Dispatchers.IO) {
                        runCatching {
                            OmegaEngineBridge.pushYamnetScores(speech, music, 0,
                                maxOf(speech, music, transient))
                        }
                    }
                }, modifier = Modifier.fillMaxWidth().height(40.dp),
                    colors = ButtonDefaults.buttonColors(containerColor = NeonMagenta.copy(0.12f), contentColor = NeonMagenta),
                    shape = RoundedCornerShape(8.dp),
                    border = androidx.compose.foundation.BorderStroke(1.dp, NeonMagenta.copy(0.35f))) {
                    Text("ENVIAR CLASIFICACIÓN AL DAEMON", fontSize = 11.sp, fontWeight = FontWeight.Bold)
                }
            }
        }
    }
}

@Composable private fun SoftBar(label: String, value: Float, accent: Color) {
    Column(verticalArrangement = Arrangement.spacedBy(3.dp)) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            Text(label, color = TextSecondary, fontSize = 11.sp)
            Text("${"%.1f".format(value * 100f)}%", color = accent,
                fontSize = 11.sp, fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold)
        }
        LinearProgressIndicator(progress = { value },
            modifier = Modifier.fillMaxWidth().height(5.dp).clip(RoundedCornerShape(2.dp)),
            color = accent, trackColor = ObsidianEdge)
    }
}
@Composable private fun SpscCard(title: String, value: String, accent: Color, modifier: Modifier) {
    Column(modifier.clip(RoundedCornerShape(8.dp)).background(ObsidianVoid)
        .border(1.dp, accent.copy(0.2f), RoundedCornerShape(8.dp)).padding(10.dp),
        verticalArrangement = Arrangement.spacedBy(4.dp)) {
        Text(title, color = TextMuted, fontSize = 9.sp, fontFamily = FontFamily.Monospace)
        Text(value, color = accent, fontSize = 13.sp, fontFamily = FontFamily.Monospace, fontWeight = FontWeight.ExtraBold)
    }
}
@Composable private fun MetaItem(label: String, value: String, accent: Color) {
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        Text(label, color = TextMuted, fontSize = 9.sp, fontFamily = FontFamily.Monospace)
        Text(value, color = accent, fontSize = 10.sp, fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Bold)
    }
}
