package com.ivanna.omega.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.ui.Alignment
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
import androidx.compose.foundation.layout.ExperimentalLayoutApi

/**
 * SpatialAudioPanel — control real de HRTF / RIR / SAF.
 * Ningún control decorativo: cada uno va Compose→Prefs→JNI/Bridge→DSP,
 * y el estado mostrado se LEE del motor (polling 1s), no se asume.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun SpatialAudioPanel(
    onBack: (() -> Unit)? = null,
    modifier: Modifier = Modifier
) {
    val context = LocalContext.current
    var state by remember { mutableStateOf(SpatialAudioPrefs.load(context)) }
    fun update(f: (SpatialAudioState) -> SpatialAudioState) {
        state = f(state)
        SpatialAudioPrefs.save(context, state)
    }

    var supremeState by remember { mutableStateOf(SupremeAxesPrefs.load(context)) }
    val supremeTelemetry by rememberSupremeAxesTelemetry()
    fun updateSupreme(f: (SupremeAxesState) -> SupremeAxesState) {
        supremeState = f(supremeState)
        SupremeAxesPrefs.save(context, supremeState)
    }

    // Estado real leído del motor (FASE 5)
    var hrtfLoaded by remember { mutableStateOf(false) }
    var activeSubject by remember { mutableStateOf("none") }
    var roomStatus by remember { mutableStateOf("") }
    var safStatus by remember { mutableStateOf("") }

    // Estado del Motor Híbrido Magistral (VBAP 3D + Ambisonics + HRTF KEMAR 128-tap + Schroeder/Moorer)
    // §0.4: Por defecto OFF (0f) para no duplicar HRTF ni cola de sala sobre ObjectRenderer/RirConvolver.
    val initialHybridTelem = remember { com.ivanna.omega.core.NativeBridge.safeGetHybridMagistralTelemetry() }
    var hybridEnabled by remember { mutableStateOf(initialHybridTelem.getOrElse(0) { 0f } >= 0.5f) }
    var hybridBinauralWet by remember { mutableStateOf(initialHybridTelem.getOrElse(1) { 0.65f }) }
    var hybridAzimuthDeg by remember { mutableStateOf(initialHybridTelem.getOrElse(2) { 30f }) }
    var hybridElevationDeg by remember { mutableStateOf(initialHybridTelem.getOrElse(3) { 0f }) }
    var hybridRoomSize by remember { mutableStateOf(initialHybridTelem.getOrElse(4) { 0.55f }) }
    var hybridAbsorption by remember { mutableStateOf(initialHybridTelem.getOrElse(5) { 0.35f }) }
    var hybridDampening by remember { mutableStateOf(initialHybridTelem.getOrElse(6) { 0.40f }) }
    var hybridRoomWet by remember { mutableStateOf(initialHybridTelem.getOrElse(7) { 0.25f }) }

    fun pushHybridMagistral() {
        com.ivanna.omega.core.NativeBridge.safeSetHybridMagistralParams(
            enabled = hybridEnabled,
            binauralWet = hybridBinauralWet,
            virtualAzimuthDeg = hybridAzimuthDeg,
            virtualElevationDeg = hybridElevationDeg,
            roomSize = hybridRoomSize,
            roomAbsorption = hybridAbsorption,
            roomDampening = hybridDampening,
            roomWetMix = hybridRoomWet
        )
    }

    // Restore al arrancar: empuja lo persistido al motor (FASE 3 pasos 1-4) fuera del hilo UI
    LaunchedEffect(Unit) {
        kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.IO) {
            if (state.hrtfEnabled) {
                IvannaSpatialEngine.enabled = true
                IvannaSpatialManager.setHrtfSubject(state.hrtfSubject)
            }
            IvannaSpatialManager.setReverbLevel(state.objectReverbLevel)
            if (state.rirEnabled) OmegaEngineBridge.setRoom(state.rirRt60, state.rirWet) else OmegaEngineBridge.disableRoom()
            if (state.safEnabled) {
                runCatching { SaFBridge.nativeSaFInit("/data/adb/ivanna_omega/SAF_model.json") }
                val q = com.ivanna.omega.saf.SaFRoomBridge.getParams()
                OmegaEngineBridge.pushSafLatentQ(
                    FloatArray(7) { i -> q.getOrElse(i) { 0f } * state.safIntensity },
                    gain = state.safIntensity
                )
            }
            runCatching {
                com.ivanna.omega.core.NativeBridge.safeSetCochlearInverseEnabled(state.cochlearInverseEnabled)
                com.ivanna.omega.core.NativeBridge.safeSetCochlearIntensity(state.cochlearIntensity)
                SupremeAxesPrefs.applyToNative(supremeState)
                pushHybridMagistral()
            }
            while (true) {
                hrtfLoaded    = IvannaSpatialManager.isHrtfDatasetLoaded()
                activeSubject = IvannaSpatialManager.currentHrtfSubject()
                val cochlearNat = IvannaSpatialManager.isCochlearActive()
                roomStatus    = (OmegaEngineBridge.getRoomStatus()?.toString() ?: "Studio BRIR procedimental activo") +
                                (if (cochlearNat) " · PINN ON" else "")
                safStatus     = runCatching {
                    if (SaFBridge.nativeSaFIsConverged()) "convergido" else "iteración ${SaFBridge.nativeSaFGetIteration()} · error %.3f".format(SaFBridge.nativeSaFGetError())
                }.getOrDefault("modelo no cargado")

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
    }

    Column(modifier.fillMaxSize().background(ObsidianDeep).verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp)) {

        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically
        ) {
            if (onBack != null) {
                IconButton(onClick = onBack) {
                    Icon(
                        imageVector = Icons.Default.ArrowBack,
                        contentDescription = "Atrás",
                        tint = AuroraCyan
                    )
                }
                Spacer(modifier = Modifier.width(4.dp))
            }
            Text("SPATIAL AUDIO · INVERSIÓN COCLEAR (PINN)", color = AuroraCyan, fontSize = 13.sp, fontWeight = FontWeight.ExtraBold, letterSpacing = 2.sp)
        }

        // ── Eje Supremo: Inversión Biomecánica Coclear (PINN) ──────────────
        CochlearInverseCard(
            initialEnabled = state.cochlearInverseEnabled,
            initialIntensity = state.cochlearIntensity,
            onStateChanged = { en, inten ->
                update { it.copy(cochlearInverseEnabled = en, cochlearIntensity = inten) }
            }
        )

        // ── HRTF ──────────────────────────────────────────────────────────
        SpatialCard("HRTF BINAURAL", "Dataset IHR1 medido · deploy Magisk con SHA256") {
            RowSwitch("HRTF activo", state.hrtfEnabled) { on ->
                update { it.copy(hrtfEnabled = on) }
                IvannaSpatialEngine.enabled = on
                if (on) IvannaSpatialManager.setHrtfSubject(state.hrtfSubject)
            }
            Text("Sujeto:", color = TextSecondary, fontSize = 11.sp)
            // FIX (descableado): los IDs anteriores (kemar_subject_165,
            // subject_003, subject_008, subject_009) no coincidían con
            // NINGUNO de los 11 sujetos reales del índice deployado
            // (magisk_module/.../hrtf/hrtf_index.json) — cada chip fallaba
            // silenciosamente (runCatching en setHrtfSubject → false, sin
            // efecto, sin log visible al usuario). IDs verificados contra
            // el índice real: kemar, cipic_003..cipic_165, pulse.
            val subjects = listOf(
                "KEMAR"      to "kemar",
                "KEMAR-LG"   to "kemar_large",
                "CIPIC 003"  to "cipic_003",
                "CIPIC 165"  to "cipic_165",
                "TU-Berlin"  to "tu_berlin_kemar",
                "Pulse"      to "pulse"
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
            LabeledSlider("Reverb de objetos", state.objectReverbLevel, 0f..1f, "%.2f") { v ->
                update { it.copy(objectReverbLevel = v) }
                IvannaSpatialManager.setReverbLevel(v)
            }
            Text("Motor: $roomStatus · ObjReverb=${"%.2f".format(IvannaSpatialManager.reverbLevel)}", color = TextMuted, fontSize = 10.sp)
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

        // ── MOTOR HÍBRIDO MAGISTRAL (VBAP 3D + AMBISONICS + HRTF + SALA) ──
        SpatialCard(
            "MOTOR HÍBRIDO MAGISTRAL",
            "VBAP 3D → Ambisonics 1er Orden → HRTF KEMAR 128-tap + Schroeder/Moorer"
        ) {
            RowSwitch("Motor Híbrido Magistral Activo", hybridEnabled) { on ->
                hybridEnabled = on
                pushHybridMagistral()
            }
            LabeledSlider("Mezcla Binaural HRTF", hybridBinauralWet, 0f..1f, "%.2f") { v ->
                hybridBinauralWet = v
                pushHybridMagistral()
            }
            LabeledSlider("Azimut Virtual 3D", hybridAzimuthDeg, 5f..90f, "%.0f°") { v ->
                hybridAzimuthDeg = v
                pushHybridMagistral()
            }
            LabeledSlider("Elevación Virtual 3D", hybridElevationDeg, -45f..45f, "%.0f°") { v ->
                hybridElevationDeg = v
                pushHybridMagistral()
            }
            LabeledSlider("Tamaño Sala Acústica", hybridRoomSize, 0.1f..1f, "%.2f") { v ->
                hybridRoomSize = v
                pushHybridMagistral()
            }
            LabeledSlider("Absorción de Pared", hybridAbsorption, 0.05f..0.95f, "%.2f") { v ->
                hybridAbsorption = v
                pushHybridMagistral()
            }
            LabeledSlider("Amortiguamiento HF", hybridDampening, 0.05f..0.95f, "%.2f") { v ->
                hybridDampening = v
                pushHybridMagistral()
            }
            LabeledSlider("Mezcla Sala (Wet)", hybridRoomWet, 0f..0.60f, "%.2f") { v ->
                hybridRoomWet = v
                pushHybridMagistral()
            }
        }

        // ── PIPELINE ESPACIAL EN VIVO ─────────────────────────────────────
        SpatialCard(
            "PIPELINE ESPACIAL EN VIVO",
            "Cadena unificada Zero-Pop · SPSC Lock-Free · Crossfade 512 muestras"
        ) {
            Text(
                text = "1. Separación de Objetos + VBAP 3D (${"%.0f".format(hybridAzimuthDeg)}°)\n" +
                       "2. Codificación Ambisonics B-Format (W, X, Y, Z) → Virtual 7.1.4\n" +
                       "3. Convolución Binaural HRTF ($activeSubject) + Híbrido Magistral (${if (hybridEnabled) "ON" else "BYPASS"})\n" +
                       "4. Proyección de Sala RIR (${if (state.rirEnabled) "RT60 ${"%.2f".format(state.rirRt60)}s" else "BYPASS"}) + SAF Φ_∞",
                color = AuroraCyan,
                fontSize = 10.sp
            )
        }

        // ── 5 Ejes de Supremacía Cuántico-Neuromórfica (C++23 Lock-Free) ──
        WarpedLatticeAxisCard(supremeState, supremeTelemetry, ::updateSupreme)
        TransharmonicCvnnAxisCard(supremeState, supremeTelemetry, ::updateSupreme)
        SnnNmfHoaAxisCard(supremeState, supremeTelemetry, ::updateSupreme)
        PinnaManifoldAxisCard(supremeState, supremeTelemetry, ::updateSupreme)
        ShmFarrowArbitratorAxisCard(supremeState, supremeTelemetry, ::updateSupreme)
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
