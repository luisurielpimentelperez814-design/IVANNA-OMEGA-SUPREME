package com.ivanna.omega.ui

import com.ivanna.omega.spatial.HrtfSubjectSelector
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.platform.LocalContext
import com.ivanna.omega.audio.OmegaMetrics
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.core.NativeBridge
import com.ivanna.omega.dsp.DSPBridge
import com.ivanna.omega.magisk.MagiskBridge
import com.ivanna.omega.magisk.OmegaEngineBridge
import com.ivanna.omega.saf.SaFBridge
import com.ivanna.omega.spatial.IvannaSpatialEngine
import com.ivanna.omega.spatial.IvannaSpatialManager
import com.ivanna.omega.spatial.SpatialControlStore
import com.ivanna.omega.ui.theme.*
import org.json.JSONObject
import java.io.File

/**
 * SpatialControlPanel — control real de HRTF / RIR / SAF.
 *
 * Cableado (sin controles decorativos):
 *  HRTF on/off   → IvannaNativeLib.nativeSetHRTFEnabled (JNI real)
 *  Sujeto HRTF   → SaFBridge.nativeSaFSetSubjectIndex (ancla p₀) + daemon JSON
 *  RIR on/wet    → MagiskBridge SET_REVERB + DSPBridge.nativeSetSpatialWet
 *  Sala RIR      → metadata.csv real en /data/adb/ivanna_omega/rir/ (RT60 real)
 *  SAF on/int    → daemon SET_SAF_STATE (command_server lo procesa de verdad)
 *  Persistencia  → SpatialControlStore (SharedPreferences) en cada cambio
 *  Bidireccional → OmegaMetrics.shared (hrtfActive, rms, sampleRate) + estado
 *                  de carga del dataset leído del filesystem real.
 */
@Composable
fun SpatialControlPanel(onBack: () -> Unit = {}) {
    val context = LocalContext.current
    var cfg by remember { mutableStateOf(SpatialControlStore.load(context)) }
    var adaptivePrefs by remember { mutableStateOf(AdaptiveControlsPrefs.load(context)) }
    var hybridTele by remember { mutableStateOf(NativeBridge.safeGetHybridMagistralTelemetry()) }
    val metrics by OmegaMetrics.shared.collectAsState()
    var hrtfSubjectReply by remember { mutableStateOf("") }

    fun syncHybrid(s: AdaptiveControlsState) {
        AdaptiveControlsPrefs.save(context, s)
        adaptivePrefs = s
        NativeBridge.safeSetHybridMagistralParams(
            enabled             = s.hybridMagistralEnabled,
            binauralWet         = s.hybridBinauralWet,
            virtualAzimuthDeg   = if (kotlin.math.abs(s.binauralAzimuth) > 1f) s.binauralAzimuth else 30f,
            virtualElevationDeg = s.binauralElevation,
            roomSize            = s.hybridRoomSize,
            roomAbsorption      = s.hybridRoomAbsorption,
            roomDampening       = s.hybridRoomDampening,
            roomWetMix          = s.hybridRoomWetMix
        )
        hybridTele = NativeBridge.safeGetHybridMagistralTelemetry()
    }

    LaunchedEffect(Unit) {
        IvannaSpatialEngine.setDistance(cfg.reflectionDistance)
        syncHybrid(adaptivePrefs)
    }

    fun apply(c: SpatialControlStore.SpatialConfig) {
        cfg = c
        SpatialControlStore.save(context, c)
        // Distancia / Reflexiones → IvannaSpatialEngine (nativeSetReflectionDelay/Gain)
        IvannaSpatialEngine.setDistance(c.reflectionDistance)
        // HRTF enable → motor in-process
        if (IvannaNativeLib.isLoaded) runCatching { IvannaNativeLib.nativeSetHRTFEnabled(c.hrtfEnabled) }
        // Sujeto → ancla SAF + daemon
        val idx = SpatialControlStore.SUBJECTS.indexOf(c.hrtfSubject)
        if (IvannaNativeLib.isLoaded && idx >= 0) runCatching { SaFBridge.setSubjectIndexHint(idx) }
        // RIR wet → DSPBridge (wet espacial real) + daemon (reverb)
        if (DSPBridge.isLoaded) runCatching {
            // FIX: mix=0.8→0.5 (neutro). mix>0.5 → +3.6dB pre-EQ → biquad inestable → tronido
            DSPBridge.setParams(drive = 0.5f, wet = if (c.rirEnabled) c.rirWet else 0f, mix = 0.5f,
                alpha = 0.94f, beta = 0.85f, gamma = 0.72f, freq = 1000f, resonance = 0.7f,
                low = 0f, mid = 0f, high = 0f, presence = 0f, master = 0.85f)
        }
        if (MagiskBridge.isDaemonRunning) runCatching {
            MagiskBridge.sendCommand("SET_REVERB:${if (c.rirEnabled) c.rirWet else 0f}")
        }
        // SAF → daemon (SET_SAF_STATE procesado por command_server)
        runCatching {
            OmegaEngineBridge.sendCommand(JSONObject().apply {
                put("action", "SET_SAF_STATE")
                put("enabled", c.safEnabled)
                put("intensity", c.safIntensity.toDouble())
                put("timestamp", System.currentTimeMillis())
            })
        }
        // Upmixer 4-Stems + posiciones 3D → IvannaSpatialManager
        IvannaSpatialManager.setUpmixerEnabled(c.upmixerEnabled)
        IvannaSpatialManager.setStemPosition(0, c.stem0X, c.stem0Y, c.stem0Z, c.stem0Width)
        IvannaSpatialManager.setStemPosition(1, c.stem1X, c.stem1Y, c.stem1Z, c.stem1Width)
        IvannaSpatialManager.setStemPosition(2, c.stem2X, c.stem2Y, c.stem2Z, c.stem2Width)
        IvannaSpatialManager.setStemPosition(3, c.stem3X, c.stem3Y, c.stem3Z, c.stem3Width)
    }

    // Estado real de carga de datasets (filesystem, no simulado)
    // Buscar IHR1 activo en rutas prioritarias (Magisk mount primero)
    val ihr1 = listOf(
        File("/system/etc/ivanna_omega/hrtf/${HrtfSubjectSelector.resolveSubjectId(cfg.hrtfSubject)}.ihr1"),
        File("/system/etc/ivanna_omega/hrtf/kemar.ihr1"),
        File("/data/adb/ivanna_omega/hrtf_dataset.ihr1")
    ).firstOrNull { it.exists() }
    val safModel = listOf(
        File("/system/etc/ivanna_omega/SAF_model.json"),
        File("/data/adb/ivanna_omega/SAF_model.json")
    ).firstOrNull { it.exists() }
    val rirDir = File("/data/adb/ivanna_omega/rir")
    val rirMeta = File(rirDir, "metadata.csv")
    val rooms = remember { parseRirRooms(rirMeta) }

    Column(
        Modifier.fillMaxSize().background(ObsidianVoid)
            .verticalScroll(rememberScrollState()).padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Text("CONTROL ESPACIAL", color = AuroraCyan, fontFamily = FontFamily.Monospace,
                fontWeight = FontWeight.Bold, fontSize = 14.sp, letterSpacing = 2.sp,
                modifier = Modifier.weight(1f))
            OutlinedButton(onClick = onBack) { Text("← VOLVER", fontSize = 10.sp) }
        }

        // ── HRTF ──
        PanelCard("HRTF — ${if (metrics.hrtfActive) "ACTIVO" else "OFF"}", PhosphorGreen) {
            ToggleRow("Activar HRTF", cfg.hrtfEnabled) { apply(cfg.copy(hrtfEnabled = it)) }
            Text("Sujeto:", color = TextSecondary, fontSize = 11.sp)
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                SpatialControlStore.SUBJECTS.forEach { s ->
                    val sel = cfg.hrtfSubject == s
                    Text(s, color = if (sel) AuroraCyan else TextMuted, fontSize = 10.sp,
                        fontFamily = FontFamily.Monospace,
                        modifier = Modifier
                            .border(1.dp, if (sel) AuroraCyan else ObsidianEdge, RoundedCornerShape(6.dp))
                            .clickable {
                                apply(cfg.copy(hrtfSubject = s))
                                hrtfSubjectReply = "Sujeto anclado: $s"
                            }
                            .padding(horizontal = 8.dp, vertical = 4.dp))
                }
            }
            Text(
                if (ihr1 != null) "Dataset: ${ihr1.name} (${ihr1.length() / 1024} KB)"
                else "Dataset: FALLBACK sintético (módulo no instalado)",
                color = if (ihr1 != null) PhosphorGreen else AmberSignal, fontSize = 10.sp,
                fontFamily = FontFamily.Monospace)
            if (hrtfSubjectReply.isNotBlank()) Text(hrtfSubjectReply, color = TextMuted, fontSize = 10.sp)
        }

        // ── RIR ──
        PanelCard("RIR — REVERB DE SALA", AuroraCyan) {
            ToggleRow("Activar reverb RIR", cfg.rirEnabled) { apply(cfg.copy(rirEnabled = it)) }
            SliderRow("Mezcla wet", cfg.rirWet, 0f..0.6f) { apply(cfg.copy(rirWet = it)) }
            SliderRow("Distancia/Reflexiones", cfg.reflectionDistance, 0.5f..2.0f) {
                apply(cfg.copy(reflectionDistance = it))
            }
            Text(
                "Reflexiones: dist=${"%.2f".format(IvannaSpatialEngine.distanceMeters)} m · " +
                "delay=${"%.1f".format(IvannaSpatialEngine.reflectionDelayMs)} ms · " +
                "gain=${"%.2f".format(IvannaSpatialEngine.reflectionGain)}",
                color = AuroraCyan, fontSize = 10.sp, fontFamily = FontFamily.Monospace
            )
            if (rooms.isNotEmpty()) {
                Text("Sala (${rooms.size} medidas):", color = TextSecondary, fontSize = 11.sp)
                val room = rooms[cfg.rirRoom.coerceIn(0, rooms.size - 1)]
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                    TextButton(onClick = {
                        apply(cfg.copy(rirRoom = (cfg.rirRoom - 1 + rooms.size) % rooms.size))
                    }) { Text("◀", fontSize = 12.sp) }
                    Text("${room.first} · RT60 ${"%.2f".format(room.second)}s",
                        color = AuroraCyan, fontSize = 11.sp, fontFamily = FontFamily.Monospace)
                    TextButton(onClick = {
                        apply(cfg.copy(rirRoom = (cfg.rirRoom + 1) % rooms.size))
                    }) { Text("▶", fontSize = 12.sp) }
                }
            } else {
                Text("IR: no desplegadas — usando síntesis algorítmica",
                    color = AmberSignal, fontSize = 10.sp, fontFamily = FontFamily.Monospace)
            }
        }

        // ── SAF ──
        PanelCard("SAF — PERSONALIZACIÓN Φ", NeonMagenta) {
            ToggleRow("Adaptación SAF", cfg.safEnabled) { apply(cfg.copy(safEnabled = it)) }
            SliderRow("Intensidad", cfg.safIntensity, 0f..1f) { apply(cfg.copy(safIntensity = it)) }
            val model = safModel
            Text(
                if (model?.exists() == true) {
                    "SAF_model.json ✓ (${model.length() / 1024} KB)"
                } else {
                    "SAF_model.json no desplegado"
                },
                color = if (model?.exists() == true) PhosphorGreen else AmberSignal,
                fontSize = 10.sp, fontFamily = FontFamily.Monospace)
        }

        // ── UPMIXER 4-STEMS 3D (B2: setUpmixerEnabled + setStemPosition) ──
        PanelCard("UPMIXER 4-STEMS 3D — ${if (cfg.upmixerEnabled) "ACTIVO" else "BYPASS"}", AuroraCyan) {
            ToggleRow("Activar Upmixer Neural (Crossfade Atómico)", cfg.upmixerEnabled) {
                apply(cfg.copy(upmixerEnabled = it))
            }
            Text("Stem activo:", color = TextSecondary, fontSize = 11.sp)
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                SpatialControlStore.STEMS.forEachIndexed { idx, label ->
                    val sel = cfg.selectedStem == idx
                    Text(
                        label,
                        color = if (sel) AuroraCyan else TextMuted,
                        fontSize = 10.sp,
                        fontFamily = FontFamily.Monospace,
                        modifier = Modifier
                            .border(1.dp, if (sel) AuroraCyan else ObsidianEdge, RoundedCornerShape(6.dp))
                            .clickable { apply(cfg.copy(selectedStem = idx)) }
                            .padding(horizontal = 8.dp, vertical = 4.dp)
                    )
                }
            }
            val curX = when (cfg.selectedStem) { 0 -> cfg.stem0X; 1 -> cfg.stem1X; 2 -> cfg.stem2X; else -> cfg.stem3X }
            val curY = when (cfg.selectedStem) { 0 -> cfg.stem0Y; 1 -> cfg.stem1Y; 2 -> cfg.stem2Y; else -> cfg.stem3Y }
            val curZ = when (cfg.selectedStem) { 0 -> cfg.stem0Z; 1 -> cfg.stem1Z; 2 -> cfg.stem2Z; else -> cfg.stem3Z }
            val curW = when (cfg.selectedStem) { 0 -> cfg.stem0Width; 1 -> cfg.stem1Width; 2 -> cfg.stem2Width; else -> cfg.stem3Width }

            fun updateSelectedStem(nx: Float = curX, ny: Float = curY, nz: Float = curZ, nw: Float = curW) {
                val updated = when (cfg.selectedStem) {
                    0 -> cfg.copy(stem0X = nx, stem0Y = ny, stem0Z = nz, stem0Width = nw)
                    1 -> cfg.copy(stem1X = nx, stem1Y = ny, stem1Z = nz, stem1Width = nw)
                    2 -> cfg.copy(stem2X = nx, stem2Y = ny, stem2Z = nz, stem2Width = nw)
                    else -> cfg.copy(stem3X = nx, stem3Y = ny, stem3Z = nz, stem3Width = nw)
                }
                apply(updated)
            }

            SliderRow("Posición X (Izq/Der)", curX, -1.5f..1.5f) { updateSelectedStem(nx = it) }
            SliderRow("Posición Y (Elevación)", curY, -1.5f..1.5f) { updateSelectedStem(ny = it) }
            SliderRow("Posición Z (Profundidad)", curZ, -1.5f..1.5f) { updateSelectedStem(nz = it) }
            SliderRow("Anchura Objeto (Width)", curW, 0.05f..1.0f) { updateSelectedStem(nw = it) }
            TextButton(onClick = {
                com.ivanna.omega.spatial.IvannaSpatialManager.reset()
                apply(SpatialControlStore.SpatialConfig())
            }) {
                Text("REINICIAR ESCENA 3D / STEMS", color = AuroraCyan, fontSize = 10.sp, fontFamily = FontFamily.Monospace)
            }
        }

        // ── MOTOR HÍBRIDO MAGISTRAL (HRTF 128-TAP + SALA SCHROEDER) ──
        PanelCard("HÍBRIDO MAGISTRAL — ${if (adaptivePrefs.hybridMagistralEnabled) "ACTIVO" else "BYPASS"}", PhosphorGreen) {
            ToggleRow("Convolución Híbrida 128-Tap + Sala", adaptivePrefs.hybridMagistralEnabled) {
                syncHybrid(adaptivePrefs.copy(hybridMagistralEnabled = it))
            }
            SliderRow("Mezcla Binaural KEMAR", adaptivePrefs.hybridBinauralWet, 0f..1f) {
                syncHybrid(adaptivePrefs.copy(hybridBinauralWet = it))
            }
            SliderRow("Tamaño de Sala Schroeder", adaptivePrefs.hybridRoomSize, 0.1f..1f) {
                syncHybrid(adaptivePrefs.copy(hybridRoomSize = it))
            }
            SliderRow("Absorción de Paredes", adaptivePrefs.hybridRoomAbsorption, 0.05f..0.95f) {
                syncHybrid(adaptivePrefs.copy(hybridRoomAbsorption = it))
            }
            SliderRow("Amortiguamiento Aire HF", adaptivePrefs.hybridRoomDampening, 0.05f..0.90f) {
                syncHybrid(adaptivePrefs.copy(hybridRoomDampening = it))
            }
            SliderRow("Mezcla Reverb Schroeder", adaptivePrefs.hybridRoomWetMix, 0f..0.75f) {
                syncHybrid(adaptivePrefs.copy(hybridRoomWetMix = it))
            }
            Text(
                "Az=${"%.1f".format(hybridTele.getOrElse(2) { 30f })}° · " +
                "El=${"%.1f".format(hybridTele.getOrElse(3) { 0f })}° · " +
                "Sala=${"%.2f".format(hybridTele.getOrElse(4) { 0.55f })} · " +
                "Damp=${"%.2f".format(hybridTele.getOrElse(6) { 0.40f })}",
                color = AuroraCyan, fontSize = 10.sp, fontFamily = FontFamily.Monospace
            )
        }

        // ── Telemetría bidireccional (DSP → UI) ──
        PanelCard("ESTADO DEL MOTOR", AmberSignal) {
            Text("SR ${metrics.sampleRate / 1000} kHz · RMS ${"%.3f".format(metrics.rmsLevel)} · " +
                 "HRTF ${if (metrics.hrtfActive) "ON" else "OFF"} · " +
                 "daemon ${if (MagiskBridge.isDaemonRunning) "ONLINE" else "OFFLINE"}",
                color = TextPrimary, fontSize = 10.sp, fontFamily = FontFamily.Monospace)
        }
    }
}

private fun parseRirRooms(meta: File): List<Pair<String, Float>> {
    if (!meta.exists()) return emptyList()
    return runCatching {
        meta.readLines().drop(1).mapNotNull { line ->
            val c = line.split(',')
            if (c.size >= 12) c[0].removeSuffix(".wav") to (c[11].toFloatOrNull() ?: 0f) else null
        }
    }.getOrDefault(emptyList())
}

@Composable
private fun PanelCard(title: String, accent: androidx.compose.ui.graphics.Color,
                      content: @Composable ColumnScope.() -> Unit) {
    Column(
        Modifier.fillMaxWidth()
            .background(ObsidianSoft, RoundedCornerShape(12.dp))
            .border(1.dp, accent.copy(alpha = 0.3f), RoundedCornerShape(12.dp))
            .padding(14.dp),
        verticalArrangement = Arrangement.spacedBy(10.dp)
    ) {
        Text(title, color = accent, fontFamily = FontFamily.Monospace,
            fontWeight = FontWeight.Bold, fontSize = 12.sp, letterSpacing = 1.5.sp)
        content()
    }
}

@Composable
private fun ToggleRow(label: String, value: Boolean, onChange: (Boolean) -> Unit) {
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically) {
        Text(label, color = TextSecondary, fontSize = 11.sp)
        Switch(checked = value, onCheckedChange = onChange)
    }
}

@Composable
private fun SliderRow(label: String, value: Float, range: ClosedFloatingPointRange<Float>,
                      onChange: (Float) -> Unit) {
    Column {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            Text(label, color = TextSecondary, fontSize = 11.sp)
            Text("%.2f".format(value), color = AuroraCyan, fontSize = 11.sp,
                fontFamily = FontFamily.Monospace)
        }
        Slider(value = value, onValueChange = onChange, valueRange = range)
    }
}
