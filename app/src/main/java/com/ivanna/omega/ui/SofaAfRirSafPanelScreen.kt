package com.ivanna.omega.ui

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
 * SofaAfRirSafPanelScreen — panel de control unificado para los subsistemas que
 * ya existen en el motor pero no tenían superficie de UI:
 *
 *   SOFA — perfil/preset HRTF (SaFBridge.setSubjectIndexHint + reloadHrtf),
 *          intensidad espacial (nativeSetSpatialWet / nativeSetSpatialWidthDirect),
 *          on/off (nativeSetHRTFEnabled).
 *   AF   — modo AUTO/MANUAL (nativeSetAdaptiveEngineEnabled), intensidad
 *          adaptativa y modo (nativeSetAdaptiveControls), estado real desde
 *          nativeIsAdaptiveEngineRunning / nativeGetAdaptiveTelemetry.
 *   RIR  — tamaño de sala, nº de reflexiones, decay y dry/wet mapeados a
 *          nativeSetReflectionGain/Delay + SaFRoomBridge.setRoomState.
 *   SAF  — diagnóstico Φ_SAF-Room^∞ (getDiagnostics/getParams/step/reset),
 *          intensidad de protección (nativeSetFatigueProtection) y protección
 *          de voz (VoiceProtectionManager).
 *
 * No introduce motores nuevos ni duplica setters: todo son APIs existentes.
 */

private const val MAX_REFLECTIONS = 8

@Composable
fun SofaAfRirSafPanelScreen(
    onBack: () -> Unit = {},
    voiceMgr: VoiceProtectionManager? = null
) {
    val ctx = LocalContext.current
    val loaded = IvannaNativeLib.isLoaded

    // ── SOFA ────────────────────────────────────────────────────────────────
    var sofaEnabled by remember { mutableStateOf(true) }
    var sofaPreset by remember { mutableStateOf(0) }
    var sofaIntensity by remember { mutableStateOf(0.5f) }

    // ── AF ──────────────────────────────────────────────────────────────────
    var afAuto by remember { mutableStateOf(true) }
    var afMode by remember { mutableStateOf(AdaptiveMode.NATURAL) }
    var afIntensity by remember { mutableStateOf(50f) }
    var afRunning by remember { mutableStateOf(false) }
    var afTelemetry by remember { mutableStateOf<FloatArray?>(null) }

    // ── RIR ─────────────────────────────────────────────────────────────────
    var rirEnabled by remember { mutableStateOf(false) }
    var roomSize by remember { mutableStateOf(0.4f) }      // 0..1 → 3..40 m
    var reflections by remember { mutableStateOf(4) }
    var decay by remember { mutableStateOf(0.35f) }        // 0..1 → RT60 0..2.5 s
    var dryWet by remember { mutableStateOf(0.25f) }

    // ── SAF ─────────────────────────────────────────────────────────────────
    var safProtection by remember { mutableStateOf(0.5f) }
    var voiceProt by remember { mutableStateOf(voiceMgr?.isActive() ?: false) }
    var safDiag by remember { mutableStateOf(FloatArray(5)) }
    var safParams by remember { mutableStateOf(FloatArray(7)) }

    // Telemetría viva (AF + SAF)
    LaunchedEffect(loaded) {
        while (loaded) {
            runCatching {
                afRunning = IvannaNativeLib.nativeIsAdaptiveEngineRunning()
                afTelemetry = IvannaNativeLib.nativeGetAdaptiveTelemetry()
                safDiag = SaFRoomBridge.getDiagnostics()
                safParams = SaFRoomBridge.getParams()
            }
            delay(250)
        }
    }

    // Aplicadores — sólo APIs existentes
    fun applySofa() {
        if (!loaded) return
        runCatching {
            IvannaNativeLib.nativeSetHRTFEnabled(sofaEnabled)
            IvannaNativeLib.nativeSetSpatialWet(if (sofaEnabled) sofaIntensity else 0f)
            IvannaNativeLib.nativeSetSpatialWidthDirect(
                if (sofaEnabled) 0.5f + sofaIntensity else 0.5f
            )
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
        val rt60 = decay * 2.5f
        // DRR desciende con sala grande y decay largo (rango típico -10..+15 dB)
        val drr = 15f - (roomSize * 15f) - (decay * 10f)
        runCatching {
            for (i in 0 until MAX_REFLECTIONS) {
                val active = rirEnabled && i < reflections
                // Retardo de la i-ésima reflexión: camino acústico ≈ 2·d·(i+1)/c
                val delayMs = if (active) (2f * meters * (i + 1) / 343f) * 1000f else 0f
                // Atenuación exponencial gobernada por decay + mezcla dry/wet
                val gain =
                    if (active) dryWet * 0.9f.pow((i + 1) * (1.6f - decay)) else 0f
                IvannaNativeLib.nativeSetReflectionDelay(i, delayMs)
                IvannaNativeLib.nativeSetReflectionGain(i, gain)
            }
            SaFRoomBridge.setRoomState(
                rt60 = if (rirEnabled) rt60 else 0f,
                drr = drr,
                roomMode = if (rirEnabled) roomSize else 0f
            )
        }
    }

    fun applySaf() {
        if (!loaded) return
        runCatching {
            IvannaNativeLib.nativeSetFatigueProtection(safProtection, safProtection)
        }
    }

    // Empuje inicial del estado visible al motor
    LaunchedEffect(Unit) { applySofa(); applyAf(); applyRir(); applySaf() }

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
                Text(
                    "PANEL SOFA · AF · RIR · SAF",
                    color = AuroraCyan,
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.Bold
                )
                Text(
                    if (loaded) "Motor nativo cargado" else "Motor nativo NO cargado",
                    color = if (loaded) PhosphorGreen else AmberSignal,
                    style = MaterialTheme.typography.labelSmall
                )
            }
            TextButton(onClick = onBack) { Text("VOLVER", color = TextMuted) }
        }

        // ── SOFA ────────────────────────────────────────────────────────────
        PanelSection("SOFA · HRTF", AuroraCyan) {
            ToggleRow("Activar HRTF/SOFA", sofaEnabled) { sofaEnabled = it; applySofa() }
            Text("Perfil / preset", color = TextMuted, style = MaterialTheme.typography.labelSmall)
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                SOFA_PRESETS.forEachIndexed { idx, label ->
                    FilterChip(
                        selected = sofaPreset == idx,
                        onClick = {
                            sofaPreset = idx
                            runCatching {
                                SaFBridge.setSubjectIndexHint(idx)
                                IvannaSpatialManager.reloadHrtf(ctx)
                            }
                            applySofa()
                        },
                        label = { Text(label, style = MaterialTheme.typography.labelSmall) }
                    )
                }
            }
            SliderRow("Intensidad espacial", sofaIntensity, 0f, 1f) {
                sofaIntensity = it; applySofa()
            }
            StatusLine("Sujeto activo", IvannaSpatialManager.activeSubject)
            StatusLine("Estado", if (sofaEnabled) "ACTIVO" else "BYPASS")
        }

        // ── AF ──────────────────────────────────────────────────────────────
        PanelSection("AF · ADAPTIVE FEATURES", NeonMagenta) {
            ToggleRow(if (afAuto) "Modo AUTO" else "Modo MANUAL", afAuto) {
                afAuto = it; applyAf()
            }
            Text("Perfil adaptativo", color = TextMuted, style = MaterialTheme.typography.labelSmall)
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                AdaptiveMode.entries.forEach { m ->
                    FilterChip(
                        selected = afMode == m,
                        onClick = { afMode = m; applyAf() },
                        label = { Text(m.label, style = MaterialTheme.typography.labelSmall) }
                    )
                }
            }
            SliderRow("Intensidad adaptativa", afIntensity, 0f, 100f) {
                afIntensity = it; applyAf()
            }
            StatusLine("Motor", if (afRunning) "EN EJECUCIÓN" else "DETENIDO")
            afTelemetry?.let { t ->
                if (t.size >= 9) {
                    StatusLine("Target gain", fmt(t[3]))
                    StatusLine("Compresor", fmt(t[4]))
                    StatusLine("Ancho espacial", fmt(t[6]))
                    StatusLine("Margen seguridad", fmt(t[7]))
                }
            }
        }

        // ── RIR ─────────────────────────────────────────────────────────────
        PanelSection("RIR · SALA / REFLEXIONES", AuroraCyan) {
            ToggleRow("Activar RIR", rirEnabled) { rirEnabled = it; applyRir() }
            SliderRow("Tamaño de sala", roomSize, 0f, 1f, "${(3f + roomSize * 37f).toInt()} m") {
                roomSize = it; applyRir()
            }
            SliderRow(
                "Reflexiones", reflections.toFloat(), 1f, MAX_REFLECTIONS.toFloat(),
                "$reflections", steps = MAX_REFLECTIONS - 2
            ) { reflections = it.toInt().coerceIn(1, MAX_REFLECTIONS); applyRir() }
            SliderRow("Decay (RT60)", decay, 0f, 1f, fmt(decay * 2.5f) + " s") {
                decay = it; applyRir()
            }
            SliderRow("Mezcla dry/wet", dryWet, 0f, 1f) { dryWet = it; applyRir() }
            StatusLine("Estado", if (rirEnabled) "ACTIVO" else "BYPASS")
        }

        // ── SAF ─────────────────────────────────────────────────────────────
        PanelSection("SAF · Φ_SAF-Room^∞", PhosphorGreen) {
            StatusLine(
                "Sistema",
                if (loaded && safDiag.size >= 5 && safDiag[4] > 0f) "CONVERGIENDO" else "EN ESPERA"
            )
            SliderRow("Intensidad protección", safProtection, 0f, 1f) {
                safProtection = it; applySaf()
            }
            ToggleRow("Protección de voz", voiceProt) { on ->
                voiceProt = on
                runCatching { if (on) voiceMgr?.enable() else voiceMgr?.disable() }
            }
            if (safDiag.size >= 5) {
                StatusLine("α* (paso óptimo)", fmt(safDiag[0]))
                StatusLine("E_t (error)", fmt(safDiag[1]))
                StatusLine("λ_t (regularización)", fmt(safDiag[2]))
                StatusLine("σ (acoplamiento)", fmt(safDiag[3]))
                StatusLine("Iteraciones", safDiag[4].toInt().toString())
            }
            StatusLine("p_t", safParams.joinToString(" ") { fmt(it) })
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedButton(onClick = { runCatching { SaFRoomBridge.step() } }) { Text("STEP") }
                OutlinedButton(onClick = { runCatching { SaFRoomBridge.reset() } }) { Text("RESET") }
            }
        }

        Spacer(Modifier.height(24.dp))
    }
}

private val SOFA_PRESETS = listOf("NEUTRO", "AMPLIO", "CERCANO", "CINE")

private fun fmt(v: Float): String = String.format("%.3f", v)

@Composable
private fun PanelSection(title: String, accent: Color, content: @Composable ColumnScope.() -> Unit) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        color = ObsidianSoft,
        shape = MaterialTheme.shapes.medium,
        border = BorderStroke(1.dp, accent.copy(alpha = 0.30f))
    ) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            Text(
                title,
                color = accent,
                style = MaterialTheme.typography.titleSmall,
                fontWeight = FontWeight.Bold
            )
            content()
        }
    }
}

@Composable
private fun ToggleRow(label: String, checked: Boolean, onChange: (Boolean) -> Unit) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(label, color = TextMuted, style = MaterialTheme.typography.bodySmall,
            modifier = Modifier.weight(1f))
        Switch(checked = checked, onCheckedChange = onChange)
    }
}

@Composable
private fun SliderRow(
    label: String,
    value: Float,
    min: Float,
    max: Float,
    valueText: String? = null,
    steps: Int = 0,
    onChange: (Float) -> Unit
) {
    Column {
        Row {
            Text(label, color = TextMuted, style = MaterialTheme.typography.labelSmall,
                modifier = Modifier.weight(1f))
            Text(valueText ?: fmt(value), color = AuroraCyan,
                style = MaterialTheme.typography.labelSmall)
        }
        Slider(value = value, onValueChange = onChange, valueRange = min..max, steps = steps)
    }
}

@Composable
private fun StatusLine(label: String, value: String) {
    Row {
        Text(label, color = TextMuted, style = MaterialTheme.typography.labelSmall,
            modifier = Modifier.weight(1f))
        Text(value, color = PhosphorGreen, style = MaterialTheme.typography.labelSmall)
    }
}
