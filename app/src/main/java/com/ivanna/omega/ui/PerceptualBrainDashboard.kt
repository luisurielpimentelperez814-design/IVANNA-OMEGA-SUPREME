package com.ivanna.omega.ui

import androidx.compose.animation.core.*
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.ai.PerceptualBrainEngine
import com.ivanna.omega.ai.PerceptualSnapshot

private val Carbon = Color(0xFF0A0A0A)
private val Surface1 = Color(0xFF121212)
private val Surface2 = Color(0xFF1A1A1A)
private val Border1 = Color(0xFF262626)
private val CyanGlow = Color(0xFF00F5FF)
private val CyanDim = Color(0x3300F5FF)
private val GoldGlow = Color(0xFFFFD700)
private val MagentaGlow = Color(0xFFFF00FF)
private val EmeraldGlow = Color(0xFF00FF88)
private val TextPri = Color(0xFFFFFFFF)
private val TextSec = Color(0xFF888888)
private val TextMid = Color(0xFFCCCCCC)

@Composable
fun PerceptualBrainDashboard(
    engine: PerceptualBrainEngine,
    onBack: () -> Unit,
    modifier: Modifier = Modifier
) {
    val snapshot by engine.snapshot.collectAsState()
    val scrollState = rememberScrollState()

    Column(
        modifier = modifier
            .fillMaxSize()
            .background(Carbon)
            .padding(horizontal = 16.dp)
            .verticalScroll(scrollState)
    ) {
        Spacer(Modifier.height(16.dp))

        // --- Header Bar ---
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column {
                Text(
                    text = "PERCEPTUAL BRAIN CORTEX v3.0",
                    color = TextPri,
                    fontSize = 14.sp,
                    fontWeight = FontWeight.ExtraBold,
                    letterSpacing = 1.2.sp
                )
                Text(
                    text = "IVANNA OMEGA SUPREME · NEURO-ACOUSTIC BRAIN",
                    color = CyanGlow,
                    fontSize = 9.sp,
                    letterSpacing = 1.sp
                )
            }
            OutlinedButton(
                onClick = onBack,
                colors = ButtonDefaults.outlinedButtonColors(contentColor = GoldGlow),
                border = BorderStroke(1.dp, GoldGlow),
                shape = RoundedCornerShape(8.dp),
                contentPadding = PaddingValues(horizontal = 12.dp, vertical = 4.dp)
            ) {
                Text("CERRAR", fontSize = 10.sp, fontWeight = FontWeight.Bold)
            }
        }

        Spacer(Modifier.height(16.dp))

        // --- Section 1: Perceptual Brain Status ---
        BrainStatusCard(snapshot)

        Spacer(Modifier.height(12.dp))

        // --- Section 2: Human Auditory Perception Panel ---
        HumanAuditoryPanel(snapshot)

        Spacer(Modifier.height(12.dp))

        // --- Section 3: TinyML STFT Classifier Cortex ---
        TinyMlClassifierPanel(snapshot)

        Spacer(Modifier.height(12.dp))

        // --- Section 4: DSP Cortex & Spatial Field ---
        DspCortexPanel(snapshot)

        Spacer(Modifier.height(12.dp))

        // --- Section 5: Perceptual Intelligence Sliders ---
        PerceptualSlidersCard(snapshot = snapshot, engine = engine)

        Spacer(Modifier.height(24.dp))
    }
}

@Composable
private fun BrainStatusCard(snapshot: PerceptualSnapshot) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(12.dp),
        colors = CardDefaults.cardColors(containerColor = Surface1),
        border = BorderStroke(1.dp, Border1)
    ) {
        Column(modifier = Modifier.padding(14.dp)) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "ESTADO DEL CEREBRO PERCEPTUAL",
                    color = GoldGlow,
                    fontSize = 11.sp,
                    fontWeight = FontWeight.ExtraBold,
                    letterSpacing = 1.sp
                )
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Box(
                        modifier = Modifier
                            .size(8.dp)
                            .clip(CircleShape)
                            .background(if (snapshot.perceptionOnline) EmeraldGlow else Color.Red)
                    )
                    Spacer(Modifier.width(6.dp))
                    Text(
                        text = if (snapshot.perceptionOnline) "ENGINE ONLINE" else "OFFLINE",
                        color = if (snapshot.perceptionOnline) EmeraldGlow else Color.Red,
                        fontSize = 9.sp,
                        fontWeight = FontWeight.Bold
                    )
                }
            }

            Spacer(Modifier.height(12.dp))

            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                MetricProgressBlock(
                    label = "CONFIDENCE",
                    value = snapshot.confidence,
                    color = CyanGlow,
                    modifier = Modifier.weight(1f)
                )
                Spacer(Modifier.width(8.dp))
                MetricProgressBlock(
                    label = "IMMERSION",
                    value = snapshot.immersion,
                    color = MagentaGlow,
                    modifier = Modifier.weight(1f)
                )
                Spacer(Modifier.width(8.dp))
                MetricProgressBlock(
                    label = "ATTENTION",
                    value = snapshot.attention,
                    color = GoldGlow,
                    modifier = Modifier.weight(1f)
                )
            }

            Spacer(Modifier.height(8.dp))

            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                MetricProgressBlock(
                    label = "FATIGUE INDEX",
                    value = snapshot.fatigue,
                    color = if (snapshot.fatigue > 0.4f) Color(0xFFFF5555) else EmeraldGlow,
                    modifier = Modifier.weight(1f)
                )
                Spacer(Modifier.width(8.dp))
                MetricProgressBlock(
                    label = "EMOTION EST.",
                    value = snapshot.emotion,
                    color = CyanGlow,
                    modifier = Modifier.weight(1f)
                )
            }
        }
    }
}

@Composable
private fun HumanAuditoryPanel(snapshot: PerceptualSnapshot) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(12.dp),
        colors = CardDefaults.cardColors(containerColor = Surface1),
        border = BorderStroke(1.dp, Border1)
    ) {
        Column(modifier = Modifier.padding(14.dp)) {
            Text(
                text = "PANEL HUMANO AUDITIVO (PSYCHOACOUSTICS)",
                color = CyanGlow,
                fontSize = 11.sp,
                fontWeight = FontWeight.ExtraBold,
                letterSpacing = 1.sp
            )
            Spacer(Modifier.height(10.dp))

            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                TextValueItem(label = "ISO 226 Loudness", value = "%.1f dBSPL".format(snapshot.iso226LoudnessDb))
                TextValueItem(label = "Bark Bands", value = "${snapshot.barkBandsCount} Bands")
                TextValueItem(label = "Mel Bands", value = "${snapshot.melBandsCount} Bands")
            }

            Spacer(Modifier.height(8.dp))

            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                TextValueItem(label = "Masking Model", value = "%.0f%% Eff".format(snapshot.maskingEfficiency * 100f))
                TextValueItem(label = "Temporal Masking", value = "%.1f ms".format(snapshot.temporalMaskingMs))
                TextValueItem(label = "Spectral Balance", value = "%.0f%% Ratio".format(snapshot.spectralBalanceRatio * 100f))
            }

            Spacer(Modifier.height(8.dp))

            TextValueItem(
                label = "Dynamic Range Perception",
                value = "%.1f dB".format(snapshot.dynamicRangeDb),
                color = GoldGlow
            )
        }
    }
}

@Composable
private fun TinyMlClassifierPanel(snapshot: PerceptualSnapshot) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(12.dp),
        colors = CardDefaults.cardColors(containerColor = Surface1),
        border = BorderStroke(1.dp, Border1)
    ) {
        Column(modifier = Modifier.padding(14.dp)) {
            Text(
                text = "PANEL TINYML (CONVNEXT INT8 CLASSIFIER)",
                color = MagentaGlow,
                fontSize = 11.sp,
                fontWeight = FontWeight.ExtraBold,
                letterSpacing = 1.sp
            )
            Spacer(Modifier.height(10.dp))

            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                TextValueItem(label = "STFT Feature Extractor", value = "64-Band Log-Mel")
                TextValueItem(label = "Model Core", value = "ConvNeXt INT8")
            }

            Spacer(Modifier.height(8.dp))

            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                TextValueItem(label = "Inference Latency", value = "${snapshot.convNextLatencyUs} µs", color = EmeraldGlow)
                TextValueItem(label = "SPSC Ring Buffer", value = "%.0f%% Occ".format(snapshot.ringBufferOccupancy * 100f))
            }

            Spacer(Modifier.height(8.dp))

            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                TextValueItem(label = "Dominant Class", value = snapshot.dominantClassLabel, color = TextPri)
                TextValueItem(label = "Class Confidence", value = "%.1f%%".format(snapshot.convNextConfidence * 100f), color = GoldGlow)
            }
        }
    }
}

@Composable
private fun DspCortexPanel(snapshot: PerceptualSnapshot) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(12.dp),
        colors = CardDefaults.cardColors(containerColor = Surface1),
        border = BorderStroke(1.dp, Border1)
    ) {
        Column(modifier = Modifier.padding(14.dp)) {
            Text(
                text = "PANEL DSP CORTEX & FIELD SPATIAL",
                color = GoldGlow,
                fontSize = 11.sp,
                fontWeight = FontWeight.ExtraBold,
                letterSpacing = 1.sp
            )
            Spacer(Modifier.height(10.dp))

            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                TextValueItem(label = "HRTF Status", value = snapshot.hrtfStatus)
                TextValueItem(label = "Phase Coherence", value = "%.0f%%".format(snapshot.phaseCoherence * 100f))
            }

            Spacer(Modifier.height(8.dp))

            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                TextValueItem(label = "Volterra Harmonic", value = "%.1f%% H2".format(snapshot.volterraH2Ratio * 100f))
                TextValueItem(label = "NPE Engine", value = if (snapshot.npeStateActive) "ACTIVE" else "BYPASS", color = EmeraldGlow)
            }

            Spacer(Modifier.height(8.dp))

            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                TextValueItem(label = "Safety Limiter", value = "%.1f dB Margin".format(snapshot.safetyLimiterMarginDb))
                TextValueItem(label = "Adaptive Engine", value = snapshot.adaptiveEngineState, color = CyanGlow)
            }
        }
    }
}

@Composable
private fun PerceptualSlidersCard(snapshot: PerceptualSnapshot, engine: PerceptualBrainEngine) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(12.dp),
        colors = CardDefaults.cardColors(containerColor = Surface1),
        border = BorderStroke(1.dp, Border1)
    ) {
        Column(modifier = Modifier.padding(14.dp)) {
            Text(
                text = "CONTROLES DE INTELIGENCIA PERCEPTUAL",
                color = TextPri,
                fontSize = 11.sp,
                fontWeight = FontWeight.ExtraBold,
                letterSpacing = 1.sp
            )
            Spacer(Modifier.height(12.dp))

            PerceptualSliderRow(
                label = "Perceptual Intelligence",
                value = snapshot.perceptualIntelligence,
                color = CyanGlow,
                onValueChange = { engine.setPerceptualIntelligence(it) }
            )

            Spacer(Modifier.height(10.dp))

            PerceptualSliderRow(
                label = "Neural Adaptation",
                value = snapshot.neuralAdaptation,
                color = EmeraldGlow,
                onValueChange = { engine.setNeuralAdaptation(it) }
            )

            Spacer(Modifier.height(10.dp))

            PerceptualSliderRow(
                label = "Spatial Immersion",
                value = snapshot.spatialImmersion,
                color = MagentaGlow,
                onValueChange = { engine.setSpatialImmersion(it) }
            )

            Spacer(Modifier.height(10.dp))

            PerceptualSliderRow(
                label = "Harmonic Reconstruction",
                value = snapshot.harmonicReconstruction,
                color = GoldGlow,
                onValueChange = { engine.setHarmonicReconstruction(it) }
            )

            Spacer(Modifier.height(10.dp))

            PerceptualSliderRow(
                label = "Anti-Dolby Blend",
                value = snapshot.antiDolbyBlend,
                color = CyanGlow,
                onValueChange = { engine.setAntiDolbyBlend(it) }
            )

            Spacer(Modifier.height(10.dp))

            PerceptualSliderRow(
                label = "Human Loudness Comp.",
                value = snapshot.humanLoudnessCompensation,
                color = EmeraldGlow,
                onValueChange = { engine.setHumanLoudnessCompensation(it) }
            )
        }
    }
}

@Composable
private fun MetricProgressBlock(
    label: String,
    value: Float,
    color: Color,
    modifier: Modifier = Modifier
) {
    Column(
        modifier = modifier
            .background(Surface2, RoundedCornerShape(8.dp))
            .padding(8.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Text(label, color = TextSec, fontSize = 8.sp, fontWeight = FontWeight.Bold)
            Text("%.0f%%".format(value * 100f), color = color, fontSize = 8.sp, fontWeight = FontWeight.ExtraBold)
        }
        Spacer(Modifier.height(4.dp))
        LinearProgressIndicator(
            progress = { value.coerceIn(0f, 1f) },
            modifier = Modifier
                .fillMaxWidth()
                .height(4.dp)
                .clip(RoundedCornerShape(2.dp)),
            color = color,
            trackColor = Color(0xFF222222)
        )
    }
}

@Composable
private fun TextValueItem(label: String, value: String, color: Color = TextPri) {
    Column {
        Text(label, color = TextSec, fontSize = 8.sp)
        Text(value, color = color, fontSize = 10.sp, fontWeight = FontWeight.Bold)
    }
}

@Composable
private fun PerceptualSliderRow(
    label: String,
    value: Float,
    color: Color,
    onValueChange: (Float) -> Unit
) {
    Column(modifier = Modifier.fillMaxWidth()) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(label, color = TextMid, fontSize = 10.sp, fontWeight = FontWeight.SemiBold)
            Text("%.0f%%".format(value * 100f), color = color, fontSize = 10.sp, fontWeight = FontWeight.ExtraBold)
        }
        Slider(
            value = value,
            onValueChange = onValueChange,
            colors = SliderDefaults.colors(
                thumbColor = color,
                activeTrackColor = color,
                inactiveTrackColor = Color(0xFF2B2B2B)
            ),
            modifier = Modifier.height(24.dp)
        )
    }
}
