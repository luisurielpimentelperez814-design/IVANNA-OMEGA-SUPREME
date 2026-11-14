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
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.ai.PerceptualBrainEngine
import com.ivanna.omega.ui.theme.*
import com.ivanna.omega.ai.PerceptualSnapshot
import com.ivanna.omega.preferences.PerceptualBrainPrefs

// ── Fichas magistrales · Brain (prototipo aprobado, Pantalla IV) ────────────
// Texto exacto del prototipo; nada inventado. SpecInfo / ScreenSpecCard /
// SpecInfoButton viven en SpecSheet.kt, mismo paquete.

/** Ficha del entorno · Brain. */
private val BrainScreenSpec = SpecInfo(
    title = "Brain",
    what = "El entorno neural de IVANNA: el hiper-operador Φ, la predicción PI-LSTM Milenio, el modelo Neuro-Cochlear y la gestión de fatiga. Todo lo que adapta la cadena a tu sistema nervioso, no solo a tu equipo.",
    purpose = "Para cerrar el bucle entre señal, transductor y oyente: la cadena deja de ser una lista de efectos y pasa a ser un solo organismo que anticipa, siente la respuesta del altavoz y protege tu oído en sesiones largas.",
    whenUse = "Déjalo en sus valores de directiva (Φ al 50 %, predicción activa) como configuración permanente. Entra aquí para comparar A/B o para ajustar la fatiga según la duración de tus sesiones.",
    whenAvoid = "No apagues capas neurales para «ganar pureza» mientras diagnosticas otro problema: primero aísla la causa en System. Brain apagado a medias (Φ sí, predicción no) es el estado menos coherente de todos."
)

/**
 * E · Hiper-Operador de Estado Φ.
 *
 * SIN EQUIVALENTE EXACTO en este archivo: el control «Intensidad Φ» (50 %)
 * vive en ControlTabScreen.kt / IvannaControlPanel.kt, fuera del alcance de
 * esta tarea. El botón se ancla al «Panel DSP Cortex & Field Spatial», que
 * alberga el estado del NPE —el componente neuro de Φ según el prototipo—;
 * no se inventa ningún control nuevo.
 */
private val PhiSpec = SpecInfo(
    title = "Hiper-Operador de Estado Φ",
    what = "Φ fusiona en un solo operador el bucle cerrado neural, los campos de onda continuos, la pre-distorsión Volterra, el sensado Back-EMF (la señal que el propio altavoz devuelve) y el flujo Zero-PCM, con tres vectores continuos: S_neuro(t), Z_EMF(t) y W_acústico(r,θ). Este control es su cantidad.",
    purpose = "Para que la corrección no llegue tarde: al operar muestra a muestra sobre el estado completo —señal, transductor y oyente— anticipa lo que el sistema va a hacer en vez de reaccionar un bloque después. Sensor: el micrófono del teléfono; componente neuro: el NPE del propio dispositivo, sin hardware externo.",
    whenUse = "Activo siempre, al 50 %, su valor de directiva. Es parte del sonido de la casa: la cadena magistral se afinó con Φ presente.",
    whenAvoid = "Apágalo solo para comparar A/B o diagnosticar: Φ apagado + NEU·Ω solo es la referencia limpia. No lo lleves al 100 % junto con todos los demás módulos al máximo: la cadena se vuelve impredecible y el A/B deja de decirte algo."
)

/**
 * F · PI-LSTM Milenio · Predicción neural.
 *
 * SIN EQUIVALENTE EXACTO en este archivo: no hay control PI-LSTM con ese
 * nombre aquí (el panel del predictor vive en AdaptiveEngineLivePanel.kt,
 * fuera del alcance de esta tarea). El botón se ancla al «Panel TinyML»,
 * el panel de predicción/clasificación neural de este dashboard; no se
 * inventa ningún control nuevo.
 */
private val PiLstmSpec = SpecInfo(
    title = "PI-LSTM Milenio · Predicción neural",
    what = "Una red neuronal recurrente (LSTM) informada por la física del sistema que predice la evolución inmediata de la señal y del estado acústico, en vez de limitarse a reaccionar a lo que ya pasó.",
    purpose = "Alimenta a Φ y a NEU·Ω con la pose y la señal predichas al instante de emisión: lo que oyes corresponde a donde está tu cabeza ahora, no a donde estaba hace un bloque de procesado.",
    whenUse = "Siempre activo si usas ORBE o head-tracking: la predicción es lo que hace que el campo se sienta pegado al mundo real al moverte.",
    whenAvoid = "Solo se apaga en diagnóstico. Sin predicción, con el campo espacial activo, notarás un «arrastre» del escenario al girar la cabeza: es el retardo que este módulo existe para eliminar."
)

/**
 * G · Neuro-Cochlear · Firma biológica.
 *
 * SIN EQUIVALENTE EXACTO en este archivo: el control «Sensibilidad
 * neuro-coclear» (60 %) no existe aquí; el slider más cercano, «Neural
 * Adaptation», es un control distinto y no se renombra. El botón se ancla
 * al «Panel Humano Auditivo (Psychoacoustics)» —bandas Bark/Mel,
 * enmascaramiento—, el modelo auditivo más cercano a la cóclea en este
 * dashboard; no se inventa ningún control nuevo.
 */
private val NeuroCochlearSpec = SpecInfo(
    title = "Neuro-Cochlear · Firma biológica",
    what = "Un modelo de tu cóclea y tu percepción que compensa la señal según tu firma biológica única. La sensibilidad regula cuánto de esa compensación personal se aplica sobre la señal estándar.",
    purpose = "Dos oyentes no oyen igual la misma señal. Este modelo busca cero distorsión perceptiva individual: que el equilibrio que tú percibes sea el de la mezcla, no el de tu oído sin corregir.",
    whenUse = "Tras completar tu perfil de escucha, empieza en 50–60 % y ajusta en pasos pequeños durante varios días, no en una sola sesión: la adaptación perceptiva es lenta.",
    whenAvoid = "No uses el perfil de otra persona: la firma es individual y la compensación ajena empeora la tuya. Y no subas al 100 % esperando «más efecto»: sobrecompensa y colorea."
)

/**
 * H · Gestión de fatiga auditiva.
 *
 * SIN EQUIVALENTE EXACTO en este archivo: el control «Protección contra
 * fatiga» (40 %) no existe aquí; el slider «Fatigue Index» vive en
 * TinyMlClassifierPanel.kt, fuera del alcance de esta tarea. El botón se
 * ancla a la tarjeta «Estado del cerebro perceptual», que alberga la
 * métrica FATIGUE INDEX en vivo; no se inventa ningún control nuevo.
 */
private val FatigueSpec = SpecInfo(
    title = "Gestión de fatiga auditiva",
    what = "Un vigilante de dosis sonora: sigue la energía acumulada en las bandas que más fatigan (presencia agresiva, picos repetidos) y suaviza los micro-picos que la disparan, sin bajar el volumen percibido de la música.",
    purpose = "Para terminar sesiones largas sin el oído «ardido»: la fatiga no avisa durante la escucha, se nota al final — y arruina la siguiente.",
    whenUse = "Sesiones de más de 45 minutos o escucha a volumen serio. 40 % es el punto discreto: protege sin que notes su mano.",
    whenAvoid = "En escucha crítica corta (juzgar una mezcla, comparar masters) apágalo: toca la micro-dinámica y eso es justo lo que estás evaluando."
)


@Composable
fun PerceptualBrainDashboard(
    engine: PerceptualBrainEngine,
    onBack: () -> Unit,
    modifier: Modifier = Modifier
) {
    val context = LocalContext.current

    // FIX (persistencia): los 6 controles perceptuales llegaban al DSP nativo
    // pero nunca se guardaban; al reabrir la app el motor quedaba con los
    // defaults del snapshot. Restauramos el estado guardado en el motor real.
    LaunchedEffect(engine) {
        val saved = PerceptualBrainPrefs.load(context)
        engine.setPerceptualIntelligence(saved.perceptualIntelligence)
        engine.setNeuralAdaptation(saved.neuralAdaptation)
        engine.setSpatialImmersion(saved.spatialImmersion)
        engine.setHarmonicReconstruction(saved.harmonicReconstruction)
        engine.setAntiDolbyBlend(saved.antiDolbyBlend)
        engine.setHumanLoudnessCompensation(saved.humanLoudnessCompensation)
    }

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
            .background(MagistralVoid)
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
                    color = MagistralGoldHi,
                    fontFamily = MagistralSerif,
                    fontSize = 14.sp,
                    fontWeight = FontWeight.ExtraBold,
                    letterSpacing = 1.2.sp
                )
                Text(
                    text = "IVANNA OMEGA SUPREME · NEURO-ACOUSTIC BRAIN",
                    color = MagistralMuted,
                    fontSize = 9.sp,
                    letterSpacing = 1.sp
                )
            }
            OutlinedButton(
                onClick = onBack,
                colors = ButtonDefaults.outlinedButtonColors(contentColor = AuroraCyan),
                border = BorderStroke(1.dp, AuroraCyan),
                shape = RoundedCornerShape(8.dp),
                contentPadding = PaddingValues(horizontal = 12.dp, vertical = 4.dp)
            ) {
                Text("CERRAR", fontSize = 10.sp, fontWeight = FontWeight.Bold)
            }
        }

        Spacer(Modifier.height(16.dp))

        // Ficha del entorno · Brain (prototipo aprobado, Pantalla IV)
        ScreenSpecCard(spec = BrainScreenSpec)

        Spacer(Modifier.height(12.dp))

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
        colors = CardDefaults.cardColors(containerColor = MagistralPanel),
        border = BorderStroke(1.dp, MagistralLine)
    ) {
        Column(modifier = Modifier.padding(14.dp)) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "ESTADO DEL CEREBRO PERCEPTUAL",
                    color = MagistralGoldHi,
                    fontFamily = MagistralSerif,
                    fontSize = 11.sp,
                    fontWeight = FontWeight.ExtraBold,
                    letterSpacing = 1.sp,
                    modifier = Modifier.weight(1f)
                )
                // H · Gestión de fatiga auditiva: sin control «Protección
                // contra fatiga» en este archivo; la ficha se ancla a la
                // tarjeta que alberga la métrica FATIGUE INDEX en vivo.
                SpecInfoButton(spec = FatigueSpec)
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Box(
                        modifier = Modifier
                            .size(8.dp)
                            .clip(CircleShape)
                            .background(
                                when {
                                    !snapshot.dataAvailable -> TextMuted
                                    snapshot.perceptionOnline -> PhosphorGreen
                                    else -> CoralWarn
                                }
                            )
                    )
                    Spacer(Modifier.width(6.dp))
                    Text(
                        text = when {
                            !snapshot.dataAvailable -> "SIN SEÑAL"
                            snapshot.perceptionOnline -> "ENGINE ONLINE"
                            else -> "OFFLINE"
                        },
                        color = when {
                            !snapshot.dataAvailable -> TextMuted
                            snapshot.perceptionOnline -> PhosphorGreen
                            else -> CoralWarn
                        },
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
                    color = AuroraCyan,
                    modifier = Modifier.weight(1f)
                )
                Spacer(Modifier.width(8.dp))
                MetricProgressBlock(
                    label = "ATTENTION",
                    value = snapshot.attention,
                    color = AuroraCyan,
                    modifier = Modifier.weight(1f)
                )
            }

            Spacer(Modifier.height(8.dp))

            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                MetricProgressBlock(
                    label = "FATIGUE INDEX",
                    value = snapshot.fatigue,
                    color = if (snapshot.fatigue > 0.4f) CoralWarn else PhosphorGreen,
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
        colors = CardDefaults.cardColors(containerColor = MagistralPanel),
        border = BorderStroke(1.dp, MagistralLine)
    ) {
        Column(modifier = Modifier.padding(14.dp)) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "PANEL HUMANO AUDITIVO (PSYCHOACOUSTICS)",
                    color = MagistralGoldHi,
                    fontFamily = MagistralSerif,
                    fontSize = 11.sp,
                    fontWeight = FontWeight.ExtraBold,
                    letterSpacing = 1.sp,
                    modifier = Modifier.weight(1f)
                )
                // G · Neuro-Cochlear: sin control «Sensibilidad
                // neuro-coclear» en este archivo; la ficha se ancla al
                // panel del modelo auditivo (Bark/Mel, enmascaramiento).
                SpecInfoButton(spec = NeuroCochlearSpec)
            }
            Spacer(Modifier.height(10.dp))

            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                TextValueItem(label = "ISO 226 Loudness", value = "%.1f LUFS".format(snapshot.iso226LoudnessDb))
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
                color = MagistralGoldHi
            )
        }
    }
}

@Composable
private fun TinyMlClassifierPanel(snapshot: PerceptualSnapshot) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(12.dp),
        colors = CardDefaults.cardColors(containerColor = MagistralPanel),
        border = BorderStroke(1.dp, MagistralLine)
    ) {
        Column(modifier = Modifier.padding(14.dp)) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "PANEL TINYML (CONVNEXT INT8 CLASSIFIER)",
                    color = MagistralGoldHi,
                    fontFamily = MagistralSerif,
                    fontSize = 11.sp,
                    fontWeight = FontWeight.ExtraBold,
                    letterSpacing = 1.sp,
                    modifier = Modifier.weight(1f)
                )
                // F · PI-LSTM Milenio: sin control PI-LSTM en este archivo;
                // la ficha se ancla al panel de predicción neural.
                SpecInfoButton(spec = PiLstmSpec)
            }
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
                TextValueItem(label = "Class Confidence", value = "%.1f%%".format(snapshot.convNextConfidence * 100f), color = MagistralGoldHi)
            }
        }
    }
}

@Composable
private fun DspCortexPanel(snapshot: PerceptualSnapshot) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(12.dp),
        colors = CardDefaults.cardColors(containerColor = MagistralPanel),
        border = BorderStroke(1.dp, MagistralLine)
    ) {
        Column(modifier = Modifier.padding(14.dp)) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "PANEL DSP CORTEX & FIELD SPATIAL",
                    color = MagistralGoldHi,
                    fontFamily = MagistralSerif,
                    fontSize = 11.sp,
                    fontWeight = FontWeight.ExtraBold,
                    letterSpacing = 1.sp,
                    modifier = Modifier.weight(1f)
                )
                // E · Hiper-Operador de Estado Φ: sin control «Intensidad Φ»
                // en este archivo; la ficha se ancla al panel que alberga
                // el estado del NPE, componente neuro de Φ.
                SpecInfoButton(spec = PhiSpec)
            }
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
    val context = LocalContext.current
    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(12.dp),
        colors = CardDefaults.cardColors(containerColor = MagistralPanel),
        border = BorderStroke(1.dp, MagistralLine)
    ) {
        Column(modifier = Modifier.padding(14.dp)) {
            Text(
                text = "CONTROLES DE INTELIGENCIA PERCEPTUAL",
                color = MagistralGoldHi,
                fontFamily = MagistralSerif,
                fontSize = 11.sp,
                fontWeight = FontWeight.ExtraBold,
                letterSpacing = 1.sp
            )
            Spacer(Modifier.height(12.dp))

            PerceptualSliderRow(
                label = "Perceptual Intelligence",
                value = snapshot.perceptualIntelligence,
                color = AuroraCyan,
                onValueChange = {
                    engine.setPerceptualIntelligence(it)
                    PerceptualBrainPrefs.save(context, PerceptualBrainPrefs.load(context).copy(perceptualIntelligence = it))
                }
            )

            Spacer(Modifier.height(10.dp))

            PerceptualSliderRow(
                label = "Neural Adaptation",
                value = snapshot.neuralAdaptation,
                color = AuroraCyan,
                onValueChange = {
                    engine.setNeuralAdaptation(it)
                    PerceptualBrainPrefs.save(context, PerceptualBrainPrefs.load(context).copy(neuralAdaptation = it))
                }
            )

            Spacer(Modifier.height(10.dp))

            PerceptualSliderRow(
                label = "Spatial Immersion",
                value = snapshot.spatialImmersion,
                color = AuroraCyan,
                onValueChange = {
                    engine.setSpatialImmersion(it)
                    PerceptualBrainPrefs.save(context, PerceptualBrainPrefs.load(context).copy(spatialImmersion = it))
                }
            )

            Spacer(Modifier.height(10.dp))

            PerceptualSliderRow(
                label = "Harmonic Reconstruction",
                value = snapshot.harmonicReconstruction,
                color = AuroraCyan,
                onValueChange = {
                    engine.setHarmonicReconstruction(it)
                    PerceptualBrainPrefs.save(context, PerceptualBrainPrefs.load(context).copy(harmonicReconstruction = it))
                }
            )

            Spacer(Modifier.height(10.dp))

            PerceptualSliderRow(
                label = "Anti-Dolby Blend",
                value = snapshot.antiDolbyBlend,
                color = AuroraCyan,
                onValueChange = {
                    engine.setAntiDolbyBlend(it)
                    PerceptualBrainPrefs.save(context, PerceptualBrainPrefs.load(context).copy(antiDolbyBlend = it))
                }
            )

            Spacer(Modifier.height(10.dp))

            PerceptualSliderRow(
                label = "Human Loudness Comp.",
                value = snapshot.humanLoudnessCompensation,
                color = AuroraCyan,
                onValueChange = {
                    engine.setHumanLoudnessCompensation(it)
                    PerceptualBrainPrefs.save(context, PerceptualBrainPrefs.load(context).copy(humanLoudnessCompensation = it))
                }
            )
        }
    }
}

@Composable
private fun MetricProgressBlock(
    label: String,
    value: Float,
    color: Color,
    modifier: Modifier = Modifier,
    dataAvailable: Boolean = true
) {
    Column(
        modifier = modifier
            .background(MagistralPanel2, RoundedCornerShape(8.dp))
            .padding(8.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Text(label, color = TextSecondary, fontSize = 8.sp, fontWeight = FontWeight.Bold)
            Text(
                if (dataAvailable) "%.0f%%".format(value * 100f) else "—",
                color = if (dataAvailable) color else TextMuted,
                fontSize = 8.sp, fontWeight = FontWeight.ExtraBold
            )
        }
        Spacer(Modifier.height(4.dp))
        LinearProgressIndicator(
            progress = { if (dataAvailable) value.coerceIn(0f, 1f) else 0f },
            modifier = Modifier
                .fillMaxWidth()
                .height(4.dp)
                .clip(RoundedCornerShape(2.dp)),
            color = if (dataAvailable) color else TextMuted,
            trackColor = MagistralLine
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
                inactiveTrackColor = MagistralLine
            ),
            modifier = Modifier.height(24.dp)
        )
    }
}
