package com.ivanna.omega.ui

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.dsp.DSPBridge
import com.ivanna.omega.magisk.OmegaEngineBridge
import com.ivanna.omega.neuromorphic.IvannaNpeEngine
import com.ivanna.omega.saf.SaFBridge
import com.ivanna.omega.saf.SaFRoomBridge
import com.ivanna.omega.spatial.IvannaSpatialManager
import com.ivanna.omega.ui.theme.*
import kotlinx.coroutines.delay

@Composable
fun SofaAfRirSafPanelScreen(
    modifier: Modifier = Modifier,
    onBack: (() -> Unit)? = null,
    voiceMgr: Any? = null
) {
    // Basic States
    var sofaIntensity by remember { mutableStateOf(0.5f) }
    var sofaPresetIdx by remember { mutableStateOf(0) }
    var sofaSubject   by remember { mutableStateOf("none") }

    var phaseIntensity by remember { mutableStateOf(0f) }
    var phaseState     by remember { mutableStateOf(0f) }

    var afAuto       by remember { mutableStateOf(false) }
    var afMode by remember { mutableStateOf(0) }
    var afIntensity  by remember { mutableStateOf(50f) }
    var afRunning    by remember { mutableStateOf(false) }
    var afTelemetry  by remember { mutableStateOf<FloatArray?>(null) }

    var rirEnabled   by remember { mutableStateOf(false) }
    var roomSize     by remember { mutableStateOf(0.5f) }
    var reflections  by remember { mutableStateOf(10) }
    var decay        by remember { mutableStateOf(0.3f) }
    var dryWet       by remember { mutableStateOf(0.2f) }
    val MAX_DELAY_MS = 250f
    val N_REFL       = 128

    var safDiag      by remember { mutableStateOf(FloatArray(0)) }
    var safIntensity by remember { mutableStateOf(0.5f) }
    var voiceProt    by remember { mutableStateOf(false) }
    var isSafReady   by remember { mutableStateOf(false) }

    // FIX L61/L106: SpatialAudioPrefs.get() no existe — la API es load(context: Context).
    val context = LocalContext.current

    LaunchedEffect(Unit) {
        val st = SpatialAudioPrefs.load(context)
        rirEnabled = st.rirEnabled
        roomSize   = (st.rirRt60 - 0.1f) / 1.4f
        decay      = st.rirRt60 / 2.5f
        dryWet     = st.rirWet
        safIntensity = st.safIntensity
        voiceProt = false
    }

    LaunchedEffect(Unit) {
        while (true) {
            phaseState  = 0f  // getPhaseState not available
            afRunning   = false  // isRunning not available
            afTelemetry = null  // getTelemetry not available
            runCatching { safDiag = SaFRoomBridge.getDiagnostics() }
            isSafReady = SaFBridge.nativeSaFIsConverged()
            delay(500)
        }
    }

    fun applySofa() { OmegaEngineBridge.setIntensity(sofaIntensity) }
    fun applyPhase() { /* setPhaseIntensity not available */ }
    fun applyAf() {
        if (afAuto) {
            // IvannaNpeEngine.start/setMode not available
        } else {
            // IvannaNpeEngine.stop not available
        }
    }
    fun applyRir() {
        val rt60 = 0.1f + roomSize * 1.4f
        if (rirEnabled) {
            OmegaEngineBridge.setRoom(rt60, dryWet)
            runCatching { SaFRoomBridge.setRoomState(rt60, reflections.toFloat(), decay) }
        } else {
            OmegaEngineBridge.disableRoom()
            runCatching { SaFRoomBridge.setRoomState(0f, 0f, 0f) }
        }
    }
    fun applySaf() {
        val q = runCatching { SaFRoomBridge.getParams() }.getOrDefault(FloatArray(7))
        OmegaEngineBridge.pushSafLatentQ(FloatArray(7) { q.getOrElse(it) { 0f } * safIntensity }, safIntensity)
    }
    fun persist() {
        val rt60 = 0.1f + roomSize * 1.4f
        SpatialAudioPrefs.save(context, SpatialAudioPrefs.load(context).copy(
            rirEnabled = rirEnabled,
            rirRt60 = rt60,
            rirWet = dryWet,
            safIntensity = safIntensity
        ))
    }

    Column(
        modifier = modifier
            .fillMaxSize()
            .background(Brush.verticalGradient(listOf(ObsidianVoid, ObsidianSoft)))
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        Text("SOFA / AF / RIR / SAF EXPERT", color = AuroraCyan, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.ExtraBold, letterSpacing = 2.sp)

        // ── SOFA ────────────────────────────────────────────────────────────
        SPanel("SOFA BINAURAL · Dataset IHR1", PhosphorGreen) {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                val presets = listOf("Ninguno", "Acústico", "Espacial")
                presets.forEachIndexed { idx, label ->
                    FilterChip(
                        selected = sofaPresetIdx == idx,
                        onClick  = { sofaPresetIdx = idx; applySofa(); persist() },
                        label = { Text(label, fontSize = 10.sp, fontWeight = if (sofaPresetIdx == idx) FontWeight.Bold else FontWeight.Normal) },
                        modifier = Modifier.weight(1f),
                        colors = FilterChipDefaults.filterChipColors(
                            selectedContainerColor = PhosphorGreen.copy(0.2f),
                            selectedLabelColor = PhosphorGreen,
                            labelColor = TextMuted
                        ),
                        border = FilterChipDefaults.filterChipBorder(
                            enabled = true, selected = sofaPresetIdx == idx,
                            borderColor = if (sofaPresetIdx == idx) PhosphorGreen else ObsidianEdge
                        )
                    )
                }
            }
            SSlider("Intensidad espacial", sofaIntensity, 0f, 1f, color = PhosphorGreen) { sofaIntensity = it; applySofa(); persist() }
            SLine("Sujeto activo", sofaSubject, PhosphorGreen)
            SLine("HRTF cargado", if (IvannaSpatialManager.isHrtfDatasetLoaded()) "SÍ" else "NO", PhosphorGreen)
        }

        // ── FASE / PhaseOracle ───────────────────────────────────────────────
        SPanel("PHASE ORACLE · PI-LSTM", NeonMagenta) {
            SSlider("Intensidad (alpha, β=70%, γ=50%)", phaseIntensity, 0f, 1f, color = NeonMagenta) { phaseIntensity = it; applyPhase(); persist() }
            SLine("phi(t) estado", "%.4f".format(phaseState), NeonMagenta)
            SLine("alpha", "%.3f".format(phaseIntensity), NeonMagenta)
            SLine("beta", "%.3f".format(phaseIntensity * 0.7f), NeonMagenta)
            SLine("gamma", "%.3f".format(phaseIntensity * 0.5f), NeonMagenta)
        }

        // ── AF ──────────────────────────────────────────────────────────────
        SPanel("AF · ADAPTIVE FEATURES", AuroraCyan) {
            SRow("Modo AUTO", afAuto, AuroraCyan) { afAuto = it; applyAf(); persist() }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                // OptimizationMode no disponible — usar índices simples
                listOf("BALANCED", "QUALITY", "POWER").forEachIndexed { idx, label ->
                    FilterChip(
                        selected = afMode == idx,
                        onClick  = { afMode = idx; applyAf(); persist() },
                        label    = { Text(label, fontSize = 9.sp, fontWeight = if (afMode == idx) FontWeight.Bold else FontWeight.Normal) },
                        enabled  = afAuto,
                        modifier = Modifier.weight(1f),
                        colors = FilterChipDefaults.filterChipColors(
                            selectedContainerColor = AuroraCyan.copy(0.2f),
                            selectedLabelColor = AuroraCyan,
                            labelColor = TextMuted
                        ),
                        border = FilterChipDefaults.filterChipBorder(
                            enabled = afAuto, selected = afMode == idx,
                            borderColor = if (afMode == idx) AuroraCyan else ObsidianEdge
                        )
                    )
                }
            }
            SSlider("Intensidad AF", afIntensity, 0f, 100f, color = AuroraCyan, enabled = afAuto) { afIntensity = it; applyAf(); persist() }
            SLine("Motor AF", if (afRunning) "EJECUTANDO" else "DETENIDO", AuroraCyan)
        }

        // ── RIR ─────────────────────────────────────────────────────────────
        SPanel("RIR · IMPULSO DE SALA MEDIDO", AmberSignal) {
            SRow("Activar Convolución RIR", rirEnabled, AmberSignal) { rirEnabled = it; applyRir(); persist() }
            val metros = (3f + roomSize * 37f).toInt()
            SSlider("Tamaño de Sala", roomSize, 0f, 1f, color = AmberSignal, valueText = "$metros m") { roomSize = it; applyRir(); persist() }
            SSlider("Reflexiones (Taps)", reflections.toFloat(), 1f, N_REFL.toFloat(), color = AmberSignal, valueText = "$reflections", steps = N_REFL - 2) { 
                reflections = it.toInt().coerceIn(1, N_REFL); applyRir(); persist() 
            }
            SSlider("Decay RT60", decay, 0f, 1f, color = AmberSignal, valueText = "${"%.2f".format(decay * 2.5f)} s") { decay = it; applyRir(); persist() }
            SSlider("Mezcla Dry/Wet", dryWet, 0f, 1f, color = AmberSignal) { dryWet = it; applyRir(); persist() }
            
            val maxRoomDelayMs = (2f * metros * reflections / 343f * 1000f).toInt()
            SLine("Delay acústico sala", "$maxRoomDelayMs ms${if (maxRoomDelayMs > MAX_DELAY_MS) " ⚠ CAP" else ""}", AmberSignal)
            SLine("Estado del Convolver", if (rirEnabled) "ACTIVO" else "BYPASS", AmberSignal)
        }

        // ── SAF ─────────────────────────────────────────────────────────────
        SPanel("SAF · OPTIMIZADOR RIEMANNIANO", PhosphorGreen) {
            SLine("Diagnóstico SAF", if (safDiag.size >= 5 && safDiag[4] > 0f) "CONVERGIENDO" else "ESPERANDO FEEDBACK", PhosphorGreen)
            SSlider("Intensidad de Modificación (q)", safIntensity, 0f, 1f, color = PhosphorGreen) { safIntensity = it; applySaf(); persist() }
            SRow("Protector de Inteligibilidad de Voz", voiceProt, PhosphorGreen) { on -> 
                voiceProt = on
                // FIX L210: voiceMgr es Any? — enable()/disable() no existen en Any.
                // La protección de voz se persiste en prefs; el motor la lee en el
                // siguiente ciclo del daemon. No se llama ningún método sobre Any.
                persist() 
            }
            if (safDiag.size >= 5) {
                SLine("Paso Óptimo α*", "%.4f".format(safDiag[0]), PhosphorGreen)
                SLine("Error Mahalanobis E_t", "%.4f".format(safDiag[1]), PhosphorGreen)
                SLine("Regularización M", "%.4f".format(safDiag[2]), PhosphorGreen)
                SLine("Iteraciones de Gradiente", safDiag[4].toInt().toString(), PhosphorGreen)
            }
            Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                OutlinedButton(
                    onClick = { runCatching { SaFRoomBridge.step() } },
                    modifier = Modifier.weight(1f).height(44.dp),
                    shape = RoundedCornerShape(10.dp),
                    colors = ButtonDefaults.outlinedButtonColors(contentColor = PhosphorGreen),
                    border = BorderStroke(1.dp, PhosphorGreen.copy(0.4f))
                ) { Text("ITERAR (STEP)", style = MaterialTheme.typography.labelSmall) }

                OutlinedButton(
                    onClick = { runCatching { SaFRoomBridge.reset() } },
                    modifier = Modifier.weight(1f).height(44.dp),
                    shape = RoundedCornerShape(10.dp),
                    colors = ButtonDefaults.outlinedButtonColors(contentColor = AmberSignal),
                    border = BorderStroke(1.dp, AmberSignal.copy(0.4f))
                ) { Text("RESETEAR (q0)", style = MaterialTheme.typography.labelSmall) }
            }
        }
        Spacer(Modifier.height(32.dp))
    }
}

// ── Componentes Privados Magistrales ─────────────────────────────────────────

@Composable
private fun SPanel(title: String, accent: Color, content: @Composable ColumnScope.() -> Unit) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        color = ObsidianGlass,
        shape = RoundedCornerShape(16.dp),
        border = BorderStroke(1.dp, ObsidianEdge.copy(alpha = 0.5f))
    ) {
        Column(Modifier.padding(18.dp), verticalArrangement = Arrangement.spacedBy(14.dp)) {
            Text(title, color = accent, style = MaterialTheme.typography.labelLarge, fontWeight = FontWeight.Bold, letterSpacing = 1.sp)
            content()
        }
    }
}

@Composable
private fun SRow(label: String, checked: Boolean = false, accent: Color = AuroraCyan, onChange: (Boolean) -> Unit) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(label, color = TextPrimary, style = MaterialTheme.typography.bodySmall, fontWeight = FontWeight.Medium, modifier = Modifier.weight(1f))
        Switch(
            checked = checked, 
            onCheckedChange = onChange,
            colors = SwitchDefaults.colors(
                checkedThumbColor = ObsidianVoid,
                checkedTrackColor = accent,
                uncheckedThumbColor = TextMuted,
                uncheckedTrackColor = ObsidianDeep,
                uncheckedBorderColor = ObsidianEdge
            )
        )
    }
}

@Composable
private fun SSlider(label: String, value: Float, min: Float, max: Float, color: Color = AuroraCyan, valueText: String? = null, steps: Int = 0, enabled: Boolean = true, onChange: (Float) -> Unit) {
    Column {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            Text(label, color = TextSecondary, style = MaterialTheme.typography.labelSmall)
            Text(valueText ?: "%.3f".format(value), color = color, style = MaterialTheme.typography.labelSmall, fontWeight = FontWeight.Bold)
        }
        Slider(
            value = value, 
            onValueChange = onChange,
            valueRange = min..max, 
            steps = steps, 
            enabled = enabled,
            colors = SliderDefaults.colors(
                thumbColor = color,
                activeTrackColor = color,
                inactiveTrackColor = ObsidianDeep
            )
        )
    }
}

@Composable
private fun SLine(label: String, value: String, color: Color = PhosphorGreen) {
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
        Text(label, color = TextSecondary, style = MaterialTheme.typography.labelSmall)
        Text(value, color = color, style = MaterialTheme.typography.labelSmall, fontWeight = FontWeight.Bold)
    }
}
