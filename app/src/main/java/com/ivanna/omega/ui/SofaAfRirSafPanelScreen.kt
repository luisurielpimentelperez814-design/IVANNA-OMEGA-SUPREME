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
import com.ivanna.omega.ui.theme.*
import kotlinx.coroutines.delay
import kotlin.math.pow

/**
 * SofaAfRirSafPanelScreen v2 — panel unificado SOFA · AF · RIR · SAF.
 *
 * FIXES v2:
 *   BUG 1 (ruido + armónica): nativeSetReflectionDelay esperaba ratio [0..1]
 *     (d * FS_BASE=96000 → samples), pero recibía milisegundos directamente.
 *     Cualquier delay > 1 ms se clampeaba a 1.0 → 96000 samples → alias de
 *     buffer (96000 % MAX_DELAY=4096 = 1792 fijos para todos) = comb filter
 *     y resonancias armónicas. FIX: convertir ms → ratio = ms/1000.
 *     Además cap en MAX_DELAY_MS=(4096/96000)*1000≈42.6ms para evitar aliasing.
 *   BUG 2 (ruido): SaFRoomBridge.setRoomState enviaba DRR≠0 cuando RIR
 *     estaba desactivado. FIX: drr=0f cuando rirEnabled=false.
 *   BUG 3 (persistencia): todo usaba remember{mutableStateOf(default)} —
 *     cada apertura de pantalla reseteaba parámetros al default.
 *     FIX: SharedPreferences "ivanna_sofa_af_rir_saf" carga en init y guarda
 *     en cada cambio.
 */

// ── Constantes C++ (pi_lstm_milenio.hpp) ────────────────────────────────────
private const val N_REFL         = 8
private const val FS_BASE        = 96000f   // escala de delays en C++
private const val MAX_DELAY_SMP  = 4096f    // tamaño del ring buffer
// Delay máximo que no produce aliasing de buffer
private val MAX_DELAY_MS         = (MAX_DELAY_SMP / FS_BASE) * 1000f  // ≈ 42.6 ms

private const val PREFS_NAME = "ivanna_sofa_af_rir_saf"

// ── Prefs helper ─────────────────────────────────────────────────────────────
private data class PanelState(
    val sofaEnabled:  Boolean = true,
    val sofaPreset:   Int     = 0,
    val sofaIntensity: Float  = 0.5f,
    val afAuto:       Boolean = true,
    val afModeOrd:    Int     = AdaptiveMode.NATURAL.ordinal,
    val afIntensity:  Float   = 50f,
    val rirEnabled:   Boolean = false,
    val roomSize:     Float   = 0.4f,
    val reflections:  Int     = 4,
    val decay:        Float   = 0.35f,
    val dryWet:       Float   = 0.25f,
    val safProtection: Float  = 0.5f,
    val voiceProt:    Boolean = false
)

private fun loadState(ctx: Context): PanelState {
    val p = ctx.applicationContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
    val d = PanelState()
    return PanelState(
        sofaEnabled   = p.getBoolean("sofaEnabled",   d.sofaEnabled),
        sofaPreset    = p.getInt    ("sofaPreset",    d.sofaPreset),
        sofaIntensity = p.getFloat  ("sofaIntensity", d.sofaIntensity),
        afAuto        = p.getBoolean("afAuto",        d.afAuto),
        afModeOrd     = p.getInt    ("afModeOrd",     d.afModeOrd),
        afIntensity   = p.getFloat  ("afIntensity",   d.afIntensity),
        rirEnabled    = p.getBoolean("rirEnabled",    d.rirEnabled),
        roomSize      = p.getFloat  ("roomSize",      d.roomSize),
        reflections   = p.getInt    ("reflections",   d.reflections),
        decay         = p.getFloat  ("decay",         d.decay),
        dryWet        = p.getFloat  ("dryWet",        d.dryWet),
        safProtection = p.getFloat  ("safProtection", d.safProtection),
        voiceProt     = p.getBoolean("voiceProt",     d.voiceProt)
    )
}

private fun saveState(ctx: Context, s: PanelState) {
    ctx.applicationContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        .edit()
        .putBoolean("sofaEnabled",   s.sofaEnabled)
        .putInt    ("sofaPreset",    s.sofaPreset)
        .putFloat  ("sofaIntensity", s.sofaIntensity)
        .putBoolean("afAuto",        s.afAuto)
        .putInt    ("afModeOrd",     s.afModeOrd)
        .putFloat  ("afIntensity",   s.afIntensity)
        .putBoolean("rirEnabled",    s.rirEnabled)
        .putFloat  ("roomSize",      s.roomSize)
        .putInt    ("reflections",   s.reflections)
        .putFloat  ("decay",         s.decay)
        .putFloat  ("dryWet",        s.dryWet)
        .putFloat  ("safProtection", s.safProtection)
        .putBoolean("voiceProt",     s.voiceProt)
        .apply()
}

// ── Screen ───────────────────────────────────────────────────────────────────
@Composable
fun SofaAfRirSafPanelScreen(
    onBack: () -> Unit = {},
    voiceMgr: VoiceProtectionManager? = null
) {
    val ctx    = LocalContext.current
    val loaded = IvannaNativeLib.isLoaded

    // Cargar estado persistido (una vez al entrar)
    val init = remember { loadState(ctx) }

    // ── SOFA ──────────────────────────────────────────────────────────────
    var sofaEnabled   by remember { mutableStateOf(init.sofaEnabled) }
    var sofaPreset    by remember { mutableStateOf(init.sofaPreset) }
    var sofaIntensity by remember { mutableStateOf(init.sofaIntensity) }

    // ── AF ────────────────────────────────────────────────────────────────
    val modeValues = AdaptiveMode.entries.toList()
    var afAuto      by remember { mutableStateOf(init.afAuto) }
    var afMode      by remember { mutableStateOf(
        modeValues.getOrNull(init.afModeOrd) ?: AdaptiveMode.NATURAL) }
    var afIntensity by remember { mutableStateOf(init.afIntensity) }
    var afRunning   by remember { mutableStateOf(false) }
    var afTelemetry by remember { mutableStateOf<FloatArray?>(null) }

    // ── RIR ───────────────────────────────────────────────────────────────
    var rirEnabled  by remember { mutableStateOf(init.rirEnabled) }
    var roomSize    by remember { mutableStateOf(init.roomSize) }   // 0..1 → 3..40 m
    var reflections by remember { mutableStateOf(init.reflections) }
    var decay       by remember { mutableStateOf(init.decay) }      // 0..1 → RT60 0..2.5 s
    var dryWet      by remember { mutableStateOf(init.dryWet) }

    // ── SAF ───────────────────────────────────────────────────────────────
    var safProtection by remember { mutableStateOf(init.safProtection) }
    var voiceProt     by remember { mutableStateOf(
        if (voiceMgr != null) voiceMgr.isActive() else init.voiceProt) }
    var safDiag  by remember { mutableStateOf(FloatArray(5)) }
    var safParams by remember { mutableStateOf(FloatArray(7)) }

    // ── Helper de persistencia ──────────────────────────────────────────────
    fun persist() = saveState(ctx, PanelState(
        sofaEnabled, sofaPreset, sofaIntensity,
        afAuto, afMode.ordinal, afIntensity,
        rirEnabled, roomSize, reflections, decay, dryWet,
        safProtection, voiceProt
    ))

    // ── Telemetría viva ────────────────────────────────────────────────────
    LaunchedEffect(loaded) {
        while (loaded) {
            runCatching {
                afRunning   = IvannaNativeLib.nativeIsAdaptiveEngineRunning()
                afTelemetry = IvannaNativeLib.nativeGetAdaptiveTelemetry()
                safDiag     = SaFRoomBridge.getDiagnostics()
                safParams   = SaFRoomBridge.getParams()
            }
            delay(250)
        }
    }

    // ── Aplicadores — APIs existentes, sin tocar DSP C++ ──────────────────
    fun applySofa() {
        if (!loaded) return
        runCatching {
            IvannaNativeLib.nativeSetHRTFEnabled(sofaEnabled)
            IvannaNativeLib.nativeSetSpatialWet(if (sofaEnabled) sofaIntensity else 0f)
            IvannaNativeLib.nativeSetSpatialWidthDirect(
                if (sofaEnabled) 0.5f + sofaIntensity else 0.5f)
        }
    }

    fun applyAf() {
        if (!loaded) return
        runCatching {
            IvannaNativeLib.nativeSetAdaptEnabled(afAuto)
            IvannaNativeLib.nativeSetAdaptiveEngineEnabled(afAuto)
            IvannaNativeLib.nativeSetAdaptiveControls(afMode.ordinal, afIntensity)
        }
    }

    fun applyRir() {
        if (!loaded) return
        val meters = 3f + roomSize * 37f
        val rt60   = decay * 2.5f

        // FIX BUG 1 — unidades de delay:
        //   C++: delays_smp[i] = clampf(d, 0, 1) * FS_BASE (96000)
        //   El código anterior pasaba delayMs directamente → clamp(100, 0, 1) = 1.0
        //   → 96000 samples → alias 96000 % 4096 = 1792 para todas las reflexiones
        //   → comb filter → ruido digital + armónicas.
        //   FIX: d = delayMs / 1000  (segundos ≡ ratio para FS_BASE=96000)
        //   Cap en MAX_DELAY_MS ≈ 42.6ms para no superar el buffer de 4096 muestras.
        runCatching {
            for (i in 0 until N_REFL) {
                val active = rirEnabled && i < reflections
                val delayMs = if (active)
                    ((2f * meters * (i + 1) / 343f) * 1000f).coerceAtMost(MAX_DELAY_MS)
                    else 0f
                // FIX: ratio = delayMs / 1000f (FS_BASE = 96000 Hz → 1s = ratio 1.0)
                val delayRatio = delayMs / 1000f
                val gain = if (active)
                    dryWet * 0.9f.pow((i + 1).toFloat() * (1.6f - decay))
                    else 0f
                IvannaNativeLib.nativeSetReflectionDelay(i, delayRatio)
                IvannaNativeLib.nativeSetReflectionGain(i, gain)
            }
            // FIX BUG 2 — DRR no-cero cuando RIR desactivado:
            //   Antes: drr = 15f - (roomSize*15) - (decay*10) enviado siempre.
            //   Con rirEnabled=false y drr≠0 el SaFRoomBridge seguía modulando
            //   la respuesta espacial. FIX: drr=0 y roomMode=0 cuando desactivado.
            if (rirEnabled) {
                val drr = 15f - (roomSize * 15f) - (decay * 10f)
                SaFRoomBridge.setRoomState(rt60 = rt60, drr = drr, roomMode = roomSize)
            } else {
                SaFRoomBridge.setRoomState(rt60 = 0f, drr = 0f, roomMode = 0f)
            }
        }
    }

    fun applySaf() {
        if (!loaded) return
        runCatching {
            IvannaNativeLib.nativeSetFatigueProtection(safProtection, safProtection)
        }
    }

    // Aplicar estado restaurado al motor al abrir pantalla (no ciegos: usa prefs)
    LaunchedEffect(Unit) {
        applySofa(); applyAf(); applyRir(); applySaf()
    }

    // ── UI ────────────────────────────────────────────────────────────────
    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(ObsidianVoid)
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 16.dp, vertical = 20.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp)
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text("SOFA · AF · RIR · SAF",
                    color = AuroraCyan,
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.Bold)
                Text(
                    if (loaded) "Motor cargado — estado persistido" else "Motor NO cargado",
                    color = if (loaded) PhosphorGreen else AmberSignal,
                    style = MaterialTheme.typography.labelSmall)
            }
            TextButton(onClick = onBack) { Text("VOLVER", color = TextMuted) }
        }

        // ── SOFA ──────────────────────────────────────────────────────────
        PanelSection("SOFA · HRTF", AuroraCyan) {
            ToggleRow("Activar HRTF/SOFA", sofaEnabled) {
                sofaEnabled = it; applySofa(); persist()
            }
            Text("Perfil / preset", color = TextMuted,
                style = MaterialTheme.typography.labelSmall)
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                SOFA_PRESETS.forEachIndexed { idx, label ->
                    FilterChip(
                        selected = sofaPreset == idx,
                        onClick  = {
                            sofaPreset = idx
                            runCatching {
                                SaFBridge.setSubjectIndexHint(idx)
                                IvannaSpatialManager.reloadHrtf(ctx)
                            }
                            applySofa(); persist()
                        },
                        label = { Text(label, style = MaterialTheme.typography.labelSmall) }
                    )
                }
            }
            SliderRow("Intensidad espacial", sofaIntensity, 0f, 1f) {
                sofaIntensity = it; applySofa(); persist()
            }
            StatusLine("Sujeto activo", IvannaSpatialManager.activeSubject)
            StatusLine("Estado", if (sofaEnabled) "ACTIVO" else "BYPASS")
        }

        // ── AF ────────────────────────────────────────────────────────────
        PanelSection("AF · ADAPTIVE FEATURES", NeonMagenta) {
            ToggleRow(if (afAuto) "Modo AUTO" else "Modo MANUAL", afAuto) {
                afAuto = it; applyAf(); persist()
            }
            Text("Perfil adaptativo", color = TextMuted,
                style = MaterialTheme.typography.labelSmall)
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                modeValues.forEach { m ->
                    FilterChip(
                        selected = afMode == m,
                        onClick  = { afMode = m; applyAf(); persist() },
                        label = { Text(m.label, style = MaterialTheme.typography.labelSmall) }
                    )
                }
            }
            SliderRow("Intensidad adaptativa", afIntensity, 0f, 100f) {
                afIntensity = it; applyAf(); persist()
            }
            StatusLine("Motor", if (afRunning) "EN EJECUCIÓN" else "DETENIDO")
            afTelemetry?.takeIf { it.size >= 9 }?.let { t ->
                StatusLine("Target gain",      fmt(t[3]))
                StatusLine("Compresor",         fmt(t[4]))
                StatusLine("Ancho espacial",    fmt(t[6]))
                StatusLine("Margen seguridad",  fmt(t[7]))
            }
        }

        // ── RIR ───────────────────────────────────────────────────────────
        PanelSection("RIR · SALA / REFLEXIONES", AuroraCyan) {
            ToggleRow("Activar RIR", rirEnabled) {
                rirEnabled = it; applyRir(); persist()
            }
            SliderRow("Tamaño de sala", roomSize, 0f, 1f,
                "${(3f + roomSize * 37f).toInt()} m") {
                roomSize = it; applyRir(); persist()
            }
            SliderRow("Reflexiones", reflections.toFloat(), 1f, N_REFL.toFloat(),
                "$reflections", steps = N_REFL - 2) {
                reflections = it.toInt().coerceIn(1, N_REFL); applyRir(); persist()
            }
            SliderRow("Decay (RT60)", decay, 0f, 1f, fmt(decay * 2.5f) + " s") {
                decay = it; applyRir(); persist()
            }
            SliderRow("Mezcla dry/wet", dryWet, 0f, 1f) {
                dryWet = it; applyRir(); persist()
            }
            val maxDelayRoomMs = (2f * (3f + roomSize * 37f) * reflections / 343f) * 1000f
            StatusLine("Delay máx útil", "${MAX_DELAY_MS.toInt()} ms")
            StatusLine("Delay sala actual", "${maxDelayRoomMs.toInt()} ms" +
                if (maxDelayRoomMs > MAX_DELAY_MS) " ⚠ cap" else "")
            StatusLine("Estado", if (rirEnabled) "ACTIVO" else "BYPASS")
        }

        // ── SAF ───────────────────────────────────────────────────────────
        PanelSection("SAF · Φ_SAF-Room^∞", PhosphorGreen) {
            StatusLine("Sistema",
                if (loaded && safDiag.size >= 5 && safDiag[4] > 0f)
                    "CONVERGIENDO" else "EN ESPERA")
            SliderRow("Intensidad protección", safProtection, 0f, 1f) {
                safProtection = it; applySaf(); persist()
            }
            ToggleRow("Protección de voz", voiceProt) { on ->
                voiceProt = on
                runCatching { if (on) voiceMgr?.enable() else voiceMgr?.disable() }
                persist()
            }
            if (safDiag.size >= 5) {
                StatusLine("α* (paso óptimo)",    fmt(safDiag[0]))
                StatusLine("E_t (error)",          fmt(safDiag[1]))
                StatusLine("λ_t (regularización)", fmt(safDiag[2]))
                StatusLine("σ (acoplamiento)",     fmt(safDiag[3]))
                StatusLine("Iteraciones",          safDiag[4].toInt().toString())
            }
            StatusLine("p_t", safParams.joinToString(" ") { fmt(it) })
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedButton(onClick = { runCatching { SaFRoomBridge.step() } }) {
                    Text("STEP") }
                OutlinedButton(onClick = { runCatching { SaFRoomBridge.reset() } }) {
                    Text("RESET") }
            }
        }

        Spacer(Modifier.height(24.dp))
    }
}

// ── Constantes y helpers privados ────────────────────────────────────────────
private val SOFA_PRESETS = listOf("NEUTRO", "AMPLIO", "CERCANO", "CINE")

private fun fmt(v: Float): String = "%.3f".format(v)

@Composable
private fun PanelSection(
    title: String, accent: Color,
    content: @Composable ColumnScope.() -> Unit
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        color    = ObsidianSoft,
        shape    = MaterialTheme.shapes.medium,
        border   = BorderStroke(1.dp, accent.copy(alpha = 0.30f))
    ) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            Text(title, color = accent,
                style = MaterialTheme.typography.titleSmall,
                fontWeight = FontWeight.Bold)
            content()
        }
    }
}

@Composable
private fun ToggleRow(label: String, checked: Boolean, onChange: (Boolean) -> Unit) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(label, color = TextMuted,
            style = MaterialTheme.typography.bodySmall,
            modifier = Modifier.weight(1f))
        Switch(checked = checked, onCheckedChange = onChange)
    }
}

@Composable
private fun SliderRow(
    label: String, value: Float, min: Float, max: Float,
    valueText: String? = null, steps: Int = 0,
    onChange: (Float) -> Unit
) {
    Column {
        Row {
            Text(label, color = TextMuted,
                style = MaterialTheme.typography.labelSmall,
                modifier = Modifier.weight(1f))
            Text(valueText ?: fmt(value), color = AuroraCyan,
                style = MaterialTheme.typography.labelSmall)
        }
        Slider(value = value, onValueChange = onChange,
            valueRange = min..max, steps = steps)
    }
}

@Composable
private fun StatusLine(label: String, value: String) {
    Row {
        Text(label, color = TextMuted,
            style = MaterialTheme.typography.labelSmall,
            modifier = Modifier.weight(1f))
        Text(value, color = PhosphorGreen,
            style = MaterialTheme.typography.labelSmall)
    }
}
