package com.ivanna.omega.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.magisk.OmegaEngineBridge
import com.ivanna.omega.saf.SaFBridge
import com.ivanna.omega.spatial.IvannaSpatialEngine
import com.ivanna.omega.spatial.IvannaSpatialManager
import com.ivanna.omega.spatial.SaFOptimizer
import com.ivanna.omega.ui.theme.*
import kotlinx.coroutines.delay

/**
 * SpatialAudioPanel — control real de HRTF / RIR / SAF.
 * Ningún control decorativo: cada uno va Compose→Prefs→JNI/Bridge→DSP,
 * y el estado mostrado se LEE del motor (polling 1s), no se asume.
 */
@Composable
fun SpatialAudioPanel(modifier: Modifier = Modifier) {
    val context = LocalContext.current
    var state by remember { mutableStateOf(SpatialAudioPrefs.load(context)) }
    fun update(f: (SpatialAudioState) -> SpatialAudioState) {
        state = f(state)
        SpatialAudioPrefs.save(context, state)
    }

    // Estado real leído del motor (FASE 5)
    var hrtfLoaded by remember { mutableStateOf(false) }
    var activeSubject by remember { mutableStateOf("none") }
    var roomStatus by remember { mutableStateOf("") }
    var safStatus by remember { mutableStateOf("") }

    // Restore al arrancar: empuja lo persistido al motor (FASE 3 pasos 1-4)
    LaunchedEffect(Unit) {
        if (state.hrtfEnabled) {
            IvannaSpatialEngine.enabled = true
            IvannaSpatialManager.setHrtfSubject(state.hrtfSubject)
        }
        if (state.rirEnabled) OmegaEngineBridge.setRoom(state.rirRt60, state.rirWet)
        if (state.safEnabled) {
            runCatching { SaFBridge.nativeSaFInit("/data/adb/ivanna_omega/SAF_model.json") }
            // Propagar q[7] real al daemon en el restore — sin esto el DSP
            // arrancaba sin la calibración SAF aunque el switch persistido
            // fuera "activo".
            val q = com.ivanna.omega.saf.SaFRoomBridge.getParams()
            OmegaEngineBridge.pushSafLatentQ(
                FloatArray(7) { i -> q.getOrElse(i) { 0f } * state.safIntensity },
                gain = state.safIntensity
            )
        }
        while (true) {
            hrtfLoaded    = IvannaSpatialManager.isHrtfDatasetLoaded()
            activeSubject = IvannaSpatialManager.currentHrtfSubject()
            roomStatus    = OmegaEngineBridge.getRoomStatus()?.toString() ?: "daemon sin sala activa"
            safStatus     = runCatching {
                if (SaFBridge.nativeSaFIsConverged()) "convergido" else "iteración ${SaFBridge.nativeSaFGetIteration()} · error %.3f".format(SaFBridge.nativeSaFGetError())
            }.getOrDefault("modelo no cargado")
            delay(1000)
        }
    }

    Column(modifier.fillMaxSize().background(ObsidianDeep).verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp)) {

        Text("SPATIAL AUDIO", color = AuroraCyan, fontSize = 13.sp, fontWeight = FontWeight.ExtraBold, letterSpacing = 2.sp)

        // ── HRTF ──────────────────────────────────────────────────────────
        SpatialCard("HRTF BINAURAL", "Dataset IHR1 medido · deploy Magisk con SHA256") {
            RowSwitch("HRTF activo", state.hrtfEnabled) { on ->
                update { it.copy(hrtfEnabled = on) }
                IvannaSpatialEngine.enabled = on
                if (on) IvannaSpatialManager.setHrtfSubject(state.hrtfSubject)
            }
            Text("Sujeto:", color = TextSecondary, fontSize = 11.sp)
            // Mapeo etiqueta → id IHR1 real desplegado (ver HrtfSubjectSelector)
            val subjects = listOf(
                "KEMAR"      to "kemar",
                "KEMAR-LG"   to "kemar_large",
                "CIPIC 003"  to "cipic_003",
                "CIPIC 165"  to "cipic_165",
                "TU-Berlin"  to "tu_berlin_kemar",
                "Pulse"      to "pulse",
                "ITA"        to "ita_artificial_head"
            )
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                subjects.forEach { (label, id) ->
                    val sel = state.hrtfSubject == id
                    FilterChip(selected = sel, onClick = {
                        update { it.copy(hrtfSubject = id) }
                        IvannaSpatialManager.setHrtfSubject(id)
                    }, label = { Text(label, fontSize = 10.sp) })
                }
            }
            val statusColor = if (hrtfLoaded) PhosphorGreen else if (activeSubject == "none") TextMuted else AmberSignal
            val statusText  = if (hrtfLoaded) "CARGADO · $activeSubject"
                              else if (activeSubject == "none") "SIN DATASET (fallback analítico)"
                              else "FALLBACK / CARGANDO · $activeSubject"
            Text("Estado: $statusText", color = statusColor, fontSize = 11.sp, fontWeight = FontWeight.Bold)
        }

        // ── RIR ───────────────────────────────────────────────────────────
        SpatialCard("REVERB DE SALA REAL", "200 RIR medidos · convolución overlap-save · selección por RT60") {
            RowSwitch("RIR activo", state.rirEnabled) { on ->
                update { it.copy(rirEnabled = on) }
                if (on) OmegaEngineBridge.setRoom(state.rirRt60, state.rirWet) else OmegaEngineBridge.disableRoom()
            }
            LabeledSlider("RT60 objetivo", state.rirRt60, 0.1f..1.5f, "%.2f s") { v ->
                update { it.copy(rirRt60 = v) }
                if (state.rirEnabled) OmegaEngineBridge.setRoom(v, state.rirWet)
            }
            LabeledSlider("Mezcla wet/dry", state.rirWet, 0f..1f, "%.2f") { v ->
                update { it.copy(rirWet = v) }
                if (state.rirEnabled) OmegaEngineBridge.setRoom(state.rirRt60, v)
            }
            Text("Motor: $roomStatus", color = TextMuted, fontSize = 10.sp)
        }

        // ── SAF ───────────────────────────────────────────────────────────
        SpatialCard("SAF · AJUSTE ESPECTRAL ADAPTATIVO", "Vector latente q[7] → ObjectRenderer.setSafLatent") {
            RowSwitch("SAF activo", state.safEnabled) { on ->
                update { it.copy(safEnabled = on) }
                if (on) {
                    runCatching { SaFBridge.nativeSaFInit("/data/adb/ivanna_omega/SAF_model.json") }
                    // FIX (descableado): el switch solo inicializaba el modelo local
                    // pero nunca enviaba q[7] al daemon — omega_effect.cpp nunca
                    // recibía la calibración real. SaFRoomBridge.getParams() es el
                    // p_t real del optimizador Riemanniano; se envía escalado por
                    // la intensidad actual.
                    val q = com.ivanna.omega.saf.SaFRoomBridge.getParams()
                    OmegaEngineBridge.pushSafLatentQ(
                        FloatArray(7) { i -> q.getOrElse(i) { 0f } * state.safIntensity },
                        gain = state.safIntensity
                    )
                } else {
                    runCatching { SaFBridge.nativeSaFReset() }
                    OmegaEngineBridge.pushSafLatentQ(FloatArray(7), gain = 0f)
                }
            }
            LabeledSlider("Intensidad", state.safIntensity, 0f..1f, "%.2f") { v ->
                update { it.copy(safIntensity = v) }
                if (state.safEnabled) {
                    runCatching { SaFOptimizer.syncToRoomBridge(state.rirRt60) }
                    // Reenviar q[7] escalado por la nueva intensidad — antes este
                    // slider solo guardaba el número en prefs sin efecto en audio.
                    val q = com.ivanna.omega.saf.SaFRoomBridge.getParams()
                    OmegaEngineBridge.pushSafLatentQ(
                        FloatArray(7) { i -> q.getOrElse(i) { 0f } * v },
                        gain = v
                    )
                }
            }
            RowSwitch("Modo automático", state.safAutoMode) { on -> update { it.copy(safAutoMode = on) } }
            Text("Modelo: $safStatus", color = TextMuted, fontSize = 10.sp)
        }
    }
}

@Composable
private fun SpatialCard(title: String, subtitle: String, content: @Composable ColumnScope.() -> Unit) {
    Surface(color = Color(0xFF111318), shape = androidx.compose.foundation.shape.RoundedCornerShape(14.dp)) {
        Column(Modifier.padding(14.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Text(title, color = Color(0xFFE2E8F0), fontSize = 12.sp, fontWeight = FontWeight.Bold)
            Text(subtitle, color = TextMuted, fontSize = 9.sp)
            content()
        }
    }
}

@Composable
private fun RowSwitch(label: String, checked: Boolean, onChange: (Boolean) -> Unit) {
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
        Text(label, color = TextSecondary, fontSize = 12.sp)
        Switch(checked = checked, onCheckedChange = onChange)
    }
}

@Composable
private fun LabeledSlider(label: String, value: Float, range: ClosedFloatingPointRange<Float>, fmt: String, onChange: (Float) -> Unit) {
    Column {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            Text(label, color = TextSecondary, fontSize = 11.sp)
            Text(fmt.format(value), color = AuroraCyan, fontSize = 11.sp, fontWeight = FontWeight.Bold)
        }
        Slider(value = value, onValueChange = onChange, valueRange = range,
            colors = SliderDefaults.colors(thumbColor = AuroraCyan, activeTrackColor = AuroraCyan))
    }
}
