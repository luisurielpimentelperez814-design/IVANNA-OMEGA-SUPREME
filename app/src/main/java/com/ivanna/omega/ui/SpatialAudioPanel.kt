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
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.core.PersistedStateRestorer
import com.ivanna.omega.magisk.OmegaEngineBridge
import com.ivanna.omega.saf.SaFBridge
import com.ivanna.omega.spatial.IvannaSpatialEngine
import com.ivanna.omega.spatial.IvannaSpatialManager
import com.ivanna.omega.spatial.SaFOptimizer
import com.ivanna.omega.ui.theme.*
import kotlinx.coroutines.delay

@Composable
fun SpatialAudioPanel(modifier: Modifier = Modifier) {
    val state by SpatialAudioPrefs.stateFlow.collectAsState()
    var hrtfLoaded by remember { mutableStateOf(false) }
    var activeSubject by remember { mutableStateOf("none") }
    var roomStatus by remember { mutableStateOf("Desconectado") }
    var safStatus by remember { mutableStateOf("Pendiente") }

    fun update(mutator: (SpatialState) -> SpatialState) {
        val n = mutator(state)
        SpatialAudioPrefs.save(n)
        PersistedStateRestorer.restoreSpatial(n)
    }

    LaunchedEffect(Unit) {
        while (true) {
            hrtfLoaded = IvannaSpatialManager.isHrtfLoaded()
            activeSubject = IvannaSpatialManager.getActiveSubject()
            roomStatus = OmegaEngineBridge.getRoomStatus()
            safStatus = if (SaFBridge.isModelLoaded()) "SAF_model.json cargado" else "No cargado"

            if (state.safEnabled && state.safAutoMode) {
                runCatching {
                    com.ivanna.omega.saf.SaFRoomBridge.setRoomState(state.rirRt60, 0f, 0f)
                    com.ivanna.omega.saf.SaFRoomBridge.step()
                    val qa = com.ivanna.omega.saf.SaFRoomBridge.getParams()
                    OmegaEngineBridge.pushSafLatentQ(
                        FloatArray(7) { i -> qa.getOrElse(i) { 0f } * state.safIntensity },
                        gain = state.safIntensity
                    )
                }
            }
            delay(1000)
        }
    }

    Column(
        modifier
            .fillMaxSize()
            .background(Brush.verticalGradient(listOf(ObsidianVoid, ObsidianSoft)))
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        Text(
            "SPATIAL AUDIO OMNI", 
            color = AuroraCyan, 
            style = MaterialTheme.typography.titleMedium,
            fontWeight = FontWeight.ExtraBold, 
            letterSpacing = 2.sp
        )

        // ── HRTF ──────────────────────────────────────────────────────────
        SpatialCard("HRTF BINAURAL", "Dataset IHR1 medido · deploy Magisk con SHA256") {
            RowSwitch("HRTF activo", state.hrtfEnabled) { on ->
                update { it.copy(hrtfEnabled = on) }
                IvannaSpatialEngine.enabled = on
                if (on) IvannaSpatialManager.setHrtfSubject(state.hrtfSubject)
            }
            Text("Sujeto Topológico:", color = TextSecondary, fontSize = 11.sp, fontWeight = FontWeight.Bold)
            
            val subjects = listOf(
                "KEMAR"      to "kemar",
                "KEMAR-LG"   to "kemar_large",
                "CIPIC 003"  to "cipic_003",
                "CIPIC 165"  to "cipic_165",
                "TU-Berlin"  to "tu_berlin_kemar",
                "Pulse"      to "pulse"
            )

            // Grid-like layout for chips
            androidx.compose.foundation.layout.FlowRow(
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                subjects.forEach { (label, id) ->
                    val sel = state.hrtfSubject == id
                    FilterChip(
                        selected = sel, 
                        onClick = {
                            update { it.copy(hrtfSubject = id) }
                            IvannaSpatialManager.setHrtfSubject(id)
                        }, 
                        label = { Text(label, fontSize = 10.sp, fontWeight = if (sel) FontWeight.Bold else FontWeight.Normal) },
                        colors = FilterChipDefaults.filterChipColors(
                            selectedContainerColor = AuroraCyan.copy(0.2f),
                            selectedLabelColor = AuroraCyan,
                            labelColor = TextMuted
                        ),
                        border = FilterChipDefaults.filterChipBorder(
                            enabled = true, selected = sel,
                            borderColor = if (sel) AuroraCyan else ObsidianEdge
                        )
                    )
                }
            }
            
            val statusColor = if (hrtfLoaded) PhosphorGreen else if (activeSubject == "none") TextMuted else AmberSignal
            val statusText  = if (hrtfLoaded) "CARGADO · $activeSubject"
                              else if (activeSubject == "none") "SIN DATASET (fallback analítico)"
                              else "FALLBACK / CARGANDO · $activeSubject"
            
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                Box(modifier = Modifier.size(6.dp).background(statusColor, RoundedCornerShape(3.dp)))
                Text("ESTADO: $statusText", color = statusColor, fontSize = 10.sp, fontWeight = FontWeight.Bold)
            }
        }

        // ── RIR ───────────────────────────────────────────────────────────
        SpatialCard("REVERB DE SALA REAL", "200 RIR medidos · convolución overlap-save · selección por RT60") {
            RowSwitch("RIR activo", state.rirEnabled) { on ->
                update { it.copy(rirEnabled = on) }
                if (on) OmegaEngineBridge.setRoom(state.rirRt60, state.rirWet) else OmegaEngineBridge.disableRoom()
            }
            LabeledSlider("RT60 Objetivo", state.rirRt60, 0.1f..1.5f, "%.2f s") { v ->
                update { it.copy(rirRt60 = v) }
                if (state.rirEnabled) OmegaEngineBridge.setRoom(v, state.rirWet)
            }
            LabeledSlider("Mezcla Wet/Dry", state.rirWet, 0f..1f, "%.2f") { v ->
                update { it.copy(rirWet = v) }
                if (state.rirEnabled) OmegaEngineBridge.setRoom(state.rirRt60, v)
            }
            Text("Motor: $roomStatus", color = TextSecondary, fontSize = 10.sp)
        }

        // ── SAF ───────────────────────────────────────────────────────────
        SpatialCard("SAF · AJUSTE ESPECTRAL ADAPTATIVO", "Vector latente q[7] → ObjectRenderer.setSafLatent") {
            RowSwitch("SAF activo", state.safEnabled) { on ->
                update { it.copy(safEnabled = on) }
                if (on) {
                    runCatching { SaFBridge.nativeSaFInit("/data/adb/ivanna_omega/SAF_model.json") }
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
            LabeledSlider("Intensidad SAF", state.safIntensity, 0f..1f, "%.2f") { v ->
                update { it.copy(safIntensity = v) }
                if (state.safEnabled) {
                    runCatching { SaFOptimizer.syncToRoomBridge(state.rirRt60) }
                    val q = com.ivanna.omega.saf.SaFRoomBridge.getParams()
                    OmegaEngineBridge.pushSafLatentQ(
                        FloatArray(7) { i -> q.getOrElse(i) { 0f } * v },
                        gain = v
                    )
                }
            }
            RowSwitch("Modo automático", state.safAutoMode) { on -> update { it.copy(safAutoMode = on) } }
            Text("Modelo: $safStatus", color = TextSecondary, fontSize = 10.sp)
        }
        
        Spacer(Modifier.height(32.dp))
    }
}

@Composable
private fun SpatialCard(title: String, subtitle: String, content: @Composable ColumnScope.() -> Unit) {
    Surface(
        color = ObsidianGlass, 
        shape = RoundedCornerShape(16.dp),
        border = BorderStroke(1.dp, ObsidianEdge.copy(alpha = 0.5f))
    ) {
        Column(Modifier.padding(18.dp), verticalArrangement = Arrangement.spacedBy(14.dp)) {
            Column {
                Text(title, color = AuroraCyan, fontSize = 13.sp, fontWeight = FontWeight.Bold, letterSpacing = 1.sp)
                Text(subtitle, color = TextMuted, fontSize = 10.sp)
            }
            content()
        }
    }
}

@Composable
private fun RowSwitch(label: String, checked: Boolean, onChange: (Boolean) -> Unit) {
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.CenterVertically) {
        Text(label, color = TextPrimary, fontSize = 13.sp, fontWeight = FontWeight.Medium)
        Switch(
            checked = checked, 
            onCheckedChange = onChange,
            colors = SwitchDefaults.colors(
                checkedThumbColor = ObsidianVoid,
                checkedTrackColor = AuroraCyan,
                uncheckedThumbColor = TextMuted,
                uncheckedTrackColor = ObsidianDeep,
                uncheckedBorderColor = ObsidianEdge
            )
        )
    }
}

@Composable
private fun LabeledSlider(label: String, value: Float, range: ClosedFloatingPointRange<Float>, fmt: String, onChange: (Float) -> Unit) {
    Column {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            Text(label, color = TextSecondary, fontSize = 12.sp)
            Text(fmt.format(value), color = AuroraCyan, fontSize = 12.sp, fontWeight = FontWeight.Bold)
        }
        Slider(
            value = value, 
            onValueChange = onChange, 
            valueRange = range,
            colors = SliderDefaults.colors(
                thumbColor = AuroraCyan, 
                activeTrackColor = AuroraCyan,
                inactiveTrackColor = ObsidianDeep
            )
        )
    }
}
