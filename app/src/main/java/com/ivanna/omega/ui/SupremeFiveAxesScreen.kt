package com.ivanna.omega.ui

import android.os.Process
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.KeyboardArrowRight
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.core.NativeBridge
import com.ivanna.omega.ui.theme.*
import kotlinx.coroutines.delay

/**
 * Telemetría en tiempo real leída directamente del pipeline C++23 vía NativeBridge JNI.
 */
data class SupremeAxesLiveTelemetry(
    val subSampleDelaySamples: Float = 0.24f,
    val maxPhaseDerivativeRad: Float = 0.012f,
    val snnActiveSpikes: Int = 2,
    val pinnaNotchFreqHz: Float = 7800f,
    val pinnaItdMicroSec: Float = 620f,
    val shmOwnerPid: Int = 0
)

@Composable
fun rememberSupremeAxesTelemetry(): State<SupremeAxesLiveTelemetry> {
    return produceState(initialValue = SupremeAxesLiveTelemetry()) {
        while (true) {
            value = SupremeAxesLiveTelemetry(
                subSampleDelaySamples = NativeBridge.safeGetWarpedLatticeSubSampleDelay(),
                maxPhaseDerivativeRad = NativeBridge.safeGetTransharmonicPhaseStep(),
                snnActiveSpikes       = NativeBridge.safeGetSnnActiveSpikes(),
                pinnaNotchFreqHz      = NativeBridge.safeGetPinnaActiveNotchHz(),
                pinnaItdMicroSec      = NativeBridge.safeGetPinnaActiveItdUs(),
                shmOwnerPid           = NativeBridge.safeGetShmOwnerPid()
            )
            delay(250L)
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  EJE 1 CARD: Inversión Activa de Transductores (Celosía Deformada λ + Bl(x))
// ═══════════════════════════════════════════════════════════════════════════════
@Composable
fun WarpedLatticeAxisCard(
    state: SupremeAxesState,
    telemetry: SupremeAxesLiveTelemetry,
    onUpdate: ((SupremeAxesState) -> SupremeAxesState) -> Unit,
    onOpenDetail: (() -> Unit)? = null
) {
    val haptic = LocalHapticFeedback.current
    GlassCard(
        title = "EJE 1 · CELOSÍA DEFORMADA (ANTI-DIRAC)",
        accent = if (state.warpedLatticeEnabled) AuroraCyan else TextMuted,
        subtitle = "Warped Lattice Z(ω) · Excursión Bl(x) · Micro-Chirp 17.5–19 kHz",
        rightSlot = {
            ToggleSwitch(
                checked = state.warpedLatticeEnabled,
                accent = AuroraCyan,
                onCheckedChange = { en ->
                    haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                    onUpdate { it.copy(warpedLatticeEnabled = en) }
                    NativeBridge.safeSetWarpedLatticeEnabled(en)
                }
            )
        }
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            StatBlock(
                label = "τ_g INVERSO",
                value = "%.3f smp".format(telemetry.subSampleDelaySamples),
                accent = AuroraCyan,
                modifier = Modifier.weight(1f)
            )
            StatBlock(
                label = "BARK λ(SR)",
                value = "%.3f".format(state.warpedLatticeLambda),
                accent = PhosphorGreen,
                modifier = Modifier.weight(1f)
            )
            StatBlock(
                label = "SONDA CHIRP",
                value = if (state.warpedLatticeMicroChirp) "18.2 kHz" else "OFF",
                accent = if (state.warpedLatticeMicroChirp) AmberSignal else TextMuted,
                modifier = Modifier.weight(1f)
            )
        }

        AuroraSlider(
            label = "COMPENSACIÓN NO-LINEAL Bl(x) (VOLTERRA INVERSO)",
            value = state.warpedLatticeBlDrive,
            range = 0f..1f,
            displayValue = { "%.0f %%".format(it * 100f) },
            onValueChange = { drive ->
                onUpdate { it.copy(warpedLatticeBlDrive = drive) }
                NativeBridge.safeSetWarpedLatticeBlDrive(drive)
            }
        )

        AuroraSlider(
            label = "FACTOR DE DEFORMACIÓN ALOPASO BARK λ",
            value = state.warpedLatticeLambda,
            range = 0.50f..0.85f,
            displayValue = { "λ = %.3f".format(it) },
            onValueChange = { lam ->
                onUpdate { it.copy(warpedLatticeLambda = lam) }
                NativeBridge.safeSetWarpedLatticeLambda(lam)
            }
        )

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                "Inyección Micro-Chirp Psicoacústico (-78 dBFS en silencios)",
                color = TextSecondary,
                fontSize = 11.sp,
                modifier = Modifier.weight(1f)
            )
            Switch(
                checked = state.warpedLatticeMicroChirp,
                onCheckedChange = { chirp ->
                    onUpdate { it.copy(warpedLatticeMicroChirp = chirp) }
                    NativeBridge.safeSetWarpedLatticeMicroChirp(chirp)
                }
            )
        }

        if (onOpenDetail != null) {
            DetailRouteButton("ABRIR LABORATORIO EJE 1 (CELOSÍA DEFORMADA)", AuroraCyan, onOpenDetail)
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  EJE 2 CARD: Reconstrucción Transarmónica CVNN + DDSP (Evolución Anti-DSEE)
// ═══════════════════════════════════════════════════════════════════════════════
@Composable
fun TransharmonicCvnnAxisCard(
    state: SupremeAxesState,
    telemetry: SupremeAxesLiveTelemetry,
    onUpdate: ((SupremeAxesState) -> SupremeAxesState) -> Unit,
    onOpenDetail: (() -> Unit)? = null
) {
    val haptic = LocalHapticFeedback.current
    GlassCard(
        title = "EJE 2 · TRANSARMÓNICO CVNN+DDSP (ANTI-DSEE)",
        accent = if (state.transharmonicCvnnEnabled) NeonMagenta else TextMuted,
        subtitle = "Transformada Analítica Hilbert · Fase Instantánea · Supresión IMD H2",
        rightSlot = {
            ToggleSwitch(
                checked = state.transharmonicCvnnEnabled,
                accent = NeonMagenta,
                onCheckedChange = { en ->
                    haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                    onUpdate { it.copy(transharmonicCvnnEnabled = en) }
                    NativeBridge.safeSetTransharmonicCvnnEnabled(en)
                }
            )
        }
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            StatBlock(
                label = "Δφ̇ MÁX",
                value = "%.4f rad".format(telemetry.maxPhaseDerivativeRad),
                accent = PhosphorGreen,
                modifier = Modifier.weight(1f)
            )
            StatBlock(
                label = "BANDA DDSP",
                value = "> 16.0 kHz",
                accent = NeonMagenta,
                modifier = Modifier.weight(1f)
            )
            StatBlock(
                label = "IMD CANCEL",
                value = "%.0f %%".format(state.transharmonicImdCancel * 100f),
                accent = AuroraCyan,
                modifier = Modifier.weight(1f)
            )
        }

        AuroraSlider(
            label = "GANANCIA TRANSARMÓNICA COHERENTE EN FASE (k=2,3)",
            value = state.transharmonicHarmonicGain,
            range = 0f..1f,
            displayValue = { "%.0f %%".format(it * 100f) },
            onValueChange = { g ->
                onUpdate { it.copy(transharmonicHarmonicGain = g) }
                NativeBridge.safeSetTransharmonicHarmonicGain(g)
            }
        )

        AuroraSlider(
            label = "CANCELACIÓN DESTRUCTIVA IMD (VOLTERRA 2º ORDEN)",
            value = state.transharmonicImdCancel,
            range = 0f..1f,
            displayValue = { "%.0f %%".format(it * 100f) },
            onValueChange = { imd ->
                onUpdate { it.copy(transharmonicImdCancel = imd) }
                NativeBridge.safeSetTransharmonicImdCancel(imd)
            }
        )

        if (onOpenDetail != null) {
            DetailRouteButton("ABRIR LABORATORIO EJE 2 (CVNN + DDSP HILBERT)", NeonMagenta, onOpenDetail)
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  EJE 3 CARD: Separación Ciega SNN INT8 + NMF -> HOA 4º Orden (Anti-Dolby)
// ═══════════════════════════════════════════════════════════════════════════════
@Composable
fun SnnNmfHoaAxisCard(
    state: SupremeAxesState,
    telemetry: SupremeAxesLiveTelemetry,
    onUpdate: ((SupremeAxesState) -> SupremeAxesState) -> Unit,
    onOpenDetail: (() -> Unit)? = null
) {
    val haptic = LocalHapticFeedback.current
    GlassCard(
        title = "EJE 3 · SNN INT8 + NMF → HOA 4º ORDEN (ANTI-DOLBY)",
        accent = if (state.snnHoaUpmixerEnabled) PhosphorGreen else TextMuted,
        subtitle = "4 Flujos Ortogonales · 16 Canales Esféricos · UPOLA · < 1 mW Event-Driven",
        rightSlot = {
            ToggleSwitch(
                checked = state.snnHoaUpmixerEnabled,
                accent = PhosphorGreen,
                onCheckedChange = { en ->
                    haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                    onUpdate { it.copy(snnHoaUpmixerEnabled = en) }
                    NativeBridge.safeSetSnnHoaUpmixerEnabled(en)
                }
            )
        }
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            StatBlock(
                label = "LIF SPIKES",
                value = "${telemetry.snnActiveSpikes} / 4",
                accent = PhosphorGreen,
                modifier = Modifier.weight(1f)
            )
            StatBlock(
                label = "HOA ORDEN",
                value = "4º (16 ch)",
                accent = AuroraCyan,
                modifier = Modifier.weight(1f)
            )
            StatBlock(
                label = "SCHED_FIFO",
                value = if (state.snnSchedFifoPromoted) "PRIO 85" else "CFS STD",
                accent = if (state.snnSchedFifoPromoted) AmberSignal else TextMuted,
                modifier = Modifier.weight(1f)
            )
        }

        AuroraSlider(
            label = "INMERSIVIDAD BINAURAL ESFÉRICA HOA 4º ORDEN",
            value = state.snnHoaImmersivity,
            range = 0f..1f,
            displayValue = { "%.0f %%".format(it * 100f) },
            onValueChange = { w ->
                onUpdate { it.copy(snnHoaImmersivity = w) }
                NativeBridge.safeSetSnnHoaImmersivity(w)
            }
        )

        AuroraSlider(
            label = "UMBRAL DE POTENCIAL DE MEMBRANA LIF SNN (V_th)",
            value = state.snnSpikeThreshold,
            range = 0.15f..1.50f,
            displayValue = { "%.2f V".format(it) },
            onValueChange = { th ->
                onUpdate { it.copy(snnSpikeThreshold = th) }
                NativeBridge.safeSetSnnSpikeThreshold(th)
            }
        )

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                "Hilo RT SCHED_FIFO (Prioridad 85 · Cero Bombeo Acústico)",
                color = TextSecondary,
                fontSize = 11.sp,
                modifier = Modifier.weight(1f)
            )
            Switch(
                checked = state.snnSchedFifoPromoted,
                onCheckedChange = { promote ->
                    if (promote) NativeBridge.safePromoteSnnThreadToSchedFifo(85)
                    onUpdate { it.copy(snnSchedFifoPromoted = promote) }
                }
            )
        }

        if (onOpenDetail != null) {
            DetailRouteButton("ABRIR LABORATORIO EJE 3 (SNN + NMF HOA 16CH)", PhosphorGreen, onOpenDetail)
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  EJE 4 CARD: Calibración Fotogramétrica INR-SDF del Pabellón Auricular
// ═══════════════════════════════════════════════════════════════════════════════
@Composable
fun PinnaManifoldAxisCard(
    state: SupremeAxesState,
    telemetry: SupremeAxesLiveTelemetry,
    onUpdate: ((SupremeAxesState) -> SupremeAxesState) -> Unit,
    onOpenDetail: (() -> Unit)? = null
) {
    val haptic = LocalHapticFeedback.current
    GlassCard(
        title = "EJE 4 · PINNA MANIFOLD INR-SDF (ANTI-APPLE)",
        accent = if (state.pinnaManifoldEnabled) AmberSignal else TextMuted,
        subtitle = "MLP SIREN 2 Capas · Descenso Gradiente 3 Pasos · FIR 32-Tap Fase Mínima",
        rightSlot = {
            ToggleSwitch(
                checked = state.pinnaManifoldEnabled,
                accent = AmberSignal,
                onCheckedChange = { en ->
                    haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                    onUpdate { it.copy(pinnaManifoldEnabled = en) }
                    NativeBridge.safeSetPinnaManifoldEnabled(en)
                }
            )
        }
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            StatBlock(
                label = "NOTCH PINNA",
                value = "%.0f Hz".format(telemetry.pinnaNotchFreqHz),
                accent = AmberSignal,
                modifier = Modifier.weight(1f)
            )
            StatBlock(
                label = "ITD LATENTE",
                value = "%.1f μs".format(telemetry.pinnaItdMicroSec),
                accent = AuroraCyan,
                modifier = Modifier.weight(1f)
            )
            StatBlock(
                label = "SÍNTESIS FIR",
                value = "32-Tap MinΦ",
                accent = PhosphorGreen,
                modifier = Modifier.weight(1f)
            )
        }

        AuroraSlider(
            label = "MEZCLA WET/DRY DEL FILTRO FIR PERSONALIZADO",
            value = state.pinnaManifoldWetMix,
            range = 0f..1f,
            displayValue = { "%.0f %%".format(it * 100f) },
            onValueChange = { wet ->
                onUpdate { it.copy(pinnaManifoldWetMix = wet) }
                NativeBridge.safeSetPinnaManifoldWetMix(wet)
            }
        )

        AuroraSlider(
            label = "PROFUNDIDAD DE CONCHA CAVUM (z₀ LATENTE)",
            value = state.pinnaConchaDepth,
            range = -1f..1f,
            displayValue = { "%+.2f".format(it) },
            onValueChange = { c ->
                onUpdate { it.copy(pinnaConchaDepth = c) }
                NativeBridge.safeCalibratePinnaManifold(c, state.pinnaHelixCurl, state.pinnaHeadWidth)
            }
        )

        AuroraSlider(
            label = "CURVATURA DEL HÉLIX / ANTI-HÉLIX (z₁ LATENTE)",
            value = state.pinnaHelixCurl,
            range = -1f..1f,
            displayValue = { "%+.2f".format(it) },
            onValueChange = { h ->
                onUpdate { it.copy(pinnaHelixCurl = h) }
                NativeBridge.safeCalibratePinnaManifold(state.pinnaConchaDepth, h, state.pinnaHeadWidth)
            }
        )

        AuroraSlider(
            label = "ANCHO INTERAURAL CEFÁLICO (z₂ LATENTE)",
            value = state.pinnaHeadWidth,
            range = -1f..1f,
            displayValue = { "%+.2f".format(it) },
            onValueChange = { w ->
                onUpdate { it.copy(pinnaHeadWidth = w) }
                NativeBridge.safeCalibratePinnaManifold(state.pinnaConchaDepth, state.pinnaHelixCurl, w)
            }
        )

        OutlinedButton(
            onClick = {
                haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                NativeBridge.safeCalibratePinnaManifold(
                    state.pinnaConchaDepth,
                    state.pinnaHelixCurl,
                    state.pinnaHeadWidth
                )
            },
            modifier = Modifier.fillMaxWidth(),
            border = BorderStroke(1.dp, AmberSignal)
        ) {
            Text(
                "EJECUTAR DESCENSO DE GRADIENTE INR-SDF (3 PASOS < 1 ms)",
                color = AmberSignal,
                fontSize = 10.sp,
                fontWeight = FontWeight.Bold,
                fontFamily = FontFamily.Monospace
            )
        }

        if (onOpenDetail != null) {
            DetailRouteButton("ABRIR LABORATORIO EJE 4 (MANIFOLD INR-SDF)", AmberSignal, onOpenDetail)
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  EJE 5 CARD: Arbitraje SHM Lockless CAS + Alineación MSO Farrow 5º Orden
// ═══════════════════════════════════════════════════════════════════════════════
@Composable
fun ShmFarrowArbitratorAxisCard(
    state: SupremeAxesState,
    telemetry: SupremeAxesLiveTelemetry,
    onUpdate: ((SupremeAxesState) -> SupremeAxesState) -> Unit,
    onOpenDetail: (() -> Unit)? = null
) {
    val haptic = LocalHapticFeedback.current
    val myPid = remember { runCatching { Process.myPid() }.getOrDefault(1001) }

    GlassCard(
        title = "EJE 5 · ARBITRAJE SHM + FARROW 5º ORDEN MSO",
        accent = if (state.farrowMsoEnabled) AuroraCyan else TextMuted,
        subtitle = "Bypass AudioFlinger eBPF/XDP · Lockless CAS owner_pid · Retardo Sub-ns",
        rightSlot = {
            ToggleSwitch(
                checked = state.farrowMsoEnabled,
                accent = AuroraCyan,
                onCheckedChange = { en ->
                    haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                    onUpdate { it.copy(farrowMsoEnabled = en) }
                    NativeBridge.safeSetFarrowMsoEnabled(en)
                }
            )
        }
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            StatBlock(
                label = "OWNER PID",
                value = if (telemetry.shmOwnerPid == 0) "LIBRE (0)" else "PID ${telemetry.shmOwnerPid}",
                accent = if (telemetry.shmOwnerPid != 0) PhosphorGreen else TextMuted,
                modifier = Modifier.weight(1f)
            )
            StatBlock(
                label = "ITD MSO",
                value = "%+.0f ns".format(state.msoItdNanoseconds),
                accent = AuroraCyan,
                modifier = Modifier.weight(1f)
            )
            StatBlock(
                label = "eBPF XDP",
                value = if (state.ebpfBypassActive) "BYPASS ON" else "HAL STD",
                accent = if (state.ebpfBypassActive) CoralWarn else TextMuted,
                modifier = Modifier.weight(1f)
            )
        }

        AuroraSlider(
            label = "ALINEACIÓN DE FASE INTERAURAL MSO (FARROW 5º ORDEN)",
            value = state.msoItdNanoseconds,
            range = -50000f..50000f,
            displayValue = { "%+.0f ns (%.2f μs)".format(it, it / 1000f) },
            onValueChange = { ns ->
                onUpdate { it.copy(msoItdNanoseconds = ns) }
                NativeBridge.safeSetMsoItdNanoseconds(ns)
            }
        )

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                "Bypass Kernel eBPF/XDP Direct-to-HAL (Sin AudioFlinger)",
                color = TextSecondary,
                fontSize = 11.sp,
                modifier = Modifier.weight(1f)
            )
            Switch(
                checked = state.ebpfBypassActive,
                onCheckedChange = { active ->
                    onUpdate { it.copy(ebpfBypassActive = active) }
                    NativeBridge.safeSetEbpfBypassActive(active)
                }
            )
        }

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            OutlinedButton(
                onClick = {
                    haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                    val ok = NativeBridge.safeAcquireShmArbitration(myPid)
                    if (ok) onUpdate { it.copy(shmArbitrationLockedByApp = true) }
                },
                modifier = Modifier.weight(1f),
                border = BorderStroke(1.dp, PhosphorGreen)
            ) {
                Text("ADQUIRIR SHM CAS ($myPid)", color = PhosphorGreen, fontSize = 10.sp, fontWeight = FontWeight.Bold)
            }
            OutlinedButton(
                onClick = {
                    haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                    NativeBridge.safeReleaseShmArbitration(myPid)
                    onUpdate { it.copy(shmArbitrationLockedByApp = false) }
                },
                modifier = Modifier.weight(1f),
                border = BorderStroke(1.dp, CoralWarn)
            ) {
                Text("LIBERAR LEASE SHM", color = CoralWarn, fontSize = 10.sp, fontWeight = FontWeight.Bold)
            }
        }

        if (onOpenDetail != null) {
            DetailRouteButton("ABRIR LABORATORIO EJE 5 (SHM CAS + FARROW 5º)", AuroraCyan, onOpenDetail)
        }
    }
}

@Composable
private fun DetailRouteButton(label: String, accent: Color, onClick: () -> Unit) {
    Surface(
        onClick = onClick,
        modifier = Modifier.fillMaxWidth(),
        color = accent.copy(alpha = 0.10f),
        shape = RoundedCornerShape(8.dp),
        border = BorderStroke(1.dp, accent.copy(alpha = 0.45f))
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 12.dp, vertical = 9.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                text = label,
                color = accent,
                fontSize = 10.sp,
                fontWeight = FontWeight.Bold,
                fontFamily = FontFamily.Monospace,
                letterSpacing = 0.8.sp
            )
            Icon(Icons.Default.KeyboardArrowRight, contentDescription = null, tint = accent, modifier = Modifier.size(16.dp))
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  PANTALLA MAESTRA: HUB DE LOS 5 EJES DE SUPREMACÍA NEUROACÚSTICA
// ═══════════════════════════════════════════════════════════════════════════════
@Composable
fun SupremeFiveAxesHubScreen(
    onBack: () -> Unit,
    onOpenAxis1: () -> Unit = {},
    onOpenAxis2: () -> Unit = {},
    onOpenAxis3: () -> Unit = {},
    onOpenAxis4: () -> Unit = {},
    onOpenAxis5: () -> Unit = {}
) {
    val context = LocalContext.current
    var state by remember { mutableStateOf(SupremeAxesPrefs.load(context)) }
    val telemetry by rememberSupremeAxesTelemetry()

    fun updateState(transform: (SupremeAxesState) -> SupremeAxesState): SupremeAxesState {
        val next = transform(state)
        state = next
        SupremeAxesPrefs.save(context, next)
        return next
    }

    LaunchedEffect(Unit) {
        SupremeAxesPrefs.applyToNative(state)
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(ObsidianVoid)
            .windowInsetsPadding(WindowInsets.systemBars)
    ) {
        // Top Header
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .background(ObsidianSoft)
                .padding(horizontal = 12.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            IconButton(onClick = onBack) {
                Icon(Icons.Default.ArrowBack, contentDescription = "Atrás", tint = AuroraCyan)
            }
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    "5 EJES DE SUPREMACÍA NEUROACÚSTICA",
                    color = AuroraCyan,
                    fontSize = 13.sp,
                    fontWeight = FontWeight.ExtraBold,
                    letterSpacing = 1.4.sp
                )
                Text(
                    "C++23 Zero-Copy · Lock-Free · NEON · < 2.5 ms Round-Trip",
                    color = TextMuted,
                    fontSize = 10.sp,
                    fontFamily = FontFamily.Monospace
                )
            }
        }

        Column(
            modifier = Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            // Master Quick Action Bar (Activar Todos / Bypass Todos)
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                OutlinedButton(
                    onClick = {
                        val next = updateState {
                            it.copy(
                                warpedLatticeEnabled     = true,
                                transharmonicCvnnEnabled = true,
                                snnHoaUpmixerEnabled     = true,
                                pinnaManifoldEnabled     = true,
                                farrowMsoEnabled         = true
                            )
                        }
                        SupremeAxesPrefs.applyToNative(next)
                    },
                    modifier = Modifier.weight(1f),
                    border = BorderStroke(1.dp, PhosphorGreen)
                ) {
                    Text("ACTIVAR 5 EJES", color = PhosphorGreen, fontSize = 11.sp, fontWeight = FontWeight.Bold)
                }
                OutlinedButton(
                    onClick = {
                        val next = updateState {
                            it.copy(
                                warpedLatticeEnabled     = false,
                                transharmonicCvnnEnabled = false,
                                snnHoaUpmixerEnabled     = false,
                                pinnaManifoldEnabled     = false,
                                farrowMsoEnabled         = false
                            )
                        }
                        SupremeAxesPrefs.applyToNative(next)
                    },
                    modifier = Modifier.weight(1f),
                    border = BorderStroke(1.dp, CoralWarn)
                ) {
                    Text("BYPASS GLOBAL", color = CoralWarn, fontSize = 11.sp, fontWeight = FontWeight.Bold)
                }
            }

            WarpedLatticeAxisCard(state, telemetry, ::updateState, onOpenDetail = onOpenAxis1)
            TransharmonicCvnnAxisCard(state, telemetry, ::updateState, onOpenDetail = onOpenAxis2)
            SnnNmfHoaAxisCard(state, telemetry, ::updateState, onOpenDetail = onOpenAxis3)
            PinnaManifoldAxisCard(state, telemetry, ::updateState, onOpenDetail = onOpenAxis4)
            ShmFarrowArbitratorAxisCard(state, telemetry, ::updateState, onOpenDetail = onOpenAxis5)
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
//  PANTALLAS DEDICADAS POR EJE (1 A 5) CON ESPECIFICACIÓN MATEMÁTICA Y TELEMETRÍA
// ═══════════════════════════════════════════════════════════════════════════════

@Composable
private fun DedicatedAxisScaffold(
    title: String,
    subtitle: String,
    accent: Color,
    equationSpec: String,
    architectureNotes: String,
    onBack: () -> Unit,
    content: @Composable ColumnScope.() -> Unit
) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(ObsidianVoid)
            .windowInsetsPadding(WindowInsets.systemBars)
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .background(ObsidianSoft)
                .padding(horizontal = 12.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            IconButton(onClick = onBack) {
                Icon(Icons.Default.ArrowBack, contentDescription = "Atrás", tint = accent)
            }
            Column(modifier = Modifier.weight(1f)) {
                Text(title, color = accent, fontSize = 13.sp, fontWeight = FontWeight.ExtraBold, letterSpacing = 1.2.sp)
                Text(subtitle, color = TextMuted, fontSize = 10.sp, fontFamily = FontFamily.Monospace)
            }
        }
        Column(
            modifier = Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            content()

            GlassCard(
                title = "FUNDAMENTO MATEMÁTICO & KERNEL C++23",
                accent = accent,
                subtitle = "Implementación Zero-Copy Lock-Free verificada en Host & Dispositivo"
            ) {
                Text(
                    text = equationSpec,
                    color = AuroraCyan,
                    fontSize = 11.sp,
                    fontFamily = FontFamily.Monospace,
                    lineHeight = 16.sp
                )
                HorizontalDivider(color = ObsidianEdge)
                Text(
                    text = architectureNotes,
                    color = TextSecondary,
                    fontSize = 11.sp,
                    lineHeight = 16.sp
                )
            }
        }
    }
}

@Composable
fun WarpedLatticeAxisScreen(onBack: () -> Unit) {
    val context = LocalContext.current
    var state by remember { mutableStateOf(SupremeAxesPrefs.load(context)) }
    val telemetry by rememberSupremeAxesTelemetry()
    fun update(f: (SupremeAxesState) -> SupremeAxesState) {
        state = f(state)
        SupremeAxesPrefs.save(context, state)
    }

    DedicatedAxisScaffold(
        title = "EJE 1 · INVERSIÓN DE TRANSDUCTORES",
        subtitle = "WarpedLatticeTransducerInverter.hpp · Anti-Dirac",
        accent = AuroraCyan,
        equationSpec = "D(z) = (z⁻¹ - λ) / (1 - λ z⁻¹),  λ(fs) = 1.0674·(2/π·atan(0.06583·fs/1000))^½ - 0.1916\n" +
                       "f_m[n] = f_{m-1}[n] + k_m(x)·b_{m-1}[n],   Bl(x) = Bl₀·(1 - β₂ x² - β₄ x⁴)",
        architectureNotes = "• 8 etapas en celosía deformada Bark con desenrollado estático y cero asignaciones dinámicas.\n" +
                            "• Sonda Micro-Chirp enmascarada psicoacústicamente (17.5 kHz – 19.0 kHz, -78 dBFS) activa sólo en silencios (E < -62 dBFS).\n" +
                            "• Cálculo en tiempo real del retardo de grupo inverso sub-muestra τ_g(ω) ∈ [0, 1).",
        onBack = onBack
    ) {
        WarpedLatticeAxisCard(state, telemetry, ::update, onOpenDetail = null)
    }
}

@Composable
fun TransharmonicCvnnAxisScreen(onBack: () -> Unit) {
    val context = LocalContext.current
    var state by remember { mutableStateOf(SupremeAxesPrefs.load(context)) }
    val telemetry by rememberSupremeAxesTelemetry()
    fun update(f: (SupremeAxesState) -> SupremeAxesState) {
        state = f(state)
        SupremeAxesPrefs.save(context, state)
    }

    DedicatedAxisScaffold(
        title = "EJE 2 · SÍNTESIS TRANSARMÓNICA CVNN",
        subtitle = "PhaseCoherentTransharmonicSynthesizer.hpp · Anti-DSEE",
        accent = NeonMagenta,
        equationSpec = "z[n] = x[n] + j·H{x[n]},   ω_inst[n] = arg(z[n] · z*[n-1])\n" +
                       "σ_modReLU(z) = ReLU(|z| + b) · z / (|z| + ε),   y_IMD[n] = 2·a₂·e_low[n]·x_high[n]",
        architectureNotes = "• Red Neuronal de Valores Complejos (CVNN) con activación modReLU analítica que preserva la fase exacta.\n" +
                            "• Osciladores DDSP bloqueados en fase (k=2, 3) para reconstruir la banda >16 kHz sin aspereza metálica.\n" +
                            "• Cancelador destructivo de productos de intermodulación (IMD) de Volterra de 2º orden.",
        onBack = onBack
    ) {
        TransharmonicCvnnAxisCard(state, telemetry, ::update, onOpenDetail = null)
    }
}

@Composable
fun SnnNmfHoaAxisScreen(onBack: () -> Unit) {
    val context = LocalContext.current
    var state by remember { mutableStateOf(SupremeAxesPrefs.load(context)) }
    val telemetry by rememberSupremeAxesTelemetry()
    fun update(f: (SupremeAxesState) -> SupremeAxesState) {
        state = f(state)
        SupremeAxesPrefs.save(context, state)
    }

    DedicatedAxisScaffold(
        title = "EJE 3 · SEPARACIÓN CIEGA SNN + HOA 4º",
        subtitle = "SnnNmfHoaUpmixer.hpp · Anti-Dolby Event-Driven",
        accent = PhosphorGreen,
        equationSpec = "u_i[n] = α_leak·u_i[n-1] + Σ W_ij^{(q8)}·I_j[n] - V_th·s_i[n-1]\n" +
                       "B_lm[n] = Σ_{k=0..3} S_k[n] · Y_l^m(θ_k, φ_k),   (L+1)² = 16 canales (Orden 4)",
        architectureNotes = "• Spiking Neural Network (SNN) cuantizada en INT8 (< 1 mW) que inicializa las máscaras de Factorización de Matrices No Negativas (NMF).\n" +
                            "• Descomposición ortogonal sin bombeo acústico en 4 flujos: Centro, Lateral, Reflexiones Tempranas y Cola Difusa.\n" +
                            "• Proyección en armónicos esféricos reales ACN/SN3D de 4º orden (16 canales) y decodificación binaural UPOLA.",
        onBack = onBack
    ) {
        SnnNmfHoaAxisCard(state, telemetry, ::update, onOpenDetail = null)
    }
}

@Composable
fun PinnaManifoldAxisScreen(onBack: () -> Unit) {
    val context = LocalContext.current
    var state by remember { mutableStateOf(SupremeAxesPrefs.load(context)) }
    val telemetry by rememberSupremeAxesTelemetry()
    fun update(f: (SupremeAxesState) -> SupremeAxesState) {
        state = f(state)
        SupremeAxesPrefs.save(context, state)
    }

    DedicatedAxisScaffold(
        title = "EJE 4 · CALIBRACIÓN PINNA INR-SDF",
        subtitle = "PinnaManifoldInterpolator.hpp · Anti-Apple",
        accent = AmberSignal,
        equationSpec = "Φ_SDF(x; z) = W₂ · sin(ω₀ · (W₁[x ∥ z] + b₁)) + b₂\n" +
                       "z^{(t+1)} = z^{(t)} - η · ∇_z L_SDF(z^{(t)}),   h_min[n] = F⁻¹{exp(H{ln|H(ω)|})}",
        architectureNotes = "• Representación Neuronal Implícita (INR) con activación periódica SIREN que modela el Campo de Distancia Firmado (SDF) 3D del pabellón auricular.\n" +
                            "• Refinamiento por descenso de gradiente de 3 pasos sobre 6 parámetros antropométricos latentes en < 1 ms (presupuesto < 100 ms).\n" +
                            "• Síntesis de filtros FIR binaurales de 32 taps de fase mínima en el dispositivo sin archivos SOFA pesados.",
        onBack = onBack
    ) {
        PinnaManifoldAxisCard(state, telemetry, ::update, onOpenDetail = null)
    }
}

@Composable
fun ShmFarrowArbitratorAxisScreen(onBack: () -> Unit) {
    val context = LocalContext.current
    var state by remember { mutableStateOf(SupremeAxesPrefs.load(context)) }
    val telemetry by rememberSupremeAxesTelemetry()
    fun update(f: (SupremeAxesState) -> SupremeAxesState) {
        state = f(state)
        SupremeAxesPrefs.save(context, state)
    }

    DedicatedAxisScaffold(
        title = "EJE 5 · ARBITRAJE SHM & FARROW 5º MSO",
        subtitle = "ShmPipelineArbitrator.hpp · Kernel Bypass & Sub-ns ITD",
        accent = AuroraCyan,
        equationSpec = "CAS(&owner_pid, expected=0, desired=pid_daemon)\n" +
                       "y[n] = ((((c₅·d + c₄)·d + c₃)·d + c₂)·d + c₁)·d + c₀   (Horner 5º Orden)",
        architectureNotes = "• Bloque de control en memoria compartida (SHM) alineado a 64 bytes con arbitraje Compare-And-Swap (CAS) sin mutexes y recuperación automática de lease.\n" +
                            "• Filtro de retardo fraccional de Lagrange/Farrow de 5º orden (6 taps) evaluado con regla de Horner para alineación de fase de la Oliva Superior Medial (MSO) con precisión de nanosegundos.",
        onBack = onBack
    ) {
        ShmFarrowArbitratorAxisCard(state, telemetry, ::update, onOpenDetail = null)
    }
}
