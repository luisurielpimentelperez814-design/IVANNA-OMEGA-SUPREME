package com.ivanna.omega.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.platform.LocalContext
import com.ivanna.omega.audio.AudioStateManager
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.core.NativeBridge
import com.ivanna.omega.dsp.DSPBridge
import com.ivanna.omega.ui.theme.*

/**
 * SoundScreen — Sección SONIDO unificada.
 *
 * Reemplaza y unifica: OpeEngineScreen + BinauralScreen + controles dispersos de IvannaControlPanel.
 * Conecta funciones nativas antes sin UI:
 *   · nativeSetDelta   → velocidad del AGC
 *   · nativeSetEta     → wet NHO/PDEngine
 *   · nativeSetNPMax   → target del AGC
 *   · nativeSetHRTFEnabled → toggle HRTF real
 *   · nativeSetAdaptEnabled → toggle motor adaptativo
 */
@Composable
fun SoundScreen(modifier: Modifier = Modifier) {
    val context = LocalContext.current
    val audioState by AudioStateManager.audioState.collectAsState()
    var selectedTab by remember { mutableIntStateOf(0) }
    val tabs = listOf("EQ", "DINÁMICA", "BINAURAL", "NHO", "FFT")

    // Estado persistente levantado (bugs C/D/E + EQ reset) — cargado una vez, guardado en cada cambio
    var prefs by remember { mutableStateOf(AdaptiveControlsPrefs.load(context)) }
    fun updatePrefs(update: (AdaptiveControlsState) -> AdaptiveControlsState) {
        prefs = update(prefs)
        AdaptiveControlsPrefs.save(context, prefs)
    }

    // FIX: EQ/Compresor/Gain se perdían en cada reinicio porque AudioState
    // es in-memory. Restaurar desde prefs y replicar al motor nativo al abrir.
    LaunchedEffect(Unit) {
        val p = prefs
        AudioStateManager.updateState {
            it.copy(
                eqBass              = p.eqBass,
                eqMid               = p.eqMid,
                eqTreble            = p.eqTreble,
                eqPresence          = p.eqPresence,
                masterGain          = p.masterGain,
                compressorThreshold = p.compressorThreshold,
                compressorRatio     = p.compressorRatio,
                compressorAttack    = p.compressorAttack,
                compressorRelease   = p.compressorRelease
            )
        }
        if (IvannaNativeLib.isLoaded) {
            runCatching {
                IvannaNativeLib.nativeSetEQParams(p.eqBass, p.eqMid, p.eqTreble, p.masterGain)
                IvannaNativeLib.nativeSetPresenceDb(p.eqPresence)
                IvannaNativeLib.nativeSetCompressorParams(
                    p.compressorThreshold, p.compressorRatio,
                    p.compressorAttack, p.compressorRelease
                )
            }
        }
    }

    Column(modifier = modifier.background(ObsidianDeep)) {

        // ── Header ──────────────────────────────────────────────────────────
        Text(
            "SONIDO",
            color = AuroraCyan,
            fontSize = 11.sp,
            fontWeight = FontWeight.ExtraBold,
            letterSpacing = 3.sp,
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 12.dp)
        )

        // ── Tabs ────────────────────────────────────────────────────────────
        ScrollableTabRow(
            selectedTabIndex = selectedTab,
            containerColor = Color.Transparent,
            contentColor = AuroraCyan,
            edgePadding = 16.dp
        ) {
            tabs.forEachIndexed { i, title ->
                Tab(
                    selected = selectedTab == i,
                    onClick = { selectedTab = i },
                    text = {
                        Text(
                            title,
                            fontSize = 11.sp,
                            fontWeight = if (selectedTab == i) FontWeight.Bold else FontWeight.Normal,
                            color = if (selectedTab == i) AuroraCyan else TextMuted
                        )
                    }
                )
            }
        }

        HorizontalDivider(color = ObsidianEdge, thickness = 0.5.dp)

        // ── Contenido por tab ────────────────────────────────────────────────
        Column(
            modifier = Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 16.dp, vertical = 12.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            when (selectedTab) {
                0 -> EQTab(prefs, ::updatePrefs)
                1 -> DynamicsTab(prefs, ::updatePrefs)
                2 -> BinauralTab(prefs, ::updatePrefs)
                3 -> NHOTab(prefs, ::updatePrefs)
                4 -> FftOscilloscopePanel()
            }
        }
    }
}

// ── Tab EQ ──────────────────────────────────────────────────────────────────
@Composable
private fun EQTab(
    prefs: AdaptiveControlsState,
    updatePrefs: ((AdaptiveControlsState) -> AdaptiveControlsState) -> Unit
) {
    val audioState by AudioStateManager.audioState.collectAsState()
    val ctx = LocalContext.current
    val selectedKind by com.ivanna.omega.audio.ContentProfileEngine.selected.collectAsState()
    val activeKind by com.ivanna.omega.audio.ContentProfileEngine.active.collectAsState()

    GlassCard(
        "PERFIL POR CONTENIDO", NeonMagenta,
        "Activo: ${activeKind?.label ?: "—"} · EQ + dinámica + espacial coordinados"
    ) {
        Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
            com.ivanna.omega.audio.ContentKind.values().toList().chunked(3).forEach { rowKinds ->
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    rowKinds.forEach { kind ->
                        FilterChip(
                            selected = selectedKind == kind,
                            onClick = {
                                com.ivanna.omega.audio.ContentProfileEngine.select(ctx, kind)
                            },
                            label = { Text(kind.label, fontSize = 10.sp) },
                            modifier = Modifier.weight(1f)
                        )
                    }
                }
            }
        }
    }
    Spacer(Modifier.height(8.dp))

    GlassCard("ECUALIZADOR PARAMÉTRICO", AuroraCyan, "8 bandas · Q adaptativo · ISO 226") {
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            // FIX (auditoría 2026-08-12): "ISO 226" en el subtítulo de arriba
            // era decorativo — el panel real con datos en vivo está debajo.
            Bark64VisualizerPanel(modifier = Modifier.fillMaxWidth())
            IvannaSliderRow("GRAVES", audioState.eqBass, -18f, 18f, "dB") { v ->
                AudioStateManager.updateState { it.copy(eqBass = v) }
                updatePrefs { it.copy(eqBass = v) }
                if (IvannaNativeLib.isLoaded)
                    runCatching { IvannaNativeLib.nativeSetEQParams(v, audioState.eqMid, audioState.eqTreble, audioState.masterGain) }
            }
            IvannaSliderRow("MEDIOS", audioState.eqMid, -18f, 18f, "dB") { v ->
                AudioStateManager.updateState { it.copy(eqMid = v) }
                updatePrefs { it.copy(eqMid = v) }
                if (IvannaNativeLib.isLoaded)
                    runCatching { IvannaNativeLib.nativeSetEQParams(audioState.eqBass, v, audioState.eqTreble, audioState.masterGain) }
            }
            IvannaSliderRow("AGUDOS", audioState.eqTreble, -18f, 18f, "dB") { v ->
                AudioStateManager.updateState { it.copy(eqTreble = v) }
                updatePrefs { it.copy(eqTreble = v) }
                if (IvannaNativeLib.isLoaded)
                    runCatching { IvannaNativeLib.nativeSetEQParams(audioState.eqBass, audioState.eqMid, v, audioState.masterGain) }
            }
            // FIX: PRESENCIA tenía su propio campo hasta que alguien lo reemplazó
            // por `audioState.spatialWidth * 6f - 3f` — el ancho estéreo
            // interpretado como ganancia de EQ. Ahora usa audioState.eqPresence
            // (campo dedicado) y llama nativeSetEQParams con los 4 valores reales.
            IvannaSliderRow("PRESENCIA", audioState.eqPresence, -12f, 12f, "dB") { v ->
                AudioStateManager.updateState { it.copy(eqPresence = v) }
                updatePrefs { it.copy(eqPresence = v) }
                if (IvannaNativeLib.isLoaded)
                    runCatching { IvannaNativeLib.nativeSetPresenceDb(v) }
            }
            // FIX: VOLUMEN actualizaba audioState.masterGain pero no rellamaba
            // nativeSetEQParams — el motor seguía con el masterGain anterior.
            IvannaSliderRow("VOLUMEN", audioState.masterGain, 0.5f, 2f, "x") { v ->
                AudioStateManager.updateState { it.copy(masterGain = v) }
                updatePrefs { it.copy(masterGain = v) }
                if (IvannaNativeLib.isLoaded)
                    runCatching {
                        IvannaNativeLib.nativeSetEQParams(
                            audioState.eqBass, audioState.eqMid, audioState.eqTreble, v
                        )
                    }
            }
        }
    }

    Spacer(Modifier.height(8.dp))
    // FIX: Iso226StatusPanel solo mostraba el estado (read-only) pero no
    // permitía al usuario cambiar el nivel de escucha ni aplicar la compensación.
    // Iso226CalibratorPanel añade sliders + preview de curva + botón APLICAR
    // que llama Iso226Calibrator.applyAll() en las 3 capas: EQ + DSPBridge + daemon.
    Iso226CalibratorPanel(modifier = Modifier.fillMaxWidth())
    Spacer(Modifier.height(8.dp))
    // Distinto del slider "PRESENCIA" de arriba (Ruta A, in-process,
    // nativeSetHarmonicGain — solo mientras la app tiene foco/está en
    // primer plano). Este panel controla la Ruta B (daemon system-wide,
    // vía SET_PERCEPTUAL_STATE) — afecta TODO el audio del teléfono,
    // incluso con la app en background.
    HarmonicExciterPanel(modifier = Modifier.fillMaxWidth())
}

// ── Tab DINÁMICA ─────────────────────────────────────────────────────────────
@Composable
private fun DynamicsTab(
    prefs: AdaptiveControlsState,
    updatePrefs: ((AdaptiveControlsState) -> AdaptiveControlsState) -> Unit
) {
    val audioState by AudioStateManager.audioState.collectAsState()

    GlassCard("COMPRESOR", NeonMagenta, "Soft knee 6dB · Look-ahead 64ms") {
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            IvannaSliderRow("UMBRAL", audioState.compressorThreshold, -60f, 0f, "dB") { v ->
                AudioStateManager.updateState { it.copy(compressorThreshold = v) }
                updatePrefs { it.copy(compressorThreshold = v) }
                if (IvannaNativeLib.isLoaded)
                    runCatching { IvannaNativeLib.nativeSetCompressorParams(v, audioState.compressorRatio, audioState.compressorAttack, audioState.compressorRelease) }
            }
            IvannaSliderRow("RATIO", audioState.compressorRatio, 1f, 20f, ":1") { v ->
                AudioStateManager.updateState { it.copy(compressorRatio = v) }
                updatePrefs { it.copy(compressorRatio = v) }
                if (IvannaNativeLib.isLoaded)
                    runCatching { IvannaNativeLib.nativeSetCompressorParams(audioState.compressorThreshold, v, audioState.compressorAttack, audioState.compressorRelease) }
            }
            IvannaSliderRow("ATAQUE", audioState.compressorAttack, 0.1f, 200f, "ms") { v ->
                AudioStateManager.updateState { it.copy(compressorAttack = v) }
                updatePrefs { it.copy(compressorAttack = v) }
                // nativeSetGamma NO es del compresor: mueve el ángulo espacial
                // (g_pd.set_spatial_angle). El ataque va solo por setCompressorParams.
                if (IvannaNativeLib.isLoaded) {
                    runCatching { IvannaNativeLib.nativeSetCompressorParams(audioState.compressorThreshold, audioState.compressorRatio, v, audioState.compressorRelease) }
                }
            }
            IvannaSliderRow("RELEASE", audioState.compressorRelease, 10f, 2000f, "ms") { v ->
                AudioStateManager.updateState { it.copy(compressorRelease = v) }
                updatePrefs { it.copy(compressorRelease = v) }
                if (IvannaNativeLib.isLoaded)
                    runCatching { IvannaNativeLib.nativeSetCompressorParams(audioState.compressorThreshold, audioState.compressorRatio, audioState.compressorAttack, v) }
            }
        }
    }

    Spacer(Modifier.height(4.dp))

    // Bug C fix — estado levantado a prefs en lugar de remember local
    GlassCard("AGC · LOUDNESS LUFS", AuroraCyan, "Motor A · PDEngine · ITU-R BS.1770") {
        var loudnessTrimOn by remember { mutableStateOf(true) }
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("LOUDNESS TRIM ACTIVO", color = TextSecondary, fontSize = 11.sp, modifier = Modifier.weight(1f))
                Switch(
                    checked = loudnessTrimOn,
                    onCheckedChange = { en ->
                        loudnessTrimOn = en
                        if (IvannaNativeLib.isLoaded) {
                            runCatching { IvannaNativeLib.nativeSetLoudnessTrimEnabled(en) }
                        }
                    }
                )
            }
            IvannaSliderRow("TARGET AGC / LUFS", prefs.agcTarget, -36f, -6f, "LUFS") { v ->
                updatePrefs { it.copy(agcTarget = v) }
                if (IvannaNativeLib.isLoaded) {
                    runCatching { IvannaNativeLib.nativeSetLoudnessTarget(v) }
                }
                runCatching { com.ivanna.omega.neuromorphic.PiLstmBridge.setAgc(v, prefs.agcRate) }
            }
            IvannaSliderRow("VELOCIDAD", prefs.agcRate, 0f, 1f, "") { v ->
                updatePrefs { it.copy(agcRate = v) }
                // nativeSetDelta es el ancho espacial del PDEngine, no la velocidad del AGC.
                runCatching { com.ivanna.omega.neuromorphic.PiLstmBridge.setAgc(prefs.agcTarget, v) }
            }
        }
    }
}

// ── Tab BINAURAL ──────────────────────────────────────────────────────────────
@Composable
private fun BinauralTab(
    prefs: AdaptiveControlsState,
    updatePrefs: ((AdaptiveControlsState) -> AdaptiveControlsState) -> Unit
) {
    val audioState by AudioStateManager.audioState.collectAsState()
    // Bug B fix — hrtfEnabled derivado del audioState para que re-sincronice
    // si la fuente cambia externamente (no val plana ni remember sin key)
    val hrtfEnabled by remember { derivedStateOf { audioState.binaural } }
    var hybridTele by remember { mutableStateOf(NativeBridge.safeGetHybridMagistralTelemetry()) }

    fun syncHybridToNative(s: AdaptiveControlsState) {
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
    }

    LaunchedEffect(Unit) {
        syncHybridToNative(prefs)
        while (true) {
            hybridTele = NativeBridge.safeGetHybridMagistralTelemetry()
            kotlinx.coroutines.delay(250L)
        }
    }

    GlassCard("HRTF BINAURAL", AuroraCyan, "KEMAR subject_165 · 24 azimuts · 7 elevaciones") {
        Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("HRTF ACTIVO", color = TextSecondary, fontSize = 12.sp, modifier = Modifier.weight(1f))
                Switch(
                    checked = hrtfEnabled,
                    onCheckedChange = { en ->
                        AudioStateManager.updateState { it.copy(binaural = en) }
                        if (IvannaNativeLib.isLoaded) {
                            runCatching { IvannaNativeLib.nativeSetHRTFEnabled(en) }
                            runCatching { IvannaNativeLib.nativeSetBinauralEnabled(en) }
                        }
                        com.ivanna.omega.spatial.IvannaSpatialEngine.enabled = en
                    }
                )
            }
            // Bug D fix — adaptEnabled/azimuth/elevation levantados a prefs
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("MOTOR ADAPTATIVO", color = TextSecondary, fontSize = 12.sp, modifier = Modifier.weight(1f))
                Switch(
                    checked = prefs.binauralAdaptEnabled,
                    onCheckedChange = { en ->
                        updatePrefs { it.copy(binauralAdaptEnabled = en) }
                        if (IvannaNativeLib.isLoaded) runCatching { IvannaNativeLib.nativeSetAdaptiveEngineEnabled(en) }
                    }
                )
            }
            IvannaSliderRow("AZIMUT", prefs.binauralAzimuth, -180f, 180f, "°") { v ->
                val next = prefs.copy(binauralAzimuth = v)
                updatePrefs { next }
                val rad = v * Math.PI.toFloat() / 180f
                com.ivanna.omega.spatial.IvannaSpatialEngine.setAzimuth(rad)
                if (IvannaNativeLib.isLoaded) {
                    runCatching { IvannaNativeLib.nativeSetSpatialAngleRad(rad) }
                    runCatching { IvannaNativeLib.nativeSetBinauralPositionRad(rad, (audioState.spatialWidth / 2f).coerceIn(0f, 1f)) }
                }
                syncHybridToNative(next)
            }
            // FIX: ELEVACIÓN persistía en prefs pero no llamaba a ningún motor
            // espacial ni función nativa — la elevación era decorativa.
            // Wired → IvannaSpatialEngine.setElevation + HybridRenderer nativo.
            IvannaSliderRow("ELEVACIÓN", prefs.binauralElevation, -45f, 45f, "°") { v ->
                val next = prefs.copy(binauralElevation = v)
                updatePrefs { next }
                val rad = v * Math.PI.toFloat() / 180f
                com.ivanna.omega.spatial.IvannaSpatialEngine.setElevation(rad)
                syncHybridToNative(next)
            }
            IvannaSliderRow("ANCHO ESPACIAL", audioState.spatialWidth, 0f, 2f, "x") { v ->
                AudioStateManager.updateState { it.copy(spatialWidth = v) }
                com.ivanna.omega.spatial.IvannaSpatialEngine.setWidth(v)
                if (IvannaNativeLib.isLoaded) runCatching { IvannaNativeLib.nativeSetSpatialWidthDirect(v) }
            }
        }
    }

    Spacer(Modifier.height(8.dp))

    GlassCard(
        "MOTOR HÍBRIDO MAGISTRAL",
        NeonMagenta,
        "FIR KEMAR 128-Tap + Sala Acústica Schroeder/Moorer + Difusión All-Pass"
    ) {
        Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("CONVOLUCIÓN HÍBRIDA + SALA ACTIVA", color = TextSecondary, fontSize = 12.sp, modifier = Modifier.weight(1f))
                Switch(
                    checked = prefs.hybridMagistralEnabled,
                    onCheckedChange = { en ->
                        val next = prefs.copy(hybridMagistralEnabled = en)
                        updatePrefs { next }
                        syncHybridToNative(next)
                    }
                )
            }
            IvannaSliderRow("MEZCLA BINAURAL 128-TAP", prefs.hybridBinauralWet, 0f, 1f, "") { v ->
                val next = prefs.copy(hybridBinauralWet = v)
                updatePrefs { next }
                syncHybridToNative(next)
            }
            IvannaSliderRow("TAMAÑO DE SALA ACÚSTICA", prefs.hybridRoomSize, 0.1f, 1f, "x") { v ->
                val next = prefs.copy(hybridRoomSize = v)
                updatePrefs { next }
                syncHybridToNative(next)
            }
            IvannaSliderRow("ABSORCIÓN DE PAREDES", prefs.hybridRoomAbsorption, 0.05f, 0.95f, "") { v ->
                val next = prefs.copy(hybridRoomAbsorption = v)
                updatePrefs { next }
                syncHybridToNative(next)
            }
            IvannaSliderRow("AMORTIGUAMIENTO AIRE HF", prefs.hybridRoomDampening, 0.05f, 0.90f, "") { v ->
                val next = prefs.copy(hybridRoomDampening = v)
                updatePrefs { next }
                syncHybridToNative(next)
            }
            IvannaSliderRow("REVERB SALA SCHROEDER (WET)", prefs.hybridRoomWetMix, 0f, 0.75f, "") { v ->
                val next = prefs.copy(hybridRoomWetMix = v)
                updatePrefs { next }
                syncHybridToNative(next)
            }
            Text(
                "DSP EN VIVO: ${if (hybridTele.getOrElse(0) { 1f } > 0.5f) "ONLINE" else "BYPASS"} · " +
                "AZ=${"%.1f".format(hybridTele.getOrElse(2) { 30f })}° · " +
                "EL=${"%.1f".format(hybridTele.getOrElse(3) { 0f })}° · " +
                "RT-SALA=${"%.2f".format(hybridTele.getOrElse(4) { 0.55f })} · " +
                "DAMP=${"%.2f".format(hybridTele.getOrElse(6) { 0.40f })}",
                color = PhosphorGreen,
                fontSize = 10.sp,
                fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace
            )
        }
    }
}

// ── Tab NHO ───────────────────────────────────────────────────────────────────
// Bug E fix — los 4 sliders levantados a prefs; antes cada switch de tab los reseteaba
@Composable
// ── Tab NHO ───────────────────────────────────────────────────────────────────
// Bug E fix — los 4 sliders levantados a prefs; antes cada switch de tab los reseteaba
private fun NHOTab(
    prefs: AdaptiveControlsState,
    updatePrefs: ((AdaptiveControlsState) -> AdaptiveControlsState) -> Unit
) {
    GlassCard("MOTOR NHO · PDEngine", NeonMagenta, "Oscilador Neuroharmónico · Drive no lineal") {
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            IvannaSliderRow("WET NHO (η)", prefs.nhoEta, 0f, 1f, "") { v ->
                updatePrefs { it.copy(nhoEta = v) }
                if (IvannaNativeLib.isLoaded) runCatching { IvannaNativeLib.nativeSetEta(v) }
            }
            IvannaSliderRow("GANANCIA ARMÓNICA", prefs.nhoHarmonicGain, 0f, 1f, "") { v ->
                updatePrefs { it.copy(nhoHarmonicGain = v) }
                if (IvannaNativeLib.isLoaded) runCatching { IvannaNativeLib.nativeSetHarmonicGain(v) }
            }
            IvannaSliderRow("INHIBICIÓN LATERAL", prefs.nhoLateralInhib, 0f, 1f, "") { v ->
                updatePrefs { it.copy(nhoLateralInhib = v) }
                if (IvannaNativeLib.isLoaded) runCatching { IvannaNativeLib.nativeSetBeta(v) }
            }
            IvannaSliderRow("GAIN OHC", prefs.nhoOhcGain, 0f, 1f, "") { v ->
                updatePrefs { it.copy(nhoOhcGain = v) }
                if (IvannaNativeLib.isLoaded) runCatching { IvannaNativeLib.nativeSetAlpha(v) }
            }
        }
    }
    Spacer(modifier = Modifier.height(12.dp))
    // ── Eje Supremo Neuroacústico: Inversión Biomecánica Coclear (PINN) ───────
    // Controla CochlearActiveInverseEngine (C++20, NEON, 8 bandas Greenwood).
    // Ruta JNI: CochlearInverseViewModel → IvannaNativeLib.nativeSetCochlear*()
    //   → g_cochlearEnabled / g_cochlearIntensity (atomic) → hot-path de audio.
    CochlearInverseCard()
}

// ── Componente slider reutilizable ────────────────────────────────────────────
@Composable
private fun IvannaSliderRow(
    label: String,
    value: Float,
    min: Float,
    max: Float,
    unit: String,
    onValueChange: (Float) -> Unit
) {
    Column {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(label, color = TextSecondary, fontSize = 11.sp, fontWeight = FontWeight.Medium)
            Text(
                "${if (unit == "dB" || unit == "°") "%+.1f".format(value) else "%.2f".format(value)} $unit",
                color = AuroraCyan,
                fontSize = 11.sp,
                fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace
            )
        }
        Slider(
            value = value,
            onValueChange = onValueChange,
            valueRange = min..max,
            colors = SliderDefaults.colors(
                thumbColor = AuroraCyan,
                activeTrackColor = AuroraCyan,
                inactiveTrackColor = ObsidianEdge
            ),
            modifier = Modifier.fillMaxWidth()
        )
    }
}
