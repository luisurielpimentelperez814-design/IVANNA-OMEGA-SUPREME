package com.ivanna.omega.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.platform.LocalContext
import com.ivanna.omega.audio.AdaptiveMode
import com.ivanna.omega.audio.AudioStateManager
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.magisk.OmegaEngineBridge
import com.ivanna.omega.core.NativeBridge
import com.ivanna.omega.neuromorphic.PiLstmBridge
import com.ivanna.omega.ui.theme.*
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

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
 * esta tarea. El botón se ancla a la tarjeta del núcleo neural
 * («Sistema nervioso superior · Fases 9–15»), el equivalente más cercano
 * dentro de BrainScreen; no se inventa ningún control nuevo.
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
 * SIN EQUIVALENTE EXACTO en este archivo: no hay panel PI-LSTM con ese
 * nombre aquí (el panel del predictor vive en AdaptiveEngineLivePanel.kt,
 * fuera del alcance de esta tarea). El botón se ancla a «Modo adaptativo»
 * (Motor A · Decisión en tiempo real), al que la predicción alimenta; no se
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
 * neuro-coclear» (60 %) no existe aquí; el slider «Neural Adaptation» vive
 * en PerceptualBrainDashboard.kt (el otro archivo de esta tarea). El botón
 * se ancla a «Telemetría perceptual», la vista de la percepción que el
 * modelo corrige; no se inventa ningún control nuevo.
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
 * fatiga» (40 %) no existe aquí; el slider «Fatigue Index» vive dentro de
 * TinyMlClassifierPanel (TinyMlClassifierPanel.kt, fuera del alcance de esta
 * tarea). El botón se ancla al encabezado que precede a ese panel en el tab
 * Perceptual; no se inventa ningún control nuevo.
 */
private val FatigueSpec = SpecInfo(
    title = "Gestión de fatiga auditiva",
    what = "Un vigilante de dosis sonora: sigue la energía acumulada en las bandas que más fatigan (presencia agresiva, picos repetidos) y suaviza los micro-picos que la disparan, sin bajar el volumen percibido de la música.",
    purpose = "Para terminar sesiones largas sin el oído «ardido»: la fatiga no avisa durante la escucha, se nota al final — y arruina la siguiente.",
    whenUse = "Sesiones de más de 45 minutos o escucha a volumen serio. 40 % es el punto discreto: protege sin que notes su mano.",
    whenAvoid = "En escucha crítica corta (juzgar una mezcla, comparar masters) apágalo: toca la micro-dinámica y eso es justo lo que estás evaluando."
)

/**
 * BrainScreen — Sección CEREBRO unificada.
 *
 * Reemplaza y unifica:
 *   · AdaptiveEngineScreen   → tab ADAPTATIVO
 *   · PerceptualBrainDashboard → tab PERCEPTUAL
 *   · AdaptiveDashboard      → tab TELEMETRÍA
 *   · IvannaLabScreen        → tab LAB
 *
 * Conecta funciones nativas antes sin UI:
 *   · nativeInitializeEvolution(popSize, generations)
 *   · nativeEvolveStep()
 *   · nativeSetMutationRate(rate)
 *   · nativeLabReset / nativeLabFeed / nativeLabMeasure / nativeLabReport
 */
@Composable
fun BrainScreen(
    modifier: Modifier = Modifier,
    adaptiveBack: com.ivanna.omega.audio.AdaptiveBackend? = null
) {
    val context = LocalContext.current
    val audioState by AudioStateManager.audioState.collectAsState()
    var selectedTab by remember { mutableIntStateOf(0) }
    val tabs = listOf("ADAPTATIVO", "PERCEPTUAL", "COGNITIVO 9-15", "EVOLUTIVO", "LAB", "PROFILER")

    // Bug F fix — estado evolutivo levantado para sobrevivir cambios de tab
    var prefs by remember { mutableStateOf(AdaptiveControlsPrefs.load(context)) }
    fun updatePrefs(update: (AdaptiveControlsState) -> AdaptiveControlsState) {
        prefs = update(prefs)
        AdaptiveControlsPrefs.save(context, prefs)
    }

    Column(modifier = modifier.background(MagistralVoid)) {
        IvannaScreenHeader("CEREBRO", MagistralGold)
        IvannaTabRow(tabs, selectedTab, { selectedTab = it }, MagistralGold)
        Column(
            modifier = Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 16.dp, vertical = 12.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            // Ficha del entorno · Brain (prototipo aprobado, Pantalla IV)
            Text(
                "CAPA NEURAL",
                color = MagistralGold,
                fontSize = 11.sp,
                fontWeight = FontWeight.ExtraBold,
                letterSpacing = 2.sp
            )
            Text(
                "La señal se optimiza para el cerebro que la recibe, no para un estándar industrial: cero distorsión perceptiva individual.",
                color = MagistralMuted,
                fontSize = 12.sp
            )
            ScreenSpecCard(spec = BrainScreenSpec)
            when (selectedTab) {
                0 -> AdaptiveTab(adaptiveBack)
                1 -> Column(verticalArrangement = androidx.compose.foundation.layout.Arrangement.spacedBy(12.dp)) {
                    PerceptualTab()
                    // H · Gestión de fatiga auditiva: el slider «Fatigue Index»
                    // vive dentro de TinyMlClassifierPanel (otro archivo); la
                    // ficha se ancla a este encabezado que lo precede.
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text(
                            "Gestión de fatiga auditiva",
                            color = MagistralGoldHi,
                            fontFamily = MagistralSerif,
                            fontWeight = FontWeight.Bold,
                            fontSize = 15.sp,
                            modifier = Modifier.weight(1f)
                        )
                        SpecInfoButton(spec = FatigueSpec)
                    }
                    TinyMlClassifierPanel()
                }
                2 -> CognitiveEvolutionTab()
                3 -> Column(verticalArrangement = androidx.compose.foundation.layout.Arrangement.spacedBy(12.dp)) { EvolutionTab(prefs, ::updatePrefs); CmaEsFitnessPanel() }
                4 -> LabTab()
                5 -> NeonProfilerPanel()
            }
        }
    }
}

// ── Tab ADAPTATIVO ────────────────────────────────────────────────────────────
@Composable
private fun AdaptiveTab(backend: com.ivanna.omega.audio.AdaptiveBackend? = null) {
    val context = LocalContext.current
    val audioState by AudioStateManager.audioState.collectAsState()

    // F · PI-LSTM Milenio: sin panel PI-LSTM en este archivo; la ficha se
    // ancla a «Modo adaptativo», al que la predicción alimenta.
    GlassCard(
        "MODO ADAPTATIVO",
        MagistralGold,
        "Motor A · Decisión en tiempo real",
        rightSlot = { SpecInfoButton(spec = PiLstmSpec) }
    ) {
        Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Row(
                horizontalArrangement = Arrangement.SpaceEvenly,
                modifier = Modifier.fillMaxWidth()
            ) {
                AdaptiveMode.values().forEach { mode ->
                    val sel = audioState.adaptiveMode == mode
                    FilledTonalButton(
                        onClick = {
                            AudioStateManager.updateState { it.copy(adaptiveMode = mode) }
                            if (IvannaNativeLib.isLoaded)
                                runCatching { IvannaNativeLib.nativeSetAdaptiveControls(mode.ordinal, audioState.adaptiveIntensity * 100f) }
                        },
                        colors = ButtonDefaults.filledTonalButtonColors(
                            containerColor = if (sel) MagistralGold.copy(alpha = 0.25f) else ObsidianEdge,
                            contentColor   = if (sel) MagistralGoldHi else TextMuted
                        )
                    ) { Text(mode.label, fontSize = 11.sp) }
                }
            }
            IvannaSliderRowBrain("INTENSIDAD", audioState.adaptiveIntensity, 0f, 1f, "%") { v ->
                AudioStateManager.updateState { it.copy(adaptiveIntensity = v) }
                if (IvannaNativeLib.isLoaded)
                    runCatching { IvannaNativeLib.nativeSetAdaptiveControls(audioState.adaptiveMode.ordinal, v * 100f) }
            }
            IvannaSliderRowBrain("SAFETY MARGIN", audioState.safetyMargin, 0.5f, 1f, "") { v ->
                AudioStateManager.updateState { it.copy(safetyMargin = v) }
                backend?.applyManualState(AudioStateManager.audioState.value)
            }
        }
    }

    Spacer(Modifier.height(4.dp))

    GlassCard("MODO MANUAL", AuroraCyan, "Parámetros directos · Bypass del motor A") {
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("MODO MANUAL", color = TextSecondary, fontSize = 12.sp, modifier = Modifier.weight(1f))
                Switch(
                    checked = audioState.manualModeEnabled,
                    onCheckedChange = { enabled ->
                        AudioStateManager.updateState { s -> s.copy(manualModeEnabled = enabled) }
                        // Motor A y manual nunca escriben a la vez (mismo contrato que AdaptiveEngineScreen).
                        val adaptiveRequested =
                            AdaptiveControlsPrefs.load(context).binauralAdaptEnabled && !enabled
                        if (IvannaNativeLib.isLoaded)
                            runCatching {
                                IvannaNativeLib.guardedNative(Unit) {
                                    IvannaNativeLib.nativeSetAdaptiveEngineEnabled(adaptiveRequested)
                                }
                            }
                        // Manual mode is exclusive: it disables adaptive actuation in AudioFlinger too.
                        runCatching {
                            OmegaEngineBridge.queueSystemWideAdaptiveEnabled(
                                AdaptiveControlsPrefs.load(context).binauralAdaptEnabled && !enabled
                            )
                        }
                        val st = AudioStateManager.audioState.value
                        if (enabled) { backend?.resetModulator(); backend?.forceManualState(st) }
                        else backend?.persist(st)
                    }
                )
            }
            ControlHelpText("MODO MANUAL")
            if (audioState.manualModeEnabled) {
                IvannaSliderRowBrain("COMPRESOR", audioState.compressorThreshold, -60f, 0f, "dB") { v ->
                    AudioStateManager.updateState { it.copy(compressorThreshold = v) }
                    backend?.applyManualState(AudioStateManager.audioState.value)
                }
                IvannaSliderRowBrain("EXCITER", audioState.exciterAmount, 0f, 1f, "") { v ->
                    AudioStateManager.updateState { it.copy(exciterAmount = v) }
                    backend?.applyManualState(AudioStateManager.audioState.value)
                }
            }
        }
    }
}

// ── Tab PERCEPTUAL ────────────────────────────────────────────────────────────
@Composable
private fun PerceptualTab() {
    // Fila = (etiqueta, texto con unidad, progreso 0..1). null = motor nativo sin datos.
    var rows by remember { mutableStateOf<List<Triple<String, String, Float>>?>(null) }

    LaunchedEffect(Unit) {
        kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.Default) {
        while (true) {
            val tele = if (IvannaNativeLib.isLoaded)
                runCatching { IvannaNativeLib.nativeGetAdaptiveTelemetry() }.getOrNull() else null
            rows = if (tele == null || tele.size < 10) null else {
                fun dbfs(lin: Float) = if (lin > 1e-6f) 20f * kotlin.math.log10(lin) else -120f
                fun norm01(v: Float) = if (v.isFinite()) v.coerceIn(0f, 1f) else 0f
                fun pct(v: Float) = "%.0f %%".format(norm01(v) * 100f)
                val rmsDb = dbfs(tele[0]); val peakDb = dbfs(tele[1])
                listOf(
                    Triple("RMS", "%.1f dBFS".format(rmsDb), norm01((rmsDb + 60f) / 60f)),
                    Triple("Pico", "%.1f dBFS".format(peakDb), norm01((peakDb + 60f) / 60f)),
                    Triple("Reducción de ganancia", "%.1f dB".format(tele[2]), norm01(kotlin.math.abs(tele[2]) / 12f)),
                    Triple("Ganancia objetivo", "%.2f ×".format(tele[3]), norm01(tele[3] / 2f)),
                    Triple("Compresión aplicada", pct(tele[4]), norm01(tele[4])),
                    Triple("Reducción de exciter", pct(tele[5]), norm01(tele[5])),
                    Triple("Ancho espacial", pct(tele[6]), norm01(tele[6] / 1.5f)),
                    Triple("Margen de seguridad", pct(tele[7]), norm01(tele[7])),
                    Triple("Protección de voz", pct(tele[8]), norm01(tele[8]))
                )
            }
            kotlinx.coroutines.delay(250)
        }
    
        }
    }

    // G · Neuro-Cochlear: sin control «Sensibilidad neuro-coclear» en este
    // archivo; la ficha se ancla a la telemetría de la percepción que el
    // modelo corrige.
    GlassCard(
        "TELEMETRÍA PERCEPTUAL",
        MagistralGold,
        "Motor adaptativo · 10 Hz",
        rightSlot = { SpecInfoButton(spec = NeuroCochlearSpec) }
    ) {
        val data = rows
        if (data == null) {
            Text("Motor nativo sin datos — activa el procesamiento para ver la telemetría.",
                color = TextSecondary, fontSize = 11.sp)
        } else Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
            data.forEach { (label, text, progress) ->
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Text(label, color = TextSecondary, fontSize = 11.sp)
                    Text(text, color = AuroraCyan, fontSize = 11.sp,
                        fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace)
                }
                LinearProgressIndicator(
                    progress = { progress },
                    modifier = Modifier.fillMaxWidth().height(3.dp),
                    color = AuroraCyan,
                    trackColor = ObsidianEdge
                )
            }
        }
    }
}

// ── Tab EVOLUTIVO ─────────────────────────────────────────────────────────────
// Bug F fix — popSize/generations/mutationRate levantados a prefs; isRunning/statusText
// son estado de sesión y pueden permanecer locales.
@Composable
private fun EvolutionTab(
    prefs: AdaptiveControlsState,
    updatePrefs: ((AdaptiveControlsState) -> AdaptiveControlsState) -> Unit
) {
    var isRunning by remember { mutableStateOf(false) }
    var statusText by remember { mutableStateOf("Listo") }

    GlassCard("KERNEL EVOLUTIVO", AuroraCyan, "LM-CMA-ES · 512 bandas · Genoma DSP") {
        Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {

            IvannaSliderRowBrain("TASA MUTACIÓN", prefs.evoMutationRate, 0.001f, 0.3f, "") { v ->
                updatePrefs { it.copy(evoMutationRate = v) }
                if (IvannaNativeLib.isLoaded) runCatching { IvannaNativeLib.nativeSetMutationRate(v) }
            }

            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Column(Modifier.weight(1f)) {
                    Text("POBLACIÓN", color = TextSecondary, fontSize = 10.sp)
                    Slider(
                        value = prefs.evoPopSize.toFloat().coerceIn(32f, 128f),
                        onValueChange = { updatePrefs { s -> s.copy(evoPopSize = it.toInt()) } },
                        onValueChangeFinished = {
                            // Aplica en caliente SIN reiniciar la población (nativeInitializeEvolution
                            // la regeneraría y perdería el progreso).
                            if (isRunning && IvannaNativeLib.isLoaded)
                                runCatching { IvannaNativeLib.nativeSetEvolutionParams(prefs.evoPopSize, prefs.evoGenerations) }
                        },
                        valueRange = 32f..128f,
                        colors = SliderDefaults.colors(thumbColor = AuroraCyan, activeTrackColor = AuroraCyan, inactiveTrackColor = ObsidianEdge)
                    )
                    Text("${prefs.evoPopSize}", color = AuroraCyan, fontSize = 10.sp,
                        fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace)
                }
                Column(Modifier.weight(1f)) {
                    Text("GENERACIONES", color = TextSecondary, fontSize = 10.sp)
                    Slider(
                        value = prefs.evoGenerations.toFloat(),
                        onValueChange = { updatePrefs { s -> s.copy(evoGenerations = it.toInt()) } },
                        onValueChangeFinished = {
                            if (isRunning && IvannaNativeLib.isLoaded)
                                runCatching { IvannaNativeLib.nativeSetEvolutionParams(prefs.evoPopSize, prefs.evoGenerations) }
                        },
                        valueRange = 10f..500f,
                        colors = SliderDefaults.colors(thumbColor = AuroraCyan, activeTrackColor = AuroraCyan, inactiveTrackColor = ObsidianEdge)
                    )
                    Text("${prefs.evoGenerations}", color = AuroraCyan, fontSize = 10.sp,
                        fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace)
                }
            }

            Text(statusText, color = TextMuted, fontSize = 10.sp,
                fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace)

            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(
                    onClick = {
                        if (IvannaNativeLib.isLoaded) {
                            runCatching {
                                val ok = IvannaNativeLib.nativeInitializeEvolution(prefs.evoPopSize, prefs.evoGenerations)
                                statusText = if (ok) "Evolución inicializada" else "Error al inicializar"
                                isRunning = ok
                            }.onFailure { statusText = "Error: ${it.message}" }
                        }
                    },
                    colors = ButtonDefaults.buttonColors(containerColor = AuroraCyan.copy(alpha = 0.2f), contentColor = AuroraCyan),
                    modifier = Modifier.weight(1f)
                ) { Text("INICIAR", fontSize = 11.sp) }

                Button(
                    onClick = {
                        if (IvannaNativeLib.isLoaded && isRunning) {
                            runCatching {
                                val cont = IvannaNativeLib.nativeEvolveStep()
                                statusText = if (cont) "Evolucionando..." else "Convergido"
                                isRunning = cont
                            }
                        }
                    },
                    enabled = isRunning,
                    colors = ButtonDefaults.buttonColors(containerColor = AuroraCyan.copy(alpha = 0.2f), contentColor = AuroraCyan),
                    modifier = Modifier.weight(1f)
                ) { Text("PASO", fontSize = 11.sp) }
            }
        }
    }
}

// ── Tab LAB ───────────────────────────────────────────────────────────────────
@Composable
private fun LabTab() {
    val scope = rememberCoroutineScope()
    var reportText by remember { mutableStateOf("Presiona MEDIR para iniciar") }
    var measureResult by remember { mutableStateOf<FloatArray?>(null) }
    var perceptualCues by remember { mutableStateOf<FloatArray?>(null) }
    var audioSpectrum by remember { mutableStateOf<FloatArray?>(null) }
    var phaseEnergy by remember { mutableFloatStateOf(0f) }
    var labAutoFrames by remember { mutableIntStateOf(0) }
    var labAutoEnabled by remember {
        mutableStateOf(
            if (IvannaNativeLib.isLoaded)
                IvannaNativeLib.guardedNative(true) { IvannaNativeLib.nativeIsLabAutoEnabled() }
            else true
        )
    }
    var neuralBenchUs by remember { mutableStateOf<FloatArray?>(null) }

    LaunchedEffect(Unit) {
        kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.Default) {
        while (true) {
            if (IvannaNativeLib.isLoaded) {
                perceptualCues = IvannaNativeLib.guardedNative(null) { IvannaNativeLib.nativeGetPerceptualCues() }
                audioSpectrum = IvannaNativeLib.guardedNative(null) { IvannaNativeLib.nativeGetAudioSpectrum() }
                phaseEnergy = IvannaNativeLib.guardedNative(0f) { IvannaNativeLib.nativeGetPhaseEnergy() }
                labAutoFrames = IvannaNativeLib.guardedNative(0) { IvannaNativeLib.nativeGetLabAutoFrameCount() }
                labAutoEnabled = IvannaNativeLib.guardedNative(labAutoEnabled) { IvannaNativeLib.nativeIsLabAutoEnabled() }
            }
            kotlinx.coroutines.delay(500L)
        }
    
        }
    }

    GlassCard("IVANNA LAB", MagistralGold, "Medición · Análisis · Reporte") {
        Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    "AUTO-FEED LAB (${if (labAutoEnabled) "ON" else "OFF"})",
                    color = if (labAutoEnabled) PhosphorGreen else TextMuted,
                    fontSize = 10.sp,
                    fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace
                )
                Switch(
                    checked = labAutoEnabled,
                    onCheckedChange = { en ->
                        labAutoEnabled = en
                        if (IvannaNativeLib.isLoaded) {
                            IvannaNativeLib.guardedNative(Unit) { IvannaNativeLib.nativeSetLabAutoEnabled(en) }
                        }
                    }
                )
            }
            ControlHelpText("AUTOMÁTICO DEL LABORATORIO")
            Text(reportText, color = TextMuted, fontSize = 10.sp,
                fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace,
                modifier = Modifier.fillMaxWidth())

            Text(
                "AUTO-FEED FRAMES: $labAutoFrames · PHASE ENERGY: ${"%.4f".format(phaseEnergy)}",
                color = AuroraCyan,
                fontSize = 10.sp,
                fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace
            )
            perceptualCues?.let { cues ->
                if (cues.size >= 4) {
                    Text(
                        "CUES [L=${"%.2f".format(cues[0])} T=${"%.2f".format(cues[1])} S=${"%.2f".format(cues[2])} R=${"%.2f".format(cues[3])}]",
                        color = TextSecondary,
                        fontSize = 10.sp,
                        fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace
                    )
                }
            }
            audioSpectrum?.let { spec ->
                if (spec.isNotEmpty()) {
                    val avgEnv = spec.average().toFloat()
                    Text(
                        "ESPECTRO BEB (${spec.size} bandas) · ENV MEDIA: ${"%.4f".format(avgEnv)}",
                        color = TextSecondary,
                        fontSize = 10.sp,
                        fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace
                    )
                }
            }
            neuralBenchUs?.let { b ->
                if (b.size >= 4) {
                    Text(
                        "BENCH NEURAL (µs/256f): NHO=${"%.1f".format(b[0])} BEB=${"%.1f".format(b[1])} SP=${"%.1f".format(b[2])} TOT=${"%.1f".format(b[3])}",
                        color = PhosphorGreen,
                        fontSize = 10.sp,
                        fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace
                    )
                }
            }

            measureResult?.let { m ->
                val labLabels = listOf(
                    "THD (%)",
                    "IMD SMPTE (%)",
                    "LUFS Integrado (LUFS)",
                    "Rango Dinámico LRA (LU)",
                    "SNR Estadístico (dB)",
                    "Peak (dBFS)",
                    "True Peak (dBTP)"
                )
                Text(
                    "THD requiere tono de prueba; IMD requiere doble tono SMPTE 250 Hz + 8 kHz. — = no medible con la señal actual.",
                    color = TextMuted, fontSize = 9.sp
                )
                Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    labLabels.forEachIndexed { i, lbl ->
                        if (i < m.size) {
                            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                                Text(lbl, color = TextSecondary, fontSize = 10.sp)
                                Text(if (m[i] == -1f) "—" else "%.3f".format(m[i]), color = AuroraCyan, fontSize = 10.sp,
                                    fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace)
                            }
                        }
                    }
                }
            }

            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(
                    onClick = {
                        if (IvannaNativeLib.isLoaded) {
                            // nativeLabReset — antes sin UI
                            runCatching { IvannaNativeLib.nativeLabReset() }
                            reportText = "Lab reiniciado"
                            measureResult = null
                        }
                    },
                    colors = ButtonDefaults.buttonColors(containerColor = ObsidianEdge, contentColor = TextSecondary),
                    modifier = Modifier.weight(1f)
                ) { Text("RESET", fontSize = 11.sp) }

                Button(
                    onClick = {
                        if (IvannaNativeLib.isLoaded) {
                            // nativeLabMeasure — antes sin UI
                            runCatching {
                                measureResult = IvannaNativeLib.nativeLabMeasure()
                                // nativeLabReport — antes sin UI
                                reportText = IvannaNativeLib.nativeLabReport()
                            }.onFailure { reportText = "Error: ${it.message}" }
                        }
                    },
                    colors = ButtonDefaults.buttonColors(containerColor = AuroraCyan.copy(alpha = 0.2f), contentColor = AuroraCyan),
                    modifier = Modifier.weight(1f)
                ) { Text("MEDIR", fontSize = 11.sp) }

                Button(
                    onClick = {
                        if (IvannaNativeLib.isLoaded) {
                            scope.launch(kotlinx.coroutines.Dispatchers.Default) {
                                val res = IvannaNativeLib.guardedNative(null) {
                                    IvannaNativeLib.nativeRunNeuralBenchmarks()
                                }
                                kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.Main) {
                                    neuralBenchUs = res
                                }
                            }
                        }
                    },
                    colors = ButtonDefaults.buttonColors(containerColor = AuroraCyan.copy(alpha = 0.2f), contentColor = AuroraCyan),
                    modifier = Modifier.weight(1f)
                ) { Text("BENCH", fontSize = 11.sp) }
            }
        }
    }
}

// ── Tab COGNITIVO 9-15 (Acoustic Reality Cognitive Evolution Engine) ──────────
@Composable
private fun CognitiveEvolutionTab() {
    val context = LocalContext.current
    var axesState by remember { mutableStateOf(SupremeAxesPrefs.load(context)) }
    var realityEnabled by remember { mutableStateOf(axesState.realityReconstructionEnabled) }
    var realityIntensity by remember { mutableFloatStateOf(axesState.realityIntensity) }
    var realityTele by remember { mutableStateOf(NativeBridge.safeGetRealityTelemetryOrNull()) }
    var cogTele by remember { mutableStateOf(NativeBridge.safeGetCognitiveTelemetryOrNull()) }
    var daemonUp by remember { mutableStateOf(false) }
    var adaptiveRunning by remember { mutableStateOf(false) }

    LaunchedEffect(Unit) {
        kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.Default) {
        NativeBridge.safeSetRealityReconstructionEnabled(realityEnabled)
        NativeBridge.safeSetRealityIntensity(realityIntensity)
        while (true) {
            realityTele = NativeBridge.safeGetRealityTelemetryOrNull()
            cogTele = NativeBridge.safeGetCognitiveTelemetryOrNull()
            daemonUp = OmegaEngineBridge.isConnected
            adaptiveRunning = runCatching { IvannaNativeLib.nativeIsAdaptiveEngineRunning() }.getOrDefault(false)
            kotlinx.coroutines.delay(250L)
        }
    
        }
    }

    val priorityNames = listOf(
        "1. PROFUNDIDAD FÍSICA",
        "2. MICRODINÁMICA",
        "3. CLARIDAD VOCAL",
        "4. EXPANSIÓN AMBIENTAL"
    )
    val topAxisIdx = cogTele?.getOrNull(0)?.toInt()?.coerceIn(0, 3)
    val cogPct: (Int) -> Float? = { i -> cogTele?.getOrNull(i)?.takeIf { it.isFinite() } }
    val realPct: (Int) -> Float? = { i -> realityTele?.getOrNull(i)?.takeIf { it.isFinite() } }
    val fmtPct: (Float?) -> String = { v -> v?.let { "%.1f %%".format(it * 100f) } ?: "—" }

    // E · Hiper-Operador de Estado Φ: sin control «Intensidad Φ» en este
    // archivo; la ficha se ancla a la tarjeta del núcleo neural.
    GlassCard(
        "SISTEMA NERVIOSO SUPERIOR · FASES 9–15",
        MagistralGold,
        "AcousticCognitiveCore · Specialist Network · ExecutiveBrain · Memory · Homeostasis · DigitalTwin",
        rightSlot = { SpecInfoButton(spec = PhiSpec) }
    ) {
        Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(
                    "RECONSTRUCCIÓN COGNITIVA DE REALIDAD ACÚSTICA",
                    color = TextSecondary,
                    fontSize = 11.sp,
                    modifier = Modifier.weight(1f)
                )
                Switch(
                    checked = realityEnabled,
                    onCheckedChange = { en ->
                        realityEnabled = en
                        val next = axesState.copy(realityReconstructionEnabled = en)
                        axesState = next
                        SupremeAxesPrefs.save(context, next)
                        NativeBridge.safeSetRealityReconstructionEnabled(en)
                    }
                )
            }
            ControlHelpText("RECONSTRUCCIÓN DE REALIDAD")
            IvannaSliderRowBrain("INTENSIDAD REALIDAD", realityIntensity, 0f, 1f, "") { v ->
                realityIntensity = v
                val next = axesState.copy(realityIntensity = v)
                axesState = next
                SupremeAxesPrefs.save(context, next)
                NativeBridge.safeSetRealityIntensity(v)
            }
            Text(
                "EJE LÍDER ACTUAL: ${topAxisIdx?.let { priorityNames[it] } ?: when {
                    !IvannaNativeLib.isLoaded -> "— (motor nativo no cargado)"
                    !realityEnabled           -> "— (reconstrucción desactivada)"
                    adaptiveRunning           -> "— (motor midiendo; primer ciclo en curso)"
                    daemonUp                  -> "— (el módulo procesa el audio fuera de la app: su telemetría cognitiva aún no se exporta aquí; activa la captura de la app para medirla)"
                    else                      -> "— (sin audio en el motor de la app: activa la captura y reproduce algo)"
                }}",
                color = PhosphorGreen,
                fontSize = 11.sp,
                fontWeight = FontWeight.Bold,
                fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace
            )
        }
    }

    GlassCard(
        "FASE 9 & 10 · PRIORIDADES COGNITIVAS Y RED DE ESPECIALISTAS",
        MagistralGold,
        "Spatial · Room · MicroReality · HumanPerception Judge"
    ) {
        val metrics: List<Pair<String, Float?>> = listOf(
            "Prioridad Profundidad" to cogPct(1),
            "Prioridad Microdinámica" to cogPct(2),
            "Prioridad Claridad Vocal" to cogPct(3),
            "Prioridad Expansión" to cogPct(4),
            "Spatial Agent (Localización)" to cogPct(5),
            "Room Agent (Realismo Físico)" to cogPct(6),
            "MicroReality Agent (Vitalidad)" to cogPct(7),
            "HumanPerception Judge (Veredicto)" to cogPct(8),
            "Índice Anti-Espectacularidad" to cogPct(9)
        )
        Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
            metrics.forEach { (label, value) ->
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Text(label, color = TextSecondary, fontSize = 11.sp)
                    Text(
                        fmtPct(value),
                        color = AuroraCyan,
                        fontSize = 11.sp,
                        fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace
                    )
                }
                LinearProgressIndicator(
                    progress = { (value ?: 0f).coerceIn(0f, 1f) },
                    modifier = Modifier.fillMaxWidth().height(3.dp),
                    color = AuroraCyan,
                    trackColor = ObsidianEdge
                )
            }
        }
    }

    GlassCard(
        "FASES 11–15 · EXECUTIVE BRAIN, MEMORIA, HOMEOSTASIS Y GEMELO DIGITAL",
        MagistralGold,
        "Arbitraje de Conflictos · Rush Xanadu Memory · PID Homeostático · CMA-ES + Q-Learning"
    ) {
        val r6 = realPct(6)
        val r7 = realPct(7)
        val r8 = realPct(8)
        val roomDimensions = if (r6 != null && r7 != null && r8 != null) {
            "%.1f×%.1f×%.1f m".format(r6, r7, r8)
        } else {
            "—"
        }
        val execMetrics: List<Pair<String, String>> = listOf(
            "Coherencia Executive Brain" to fmtPct(cogPct(10)),
            "Conflictos Arbitrados (Flags)" to (cogPct(11)?.let { "0x%02X".format(it.toInt()) } ?: "—"),
            "Estabilidad Homeostática (Fase 13)" to fmtPct(cogPct(12)),
            "Coherencia Digital Twin (Fase 14)" to fmtPct(cogPct(13)),
            "Fitness CMA-ES / Q-Learning (Fase 15)" to (cogPct(14)?.let { "%.3f".format(it) } ?: "—"),
            "Consolidaciones Memoria (Fase 12)" to (cogPct(15)?.let { "${it.toInt()} estados" } ?: "—"),
            "Sala Inferida (volumen estimado · proporciones fijas 1:1.4:1.9)" to roomDimensions,
            "Realismo Perceptual Compuesto" to fmtPct(realPct(5))
        )
        Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
            execMetrics.forEach { (k, v) ->
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Text(k, color = TextSecondary, fontSize = 11.sp)
                    Text(
                        v,
                        color = PhosphorGreen,
                        fontSize = 11.sp,
                        fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace
                    )
                }
            }
        }
    }
}

// ── Slider helper ─────────────────────────────────────────────────────────────
@Composable
private fun IvannaSliderRowBrain(
    label: String, value: Float, min: Float, max: Float, unit: String,
    onValueChange: (Float) -> Unit
) {
    val safeValue = com.ivanna.omega.safmus.SafmusUniversalKernel
        .sanitizeControlValue(value, min, max) ?: min
    Column {
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            Text(label, color = TextSecondary, fontSize = 11.sp)
            Text("${"%.2f".format(value)} $unit", color = AuroraCyan, fontSize = 11.sp,
                fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace)
        }
        Slider(
            value = safeValue,
            onValueChange = { proposed ->
                com.ivanna.omega.safmus.SafmusUniversalKernel
                    .sanitizeControlValue(proposed, min, max)?.let(onValueChange)
            },
            valueRange = min..max,
            colors = SliderDefaults.colors(thumbColor = AuroraCyan, activeTrackColor = AuroraCyan, inactiveTrackColor = ObsidianEdge)
        )
        ControlHelpText(label)
    }
}
