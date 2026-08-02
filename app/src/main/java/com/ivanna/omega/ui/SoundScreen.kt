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
import com.ivanna.omega.audio.AudioStateManager
import com.ivanna.omega.core.IvannaNativeLib
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
    val audioState by AudioStateManager.audioState.collectAsState()
    var selectedTab by remember { mutableIntStateOf(0) }
    val tabs = listOf("EQ", "DINÁMICA", "BINAURAL", "NHO")

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
                0 -> EQTab()
                1 -> DynamicsTab()
                2 -> BinauralTab()
                3 -> NHOTab()
            }
        }
    }
}

// ── Tab EQ ──────────────────────────────────────────────────────────────────
@Composable
private fun EQTab() {
    val audioState by AudioStateManager.audioState.collectAsState()

    GlassCard("ECUALIZADOR PARAMÉTRICO", AuroraCyan, "8 bandas · Q adaptativo · ISO 226") {
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            IvannaSliderRow("GRAVES", audioState.eqBass, -18f, 18f, "dB") { v ->
                AudioStateManager.updateState { it.copy(eqBass = v) }
                if (IvannaNativeLib.isLoaded)
                    runCatching { IvannaNativeLib.nativeSetEQParams(v, audioState.eqMid, audioState.eqTreble, audioState.masterGain) }
            }
            IvannaSliderRow("MEDIOS", audioState.eqMid, -18f, 18f, "dB") { v ->
                AudioStateManager.updateState { it.copy(eqMid = v) }
                if (IvannaNativeLib.isLoaded)
                    runCatching { IvannaNativeLib.nativeSetEQParams(audioState.eqBass, v, audioState.eqTreble, audioState.masterGain) }
            }
            IvannaSliderRow("AGUDOS", audioState.eqTreble, -18f, 18f, "dB") { v ->
                AudioStateManager.updateState { it.copy(eqTreble = v) }
                if (IvannaNativeLib.isLoaded)
                    runCatching { IvannaNativeLib.nativeSetEQParams(audioState.eqBass, audioState.eqMid, v, audioState.masterGain) }
            }
            IvannaSliderRow("PRESENCIA", audioState.spatialWidth * 6f - 3f, -12f, 12f, "dB") { v ->
                if (IvannaNativeLib.isLoaded)
                    runCatching { IvannaNativeLib.nativeSetHarmonicGain(v / 12f + 0.5f) }
            }
            IvannaSliderRow("VOLUMEN", audioState.masterGain, 0.5f, 2f, "x") { v ->
                AudioStateManager.updateState { it.copy(masterGain = v) }
            }
        }
    }
}

// ── Tab DINÁMICA ─────────────────────────────────────────────────────────────
@Composable
private fun DynamicsTab() {
    val audioState by AudioStateManager.audioState.collectAsState()

    GlassCard("COMPRESOR", NeonMagenta, "Soft knee 6dB · Look-ahead 64ms") {
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            IvannaSliderRow("UMBRAL", audioState.compressorThreshold, -60f, 0f, "dB") { v ->
                AudioStateManager.updateState { it.copy(compressorThreshold = v) }
                if (IvannaNativeLib.isLoaded)
                    runCatching { IvannaNativeLib.nativeSetCompressorParams(v, audioState.compressorRatio, audioState.compressorAttack, audioState.compressorRelease) }
            }
            IvannaSliderRow("RATIO", audioState.compressorRatio, 1f, 20f, ":1") { v ->
                AudioStateManager.updateState { it.copy(compressorRatio = v) }
                if (IvannaNativeLib.isLoaded)
                    runCatching { IvannaNativeLib.nativeSetCompressorParams(audioState.compressorThreshold, v, audioState.compressorAttack, audioState.compressorRelease) }
            }
            IvannaSliderRow("ATAQUE", audioState.compressorAttack, 0.1f, 200f, "ms") { v ->
                AudioStateManager.updateState { it.copy(compressorAttack = v) }
                if (IvannaNativeLib.isLoaded)
                    runCatching { IvannaNativeLib.nativeSetCompressorParams(audioState.compressorThreshold, audioState.compressorRatio, v, audioState.compressorRelease) }
            }
            IvannaSliderRow("RELEASE", audioState.compressorRelease, 10f, 2000f, "ms") { v ->
                AudioStateManager.updateState { it.copy(compressorRelease = v) }
                if (IvannaNativeLib.isLoaded)
                    runCatching { IvannaNativeLib.nativeSetCompressorParams(audioState.compressorThreshold, audioState.compressorRatio, audioState.compressorAttack, v) }
            }
        }
    }

    Spacer(Modifier.height(4.dp))

    GlassCard("AGC — Control Automático de Ganancia", AuroraCyan, "Motor A · PDEngine") {
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            // nativeSetNPMax — target del AGC (antes sin UI)
            var agcTarget by remember { mutableFloatStateOf(-18f) }
            IvannaSliderRow("TARGET AGC", agcTarget, -36f, 0f, "dB") { v ->
                agcTarget = v
                if (IvannaNativeLib.isLoaded)
                    runCatching { IvannaNativeLib.nativeSetNPMax(v / -36f) }
            }
            // nativeSetDelta — velocidad de respuesta del AGC (antes sin UI)
            var agcRate by remember { mutableFloatStateOf(0.35f) }
            IvannaSliderRow("VELOCIDAD", agcRate, 0f, 1f, "") { v ->
                agcRate = v
                if (IvannaNativeLib.isLoaded)
                    runCatching { IvannaNativeLib.nativeSetDelta(v) }
            }
        }
    }
}

// ── Tab BINAURAL ──────────────────────────────────────────────────────────────
@Composable
private fun BinauralTab() {
    val audioState by AudioStateManager.audioState.collectAsState()
    var hrtfEnabled by remember { mutableStateOf(audioState.binaural) }
    var adaptEnabled by remember { mutableStateOf(true) }
    var azimuth by remember { mutableFloatStateOf(0f) }
    var elevation by remember { mutableFloatStateOf(0f) }

    GlassCard("HRTF BINAURAL", AuroraCyan, "KEMAR subject_165 · 24 azimuts · 7 elevaciones") {
        Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("HRTF ACTIVO", color = TextSecondary, fontSize = 12.sp, modifier = Modifier.weight(1f))
                Switch(
                    checked = hrtfEnabled,
                    onCheckedChange = { en ->
                        hrtfEnabled = en
                        AudioStateManager.updateState { it.copy(binaural = en) }
                        // nativeSetHRTFEnabled — antes sin UI
                        if (IvannaNativeLib.isLoaded) runCatching { IvannaNativeLib.nativeSetHRTFEnabled(en) }
                        com.ivanna.omega.spatial.IvannaSpatialEngine.enabled = en
                    }
                )
            }
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("MOTOR ADAPTATIVO", color = TextSecondary, fontSize = 12.sp, modifier = Modifier.weight(1f))
                Switch(
                    checked = adaptEnabled,
                    onCheckedChange = { en ->
                        adaptEnabled = en
                        // nativeSetAdaptEnabled — antes sin UI
                        if (IvannaNativeLib.isLoaded) runCatching { IvannaNativeLib.nativeSetAdaptiveEngineEnabled(en) }
                    }
                )
            }
            IvannaSliderRow("AZIMUT", azimuth, -180f, 180f, "°") { v ->
                azimuth = v
                val rad = v * Math.PI.toFloat() / 180f
                com.ivanna.omega.spatial.IvannaSpatialEngine.setAzimuth(rad)
                if (IvannaNativeLib.isLoaded) runCatching { IvannaNativeLib.nativeSetSpatialAngleRad(rad) }
            }
            IvannaSliderRow("ELEVACIÓN", elevation, -45f, 45f, "°") { v ->
                elevation = v
            }
            IvannaSliderRow("ANCHO ESPACIAL", audioState.spatialWidth, 0f, 2f, "x") { v ->
                AudioStateManager.updateState { it.copy(spatialWidth = v) }
                com.ivanna.omega.spatial.IvannaSpatialEngine.setWidth(v)
                if (IvannaNativeLib.isLoaded) runCatching { IvannaNativeLib.nativeSetSpatialWidthDirect(v) }
            }
        }
    }
}

// ── Tab NHO ───────────────────────────────────────────────────────────────────
@Composable
private fun NHOTab() {
    GlassCard("MOTOR NHO · PDEngine", NeonMagenta, "Oscilador Neuroharmónico · Drive no lineal") {
        Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
            // nativeSetEta — wet del NHO (antes sin UI directa)
            var eta by remember { mutableFloatStateOf(0.5f) }
            IvannaSliderRow("WET NHO (η)", eta, 0f, 1f, "") { v ->
                eta = v
                if (IvannaNativeLib.isLoaded) runCatching { IvannaNativeLib.nativeSetEta(v) }
            }
            var harmonicGain by remember { mutableFloatStateOf(0.5f) }
            IvannaSliderRow("GANANCIA ARMÓNICA", harmonicGain, 0f, 1f, "") { v ->
                harmonicGain = v
                if (IvannaNativeLib.isLoaded) runCatching { IvannaNativeLib.nativeSetHarmonicGain(v) }
            }
            var lateralInhib by remember { mutableFloatStateOf(0.3f) }
            IvannaSliderRow("INHIBICIÓN LATERAL", lateralInhib, 0f, 1f, "") { v ->
                lateralInhib = v
                if (IvannaNativeLib.isLoaded) runCatching { IvannaNativeLib.nativeSetLateralInhib(v) }
            }
            var ohcGain by remember { mutableFloatStateOf(0.5f) }
            IvannaSliderRow("GAIN OHC", ohcGain, 0f, 1f, "") { v ->
                ohcGain = v
                if (IvannaNativeLib.isLoaded) runCatching { IvannaNativeLib.nativeSetOhcGain(v) }
            }
        }
    }
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
