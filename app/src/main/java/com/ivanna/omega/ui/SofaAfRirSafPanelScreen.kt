package com.ivanna.omega.ui

import android.content.Context
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.ivanna.omega.audio.AdaptiveMode
import com.ivanna.omega.audio.VoiceProtectionManager
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.saf.SaFBridge
import com.ivanna.omega.saf.SaFRoomBridge
import com.ivanna.omega.spatial.IvannaSpatialManager
import com.ivanna.omega.spatial.SpatialControlStore
import com.ivanna.omega.ui.theme.*
import kotlinx.coroutines.delay

// ── Constantes C++ (pi_lstm_milenio.hpp) ─────────────────────────────────
private const val N_REFL        = 8
private const val FS_BASE       = 96000f
private const val MAX_DELAY_SMP = 4096f
private val MAX_DELAY_MS        = (MAX_DELAY_SMP / FS_BASE) * 1000f  // 42.6 ms

// ── Mapa SOFA — IDs reales que carga nativeObjectRendererSetHrtfSubject ──
// Fuente: HrtfSubjectSelector.AVAILABLE_SUBJECTS + assets/saf/sofa_elite/
private val SOFA_PRESETS = listOf(
    "MIT KEMAR"      to "kemar",
    "KEMAR Large"    to "kemar_large",
    "TU-Berlin KEMAR" to "tu_berlin_kemar",
    "CIPIC 165"      to "cipic_165",
    "Pulse"          to "pulse"
)

private const val PREFS = "ivanna_sofa_af_rir_saf_v3"

private data class PanelState(
    val sofaEnabled:   Boolean = true,
    val sofaPresetIdx: Int     = 0,
    val sofaIntensity: Float   = 0.5f,
    val afAuto:        Boolean = false,   // FIX: false por defecto, usuario activa
    val afModeOrd:     Int     = 0,       // OFF
    val afIntensity:   Float   = 0f,
    val rirEnabled:    Boolean = false,
    val roomSize:      Float   = 0.3f,
    val reflections:   Int     = 3,
    val decay:         Float   = 0.3f,
    val dryWet:        Float   = 0.2f,
    val safIntensity:  Float   = 0.5f,
    val voiceProt:     Boolean = false,
    val phaseIntensity: Float  = 0f
)

private fun load(ctx: Context): PanelState {
    val p = ctx.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
    val d = PanelState()
    return PanelState(
        sofaEnabled    = p.getBoolean("sofaEnabled",    d.sofaEnabled),
        sofaPresetIdx  = p.getInt    ("sofaPresetIdx",  d.sofaPresetIdx),
        sofaIntensity  = p.getFloat  ("sofaIntensity",  d.sofaIntensity),
        afAuto         = p.getBoolean("afAuto",         d.afAuto),
        afModeOrd      = p.getInt    ("afModeOrd",      d.afModeOrd),
        afIntensity    = p.getFloat  ("afIntensity",    d.afIntensity),
        rirEnabled     = p.getBoolean("rirEnabled",     d.rirEnabled),
        roomSize       = p.getFloat  ("roomSize",       d.roomSize),
        reflections    = p.getInt    ("reflections",    d.reflections),
        decay          = p.getFloat  ("decay",          d.decay),
        dryWet         = p.getFloat  ("dryWet",         d.dryWet),
        safIntensity   = p.getFloat  ("safIntensity",   d.safIntensity),
        voiceProt      = p.getBoolean("voiceProt",      d.voiceProt),
        phaseIntensity = p.getFloat  ("phaseIntensity", d.phaseIntensity)
    )
}

private fun save(ctx: Context, s: PanelState) {
    ctx.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
        .putBoolean("sofaEnabled",    s.sofaEnabled)
        .putInt    ("sofaPresetIdx",  s.sofaPresetIdx)
        .putFloat  ("sofaIntensity",  s.sofaIntensity)
        .putBoolean("afAuto",         s.afAuto)
        .putInt    ("afModeOrd",      s.afModeOrd)
        .putFloat  ("afIntensity",    s.afIntensity)
        .putBoolean("rirEnabled",     s.rirEnabled)
        .putFloat  ("roomSize",       s.roomSize)
        .putInt    ("reflections",    s.reflections)
        .putFloat  ("decay",          s.decay)
        .putFloat  ("dryWet",         s.dryWet)
        .putFloat  ("safIntensity",   s.safIntensity)
        .putBoolean("voiceProt",      s.voiceProt)
        .putFloat  ("phaseIntensity", s.phaseIntensity)
        .apply()
}

@Composable
fun SofaAfRirSafPanelScreen(
    onBack: () -> Unit = {},
    voiceMgr: VoiceProtectionManager? = null
) {
    val ctx    = LocalContext.current
    val loaded = IvannaNativeLib.isLoaded
    val init   = remember { load(ctx) }
    val modes  = AdaptiveMode.entries.toList()

    // ── Estado ──────────────────────────────────────────────────────────────
    var sofaEnabled    by remember { mutableStateOf(init.sofaEnabled) }
    var sofaPresetIdx  by remember { mutableStateOf(init.sofaPresetIdx.coerceIn(0, SOFA_PRESETS.lastIndex)) }
    var sofaIntensity  by remember { mutableStateOf(init.sofaIntensity) }
    var afAuto         by remember { mutableStateOf(init.afAuto) }
    var afMode         by remember { mutableStateOf(modes.getOrElse(init.afModeOrd) { AdaptiveMode.OFF }) }
    var afIntensity    by remember { mutableStateOf(init.afIntensity) }
    var rirEnabled     by remember { mutableStateOf(init.rirEnabled) }
    var roomSize       by remember { mutableStateOf(init.roomSize) }
    var reflections    by remember { mutableStateOf(init.reflections) }
    var decay          by remember { mutableStateOf(init.decay) }
    var dryWet         by remember { mutableStateOf(init.dryWet) }
    var safIntensity   by remember { mutableStateOf(init.safIntensity) }
    var voiceProt      by remember { mutableStateOf(voiceMgr?.isActive() ?: init.voiceProt) }
    var phaseIntensity by remember { mutableStateOf(init.phaseIntensity) }

    // ── Telemetria ───────────────────────────────────────────────────────────
    var afRunning   by remember { mutableStateOf(false) }
    var afTelemetry by remember { mutableStateOf<FloatArray?>(null) }
    var safDiag     by remember { mutableStateOf(FloatArray(5)) }
    var phaseState  by remember { mutableStateOf(0f) }
    var sofaSubject by remember { mutableStateOf(IvannaSpatialManager.activeSubject) }

    LaunchedEffect(loaded) {
        while (loaded) {
            runCatching {
                afRunning   = IvannaNativeLib.nativeIsAdaptiveEngineRunning()
                afTelemetry = IvannaNativeLib.nativeGetAdaptiveTelemetry()
                safDiag     = SaFRoomBridge.getDiagnostics()
                phaseState  = IvannaNativeLib.nativeGetPhaseState()
                sofaSubject = IvannaSpatialManager.activeSubject
            }
            delay(300)
        }
    }

    fun persist() = save(ctx, PanelState(
        sofaEnabled, sofaPresetIdx, sofaIntensity,
        afAuto, afMode.ordinal, afIntensity,
        rirEnabled, roomSize, reflections, decay, dryWet,
        safIntensity, voiceProt, phaseIntensity
    ))

    // ── Aplicadores ─────────────────────────────────────────────────────────

    fun applySofa() {
        if (!loaded) return
        runCatching {
            IvannaNativeLib.nativeSetHRTFEnabled(sofaEnabled)
            IvannaNativeLib.nativeSetSpatialWet(if (sofaEnabled) sofaIntensity else 0f)
            IvannaNativeLib.nativeSetSpatialWidthDirect(
                if (sofaEnabled) 0.5f + sofaIntensity * 0.5f else 0.5f)
            if (sofaEnabled) {
                // Usar el ID real del dataset embarcado
                val subjectId = SOFA_PRESETS.getOrNull(sofaPresetIdx)?.second ?: "kemar"
                IvannaSpatialManager.setHrtfSubject(subjectId)
                // Hint para calibracion SAF: idx del sujeto en tabla de 214
                SaFBridge.setSubjectIndexHint(sofaPresetIdx)
                // Sincronizar SpatialControlStore para que otros modulos lean
                val sc = SpatialControlStore.load(ctx)
                SpatialControlStore.save(ctx, sc.copy(
                    hrtfEnabled = sofaEnabled,
                    hrtfSubject = SOFA_PRESETS.getOrNull(sofaPresetIdx)?.first ?: "MIT KEMAR"
                ))
            }
        }
    }

    fun applyAf() {
        if (!loaded) return
        runCatching {
            IvannaNativeLib.nativeSetAdaptEnabled(afAuto)
            IvannaNativeLib.nativeSetAdaptiveEngineEnabled(afAuto)
            if (afAuto)
                IvannaNativeLib.nativeSetAdaptiveControls(afMode.ordinal, afIntensity)
            else
                IvannaNativeLib.nativeSetAdaptiveControls(AdaptiveMode.OFF.ordinal, 0f)
        }
    }

    fun applyRir() {
        if (!loaded) return
        val meters = 3f + roomSize * 37f
        val rt60   = decay * 2.5f
        runCatching {
            for (i in 0 until N_REFL) {
                val active = rirEnabled && i < reflections
                // FIX unidades: ratio = segundos (FS_BASE = 96000 Hz)
                val delayMs = if (active)
                    ((2f * meters * (i + 1) / 343f) * 1000f).coerceAtMost(MAX_DELAY_MS)
                    else 0f
                val delayRatio = delayMs / 1000f
                val gain = if (active)
                    dryWet * Math.pow(0.9, ((i + 1) * (1.6f - decay)).toDouble()).toFloat()
                    else 0f
                IvannaNativeLib.nativeSetReflectionDelay(i, delayRatio)
                IvannaNativeLib.nativeSetReflectionGain(i, gain)
            }
            // FIX: drr=0 cuando desactivado para no modular respuesta espacial
            if (rirEnabled) {
                val drr = 5f - roomSize * 10f - decay * 5f
                SaFRoomBridge.setRoomState(rt60 = rt60, drr = drr, roomMode = roomSize)
                // Informar al optimizador SAF-Room el estado real de la sala
                SaFRoomBridge.setSoundFieldState(
                    diffuseness = dryWet,
                    complexity  = roomSize * decay
                )
            } else {
                SaFRoomBridge.setRoomState(rt60 = 0f, drr = 0f, roomMode = 0f)
                SaFRoomBridge.setSoundFieldState(diffuseness = 0f, complexity = 0f)
            }
            SpatialControlStore.save(ctx, SpatialControlStore.load(ctx).copy(
                rirEnabled = rirEnabled,
                rirWet     = dryWet
            ))
        }
    }

    fun applySaf() {
        if (!loaded) return
        runCatching {
            IvannaNativeLib.nativeSetFatigueProtection(safIntensity, safIntensity)
            // Informar mismatch al optimizador: 0 cuando no hay calibracion activa
            SaFRoomBridge.setHrtfState(
                mismatchEnergy  = if (sofaEnabled) safIntensity * 0.1f else 0f,
                convergenceRate = if (safDiag.size >= 5 && safDiag[4] > 0f) 0.05f else 1f
            )
        }
    }

    fun applyPhase() {
        if (!loaded) return
        runCatching {
            IvannaNativeLib.nativeSetPhaseParameters(
                phaseIntensity,
                phaseIntensity * 0.7f,
                phaseIntensity * 0.5f
            )
        }
    }

    // Aplicar estado restaurado (no ciegos: solo si hay motivo de cambio)
    LaunchedEffect(Unit) {
        applySofa(); applyAf(); applyRir(); applySaf(); applyPhase()
    }

    // ── UI ──────────────────────────────────────────────────────────────────
    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(ObsidianVoid)
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 16.dp, vertical = 20.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp)
    ) {
        // Header
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text("SOFA · AF · RIR · SAF",
                    color = AuroraCyan,
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.Bold)
                Text(if (loaded) "Motor activo" else "Motor no cargado",
                    color = if (loaded) PhosphorGreen else AmberSignal,
                    style = MaterialTheme.typography.labelSmall)
            }
            TextButton(onClick = onBack) { Text("VOLVER", color = TextMuted) }
        }

        // ── SOFA ────────────────────────────────────────────────────────────
        SPanel("SOFA · HRTF", AuroraCyan) {
            SRow("Activar HRTF", sofaEnabled) {
                sofaEnabled = it; applySofa(); persist()
            }
            Text("Dataset SOFA embarcado", color = TextMuted,
                style = MaterialTheme.typography.labelSmall)
            // Chips con IDs reales
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp),
                modifier = Modifier.fillMaxWidth()) {
                SOFA_PRESETS.forEachIndexed { idx, (label, _) ->
                    FilterChip(
                        selected = sofaPresetIdx == idx,
                        onClick  = {
                            sofaPresetIdx = idx
                            applySofa(); persist()
                        },
                        label = { Text(label, style = MaterialTheme.typography.labelSmall) },
                        modifier = Modifier.weight(1f)
                    )
                }
            }
            SSlider("Intensidad espacial", sofaIntensity, 0f, 1f) {
                sofaIntensity = it; applySofa(); persist()
            }
            SLine("Sujeto activo", sofaSubject)
            SLine("HRTF cargado",
                if (IvannaSpatialManager.isHrtfDatasetLoaded()) "SI" else "NO")
        }

        // ── FASE / PhaseOracle ───────────────────────────────────────────────
        SPanel("PHASE ORACLE · Corrección de desfase", NeonMagenta) {
            SSlider(
                "Intensidad corrección (alpha, beta=70%, gamma=50%)",
                phaseIntensity, 0f, 1f
            ) { phaseIntensity = it; applyPhase(); persist() }
            SLine("phi(t) estado",   "%.4f".format(phaseState))
            SLine("alpha",           "%.3f".format(phaseIntensity))
            SLine("beta",            "%.3f".format(phaseIntensity * 0.7f))
            SLine("gamma",           "%.3f".format(phaseIntensity * 0.5f))
            Text("0 = sin corrección de fase (safe). >0 aplica oracle PI-LSTM.",
                color = TextMuted, style = MaterialTheme.typography.labelSmall)
        }

        // ── AF ──────────────────────────────────────────────────────────────
        SPanel("AF · ADAPTIVE FEATURES", AuroraCyan) {
            SRow(if (afAuto) "Modo AUTO activo" else "Modo MANUAL / OFF") {
                afAuto = it; applyAf(); persist()
            }
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                modes.forEach { m ->
                    FilterChip(
                        selected = afMode == m,
                        onClick  = { afMode = m; applyAf(); persist() },
                        label    = { Text(m.label, style = MaterialTheme.typography.labelSmall) },
                        enabled  = afAuto
                    )
                }
            }
            SSlider("Intensidad", afIntensity, 0f, 100f,
                enabled = afAuto) { afIntensity = it; applyAf(); persist() }
            SLine("Motor", if (afRunning) "EN EJECUCIÓN" else "DETENIDO")
            afTelemetry?.takeIf { it.size >= 8 }?.let { t ->
                SLine("target_gain",     "%.3f".format(t[3]))
                SLine("compresor",       "%.3f".format(t[4]))
                SLine("ancho_espacial",  "%.3f".format(t[6]))
                SLine("margen_seg",      "%.3f".format(t[7]))
            }
        }

        // ── RIR ─────────────────────────────────────────────────────────────
        SPanel("RIR · SALA / REFLEXIONES", PhosphorGreen) {
            SRow("Activar RIR", rirEnabled) {
                rirEnabled = it; applyRir(); persist()
            }
            val metros = (3f + roomSize * 37f).toInt()
            SSlider("Sala", roomSize, 0f, 1f, "$metros m") {
                roomSize = it; applyRir(); persist()
            }
            SSlider("Reflexiones", reflections.toFloat(), 1f, N_REFL.toFloat(),
                "$reflections", steps = N_REFL - 2) {
                reflections = it.toInt().coerceIn(1, N_REFL); applyRir(); persist()
            }
            SSlider("Decay RT60", decay, 0f, 1f,
                "${"%.2f".format(decay * 2.5f)} s") {
                decay = it; applyRir(); persist()
            }
            SSlider("Dry/Wet", dryWet, 0f, 1f) {
                dryWet = it; applyRir(); persist()
            }
            val maxRoomDelayMs = (2f * metros * reflections / 343f * 1000f).toInt()
            SLine("Delay cap útil", "${MAX_DELAY_MS.toInt()} ms")
            SLine("Delay sala",
                "$maxRoomDelayMs ms${if (maxRoomDelayMs > MAX_DELAY_MS) " ⚠cap" else ""}")
            SLine("Estado", if (rirEnabled) "ACTIVO" else "BYPASS")
        }

        // ── SAF ─────────────────────────────────────────────────────────────
        SPanel("SAF · Φ_SAF-Room^∞", AmberSignal) {
            SLine("Sistema",
                if (safDiag.size >= 5 && safDiag[4] > 0f) "CONVERGIENDO" else "EN ESPERA")
            SSlider("Intensidad protección", safIntensity, 0f, 1f) {
                safIntensity = it; applySaf(); persist()
            }
            SRow("Protección de voz", voiceProt) { on ->
                voiceProt = on
                runCatching { if (on) voiceMgr?.enable() else voiceMgr?.disable() }
                persist()
            }
            if (safDiag.size >= 5) {
                SLine("α* paso óptimo",     "%.4f".format(safDiag[0]))
                SLine("E_t error",           "%.4f".format(safDiag[1]))
                SLine("λ_t regularización",  "%.4f".format(safDiag[2]))
                SLine("σ acoplamiento",      "%.4f".format(safDiag[3]))
                SLine("Iteraciones",         safDiag[4].toInt().toString())
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedButton(onClick = {
                    runCatching { SaFRoomBridge.step() }
                }) { Text("STEP", style = MaterialTheme.typography.labelSmall) }
                OutlinedButton(onClick = {
                    runCatching { SaFRoomBridge.reset() }
                }) { Text("RESET", style = MaterialTheme.typography.labelSmall) }
            }
        }

        Spacer(Modifier.height(32.dp))
    }
}

// ── Componentes privados ─────────────────────────────────────────────────────
@Composable
private fun SPanel(title: String, accent: Color, content: @Composable ColumnScope.() -> Unit) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        color    = ObsidianSoft,
        shape    = MaterialTheme.shapes.medium,
        border   = BorderStroke(1.dp, accent.copy(alpha = 0.30f))
    ) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Text(title, color = accent,
                style = MaterialTheme.typography.titleSmall,
                fontWeight = FontWeight.Bold)
            content()
        }
    }
}

@Composable
private fun SRow(label: String, checked: Boolean = false, onChange: (Boolean) -> Unit) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(label, color = TextMuted,
            style = MaterialTheme.typography.bodySmall,
            modifier = Modifier.weight(1f))
        Switch(checked = checked, onCheckedChange = onChange)
    }
}

@Composable
private fun SSlider(
    label: String, value: Float, min: Float, max: Float,
    valueText: String? = null, steps: Int = 0, enabled: Boolean = true,
    onChange: (Float) -> Unit
) {
    Column {
        Row {
            Text(label, color = TextMuted,
                style = MaterialTheme.typography.labelSmall,
                modifier = Modifier.weight(1f))
            Text(valueText ?: "%.3f".format(value),
                color = AuroraCyan,
                style = MaterialTheme.typography.labelSmall)
        }
        Slider(value = value, onValueChange = onChange,
            valueRange = min..max, steps = steps, enabled = enabled)
    }
}

@Composable
private fun SLine(label: String, value: String) {
    Row {
        Text(label, color = TextMuted,
            style = MaterialTheme.typography.labelSmall,
            modifier = Modifier.weight(1f))
        Text(value, color = PhosphorGreen,
            style = MaterialTheme.typography.labelSmall)
    }
}
