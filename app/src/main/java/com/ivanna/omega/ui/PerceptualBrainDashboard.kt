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
import com.ivanna.omega.ui.theme.*
import com.ivanna.omega.ai.PerceptualSnapshot


@Composable
fun PerceptualBrainDashboard(
    engine: PerceptualBrainEngine,
    onBack: () -> Unit,
    modifier: Modifier = Modifier
) {
    DisposableEffect(engine) {
        engine.start()
        onDispose {
            engine.stop()
        }
    }

    val snapshot by engine.snapshot.collectAsState()
    val scrollState = rememberScrollState()

    Column(
        modifier = modifier
            .fillMaxSize()
            .background(ObsidianVoid)
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
                    text = "PERCEPTUAL BRAIN CORTEX v4.0",
                    color = TextPrimary,
                    fontSize = 14.sp,
                    fontWeight = FontWeight.ExtraBold,
                    letterSpacing = 1.2.sp
                )
                Text(
                    text = "IVANNA OMEGA SUPREME · NEURO-ACOUSTIC BRAIN",
                    color = AuroraCyan,
                    fontSize = 9.sp,
                    letterSpacing = 1.sp
                )
            }
            OutlinedButton(
                onClick = onBack,
                colors = ButtonDefaults.outlinedButtonColors(contentColor = AmberSignal),
                border = BorderStroke(1.dp, AmberSignal),
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
        colors = CardDefaults.cardColors(containerColor = ObsidianSoft),
        border = BorderStroke(1.dp, ObsidianEdge)
    ) {
        Column(modifier = Modifier.padding(14.dp)) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "ESTADO DEL CEREBRO PERCEPTUAL",
                    color = AmberSignal,
                    fontSize = 11.sp,
                    fontWeight = FontWeight.ExtraBold,
                    letterSpacing = 1.sp
                )
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Box(
                        modifier = Modifier
                            .size(8.dp)
                            .clip(CircleShape)
                            .background(if (snapshot.perceptionOnline) PhosphorGreen else Color.Red)
                    )
                    Spacer(Modifier.width(6.dp))
                    Text(
                        text = if (snapshot.perceptionOnline) "ENGINE ONLINE" else "OFFLINE",
                        color = if (snapshot.perceptionOnline) PhosphorGreen else Color.Red,
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
                    color = AuroraCyan,
                    modifier = Modifier.weight(1f)
                )
                Spacer(Modifier.width(8.dp))
                MetricProgressBlock(
                    label = "IMMERSION",
                    value = snapshot.immersion,
                    color = NeonMagenta,
                    modifier = Modifier.weight(1f)
                )
                Spacer(Modifier.width(8.dp))
                MetricProgressBlock(
                    label = "ATTENTION",
                    value = snapshot.attention,
                    color = AmberSignal,
                    modifier = Modifier.weight(1f)
                )
            }

            Spacer(Modifier.height(8.dp))

            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                MetricProgressBlock(
                    label = "FATIGUE INDEX",
                    value = snapshot.fatigue,
                    color = if (snapshot.fatigue > 0.4f) Color(0xFFFF5555) else PhosphorGreen,
                    modifier = Modifier.weight(1f)
                )
                Spacer(Modifier.width(8.dp))
                MetricProgressBlock(
                    label = "EMOTION EST.",
                    value = snapshot.emotion,
                    color = AuroraCyan,
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
        colors = CardDefaults.cardColors(containerColor = ObsidianSoft),
        border = BorderStroke(1.dp, ObsidianEdge)
    ) {
        Column(modifier = Modifier.padding(14.dp)) {
            Text(
                text = "PANEL HUMANO AUDITIVO (PSYCHOACOUSTICS)",
                color = AuroraCyan,
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
                color = AmberSignal
            )
        }
    }
}

@Composable
private fun TinyMlClassifierPanel(snapshot: PerceptualSnapshot) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(12.dp),
        colors = CardDefaults.cardColors(containerColor = ObsidianSoft),
        border = BorderStroke(1.dp, ObsidianEdge)
    ) {
        Column(modifier = Modifier.padding(14.dp)) {
            Text(
                text = "PANEL TINYML (CONVNEXT INT8 CLASSIFIER)",
                color = NeonMagenta,
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
                TextValueItem(label = "Inference Latency", value = "${snapshot.convNextLatencyUs} µs", color = PhosphorGreen)
                TextValueItem(label = "SPSC Ring Buffer", value = "%.0f%% Occ".format(snapshot.ringBufferOccupancy * 100f))
            }

            Spacer(Modifier.height(8.dp))

            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                TextValueItem(label = "Dominant Class", value = snapshot.dominantClassLabel, color = TextPrimary)
                TextValueItem(label = "Class Confidence", value = "%.1f%%".format(snapshot.convNextConfidence * 100f), color = AmberSignal)
            }
        }
    }
}

@Composable
private fun DspCortexPanel(snapshot: PerceptualSnapshot) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(12.dp),
        colors = CardDefaults.cardColors(containerColor = ObsidianSoft),
        border = BorderStroke(1.dp, ObsidianEdge)
    ) {
        Column(modifier = Modifier.padding(14.dp)) {
            Text(
                text = "PANEL DSP CORTEX & FIELD SPATIAL",
                color = AmberSignal,
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
                TextValueItem(label = "NPE Engine", value = if (snapshot.npeStateActive) "ACTIVE" else "BYPASS", color = PhosphorGreen)
            }

            Spacer(Modifier.height(8.dp))

            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                TextValueItem(label = "Safety Limiter", value = "%.1f dB Margin".format(snapshot.safetyLimiterMarginDb))
                TextValueItem(label = "Adaptive Engine", value = snapshot.adaptiveEngineState, color = AuroraCyan)
            }
        }
    }
}

@Composable
private fun PerceptualSlidersCard(snapshot: PerceptualSnapshot, engine: PerceptualBrainEngine) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(12.dp),
        colors = CardDefaults.cardColors(containerColor = ObsidianSoft),
        border = BorderStroke(1.dp, ObsidianEdge)
    ) {
        Column(modifier = Modifier.padding(14.dp)) {
            Text(
                text = "CONTROLES DE INTELIGENCIA PERCEPTUAL",
                color = TextPrimary,
                fontSize = 11.sp,
                fontWeight = FontWeight.ExtraBold,
                letterSpacing = 1.sp
            )
            Spacer(Modifier.height(12.dp))

            PerceptualSliderRow(
                label = "Perceptual Intelligence",
                value = snapshot.perceptualIntelligence,
                color = AuroraCyan,
                onValueChange = { engine.setPerceptualIntelligence(it) }
            )

            Spacer(Modifier.height(10.dp))

            PerceptualSliderRow(
                label = "Neural Adaptation",
                value = snapshot.neuralAdaptation,
                color = PhosphorGreen,
                onValueChange = { engine.setNeuralAdaptation(it) }
            )

            Spacer(Modifier.height(10.dp))

            PerceptualSliderRow(
                label = "Spatial Immersion",
                value = snapshot.spatialImmersion,
                color = NeonMagenta,
                onValueChange = { engine.setSpatialImmersion(it) }
            )

            Spacer(Modifier.height(10.dp))

            PerceptualSliderRow(
                label = "Harmonic Reconstruction",
                value = snapshot.harmonicReconstruction,
                color = AmberSignal,
                onValueChange = { engine.setHarmonicReconstruction(it) }
            )

            Spacer(Modifier.height(10.dp))

            PerceptualSliderRow(
                label = "Anti-Dolby Blend",
                value = snapshot.antiDolbyBlend,
                color = AuroraCyan,
                onValueChange = { engine.setAntiDolbyBlend(it) }
            )

            Spacer(Modifier.height(10.dp))

            PerceptualSliderRow(
                label = "Human Loudness Comp.",
                value = snapshot.humanLoudnessCompensation,
                color = PhosphorGreen,
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
            .background(ObsidianGlass, RoundedCornerShape(8.dp))
            .padding(8.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Text(label, color = TextSecondary, fontSize = 8.sp, fontWeight = FontWeight.Bold)
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
private fun TextValueItem(label: String, value: String, color: Color = TextPrimary) {
    Column {
        Text(label, color = TextSecondary, fontSize = 8.sp)
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
            Text(label, color = TextSecondary, fontSize = 10.sp, fontWeight = FontWeight.SemiBold)
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
