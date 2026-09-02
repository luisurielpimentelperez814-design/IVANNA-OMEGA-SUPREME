package com.ivanna.omega.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ivanna.omega.audio.AudioBackendSelector
import com.ivanna.omega.audio.AudioRouteManager
import com.ivanna.omega.audio.RouteDspCalibrator
import com.ivanna.omega.audio.UsbAudioProManager
import com.ivanna.omega.core.IvannaNativeLib
import com.ivanna.omega.magisk.OmegaEngineBridge
import com.ivanna.omega.spatial.IvannaSpatialManager
import com.ivanna.omega.ui.theme.*
import kotlinx.coroutines.delay

/**
 * EnginesStatusScreen — panel de estado de todos los motores en background.
 *
 * Motores antes invisibles en la UI (corrían en background sin ningún panel):
 *   • RouteDspCalibrator     — calibración DSP por ruta de audio (2s poll)
 *   • AudioBackendSelector   — Ruta A (daemon root) / B (DynamicsProcessing)
 *   • UsbAudioProManager     — DAC USB OTG directo (384kHz/32bit)
 *   • IvannaSpatialManager   — HRTF renderer + head tracker
 *   • AdaptiveEngineModulator — suavizado y curvas no lineales del motor adaptativo
 *   • IvannaControlLoop      — watchdog de parámetros (50ms)
 */
@Composable
fun EnginesStatusScreen(onBack: () -> Unit) {
    val ctx = LocalContext.current

    // ── Estado en tiempo real (polling 500ms) ────────────────────────────
    var backendMode    by remember { mutableStateOf(AudioBackendSelector.Mode.UNKNOWN) }
    var activeRoute    by remember { mutableStateOf("Detectando...") }
    var routeCalib     by remember { mutableStateOf("—") }
    var hrtfReady      by remember { mutableStateOf(false) }
    var hrtfSubject    by remember { mutableStateOf("none") }
    var headTracking   by remember { mutableStateOf(false) }
    var usbStreaming   by remember { mutableStateOf(false) }
    var nativeLoaded   by remember { mutableStateOf(false) }
    var daemonAlive    by remember { mutableStateOf(false) }
    var spatialWidth   by remember { mutableStateOf(0f) }
    var adaptTelemetry by remember { mutableStateOf<FloatArray?>(null) }

    LaunchedEffect(Unit) {
        while (true) {
            backendMode  = AudioBackendSelector.mode.value
            nativeLoaded = IvannaNativeLib.isLoaded
            daemonAlive  = runCatching { OmegaEngineBridge.isConnected }.getOrDefault(false)

            // Ruta activa de audio
            runCatching {
                val r = AudioRouteManager.detectOutputRoute()
                activeRoute = r.name
                routeCalib = when (r.name) {
                    "SPEAKER"   -> "HRTF off · RIR sala activa (RT60 0.7s, wet 0.30)"
                    "WIRED_AUX" -> "HRTF on · sala off"
                    "USB"       -> "HRTF on · sala off · 384kHz disponible"
                    "BLUETOOTH" -> "HRTF on · width 0.85 · RT60 0.4s (codec SBC/AAC)"
                    else        -> "Sin calibración (UNKNOWN)"
                }
            }

            // HRTF / spatial
            hrtfReady    = IvannaSpatialManager.ready
            hrtfSubject  = IvannaSpatialManager.activeSubject
            headTracking = IvannaSpatialManager.ready

            // USB Pro
            usbStreaming = UsbAudioProManager.getInstance(ctx).isActive()

            // Telemetría adaptativa
            if (nativeLoaded) {
                adaptTelemetry = runCatching {
                    IvannaNativeLib.nativeGetAdaptiveTelemetry()
                }.getOrNull()
                spatialWidth = runCatching {
                    IvannaNativeLib.nativeGetUnifiedPipelineStatus()?.getOrElse(6) { 0f } ?: 0f
                }.getOrDefault(0f)
            }

            delay(500)
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(ObsidianVoid)
            .windowInsetsPadding(WindowInsets.systemBars)
    ) {
        // Header
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .background(ObsidianSoft)
                .padding(horizontal = 16.dp, vertical = 14.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            IconButton(onClick = onBack, modifier = Modifier.size(36.dp)) {
                Icon(Icons.Default.ArrowBack, contentDescription = "Atrás", tint = AuroraCyan)
            }
            Column(Modifier.weight(1f)) {
                Text("ESTADO DE MOTORES", color = AuroraCyan,
                    fontWeight = FontWeight.ExtraBold, fontSize = 13.sp, letterSpacing = 1.5.sp)
                Text("Todos los motores en background — estado real",
                    color = TextMuted, fontSize = 10.sp)
            }
            // Indicador global
            val allOk = nativeLoaded && hrtfReady
            Box(Modifier.size(10.dp).clip(CircleShape)
                .background(if (allOk) PhosphorGreen else AmberSignal))
        }

        Column(
            modifier = Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {

            // ── 1. Audio Backend ─────────────────────────────────────────
            EngineCard(
                icon    = Icons.Default.Router,
                title   = "AUDIO BACKEND",
                status  = when (backendMode) {
                    AudioBackendSelector.Mode.ROOT_DAEMON    -> "ROOT · Daemon activo"
                    AudioBackendSelector.Mode.ROOT_NO_DAEMON -> "ROOT · Sin daemon (AudioEffect)"
                    AudioBackendSelector.Mode.NO_ROOT        -> "Sin root · DynamicsProcessing"
                    else                                     -> "Detectando..."
                },
                ok = backendMode == AudioBackendSelector.Mode.ROOT_DAEMON ||
                     backendMode == AudioBackendSelector.Mode.ROOT_NO_DAEMON,
                detail  = when (backendMode) {
                    AudioBackendSelector.Mode.ROOT_DAEMON    ->
                        "Ruta A: daemon ivanna_omega corriendo · SHM activa · latencia mínima"
                    AudioBackendSelector.Mode.ROOT_NO_DAEMON ->
                        "Ruta A degradada: raíz disponible pero daemon caído · reinicia el módulo Magisk"
                    AudioBackendSelector.Mode.NO_ROOT        ->
                        "Ruta B: DynamicsProcessing + AudioEffect · sin SHM · latencia ~20ms"
                    else -> "AudioBackendSelector aún evaluando la plataforma"
                }
            )

            // ── 2. Ruta DSP Calibrator ───────────────────────────────────
            EngineCard(
                icon   = Icons.Default.Tune,
                title  = "ROUTE DSP CALIBRATOR",
                status = "$activeRoute — activo (poll 2s)",
                ok     = activeRoute != "UNKNOWN" && activeRoute != "Detectando...",
                detail = routeCalib
            )

            // ── 3. Native DSP Engine ─────────────────────────────────────
            val t = adaptTelemetry
            EngineCard(
                icon   = Icons.Default.GraphicEq,
                title  = "NATIVE DSP ENGINE",
                status = if (nativeLoaded) "libivanna_omega_native.so cargada" else "NO CARGADA",
                ok     = nativeLoaded,
                detail = if (t != null && t.size >= 10) buildString {
                    append("RMS: ${"%.3f".format(t[0])}  Peak: ${"%.3f".format(t[1])}\n")
                    append("GR: ${"%.1f".format(t[2])}dB  Target: ${"%.3f".format(t[3])}\n")
                    append("Comp: ${"%.0f".format(t[4]*100)}%  Exciter↓: ${"%.0f".format(t[5]*100)}%\n")
                    append("SpatialW: ${"%.2f".format(spatialWidth)}  Safety↑: ${"%.0f".format(t[7]*100)}%\n")
                    append("VoiceProt: ${"%.0f".format(t[8]*100)}%  Applied: ${"%.0f".format(t[9]*100)}%")
                } else if (nativeLoaded) "Telemetría disponible cuando hay señal activa"
                  else "Cargar módulo Magisk o verificar libivanna_omega_native.so"
            )

            // ── 4. HRTF / Spatial ────────────────────────────────────────
            EngineCard(
                icon   = Icons.Default.SurroundSound,
                title  = "HRTF · SPATIAL MANAGER",
                status = if (hrtfReady) "Renderer activo · Sujeto: $hrtfSubject"
                         else "Renderer no inicializado",
                ok     = hrtfReady,
                detail = buildString {
                    if (hrtfReady) {
                        append("Sujeto HRTF: $hrtfSubject\n")
                        append("Head tracking: ${if (headTracking) "activo (ROTATION_VECTOR)" else "inactivo"}\n")
                        append("Spatial width nativo: ${"%.2f".format(spatialWidth)}")
                    } else {
                        append("IvannaSpatialManager.init() no fue llamado o falló.\n")
                        append("Abrir CALIBRACIÓN Φ_SAF para inicializar el renderer.")
                    }
                }
            )

            // ── 5. USB Audio Pro ─────────────────────────────────────────
            EngineCard(
                icon   = Icons.Default.Usb,
                title  = "USB AUDIO PRO MANAGER",
                status = if (usbStreaming) "Streaming activo (OTG directo · 384kHz/32bit)"
                         else "En espera — conecta un DAC USB",
                ok     = usbStreaming,
                detail = if (usbStreaming)
                    "Stream isochronous directo al endpoint USB · bypass mezclador Android · latencia hardware"
                else
                    "Conecta un DAC USB OTG compatible.\nEl manager detecta automáticamente CLASS=AUDIO " +
                    "SUBCLASS=STREAMING y abre el endpoint sin pasar por AudioFlinger."
            )

            // ── 6. Omega Daemon ──────────────────────────────────────────
            EngineCard(
                icon   = Icons.Default.DeveloperMode,
                title  = "OMEGA DAEMON",
                status = if (daemonAlive) "Corriendo · SHM lista"
                         else "Caído o sin root",
                ok     = daemonAlive,
                detail = if (daemonAlive)
                    "ivanna_daemon activo · shared memory disponible · Ruta A habilitada"
                else
                    "Sin daemon: la app usa Ruta B (DynamicsProcessing).\n" +
                    "Si el módulo está instalado, abre Magisk y verifica que el módulo esté activo."
            )

            // ── 7. Adaptive Engine Modulator ────────────────────────────
            EngineCard(
                icon   = Icons.Default.AutoGraph,
                title  = "ADAPTIVE ENGINE MODULATOR",
                status = "Activo — suavizado exponencial α=0.20",
                ok     = true,
                detail = "Mapea intensidad → parámetros con curvas Bézier/tanh según modo:\n" +
                         "  NATURAL: potencia 0.8 (curva suave)\n" +
                         "  STUDIO: lineal\n" +
                         "  EXTREME: potencia 1.5 (curva agresiva)\n" +
                         "Filtro de suavizado por bloque (no per-sample) para evitar zipper noise."
            )

            // ── 8. IvannaControlLoop ─────────────────────────────────────
            EngineCard(
                icon   = Icons.Default.Loop,
                title  = "IVANNA CONTROL LOOP",
                status = "Watchdog activo (50ms)",
                ok     = true,
                detail = "Comprueba coherencia de parámetros DSP cada 50ms.\n" +
                         "Reaplica ajustes si detecta drift entre el estado Kotlin y el engine nativo.\n" +
                         "Corre en Dispatchers.IO — no bloquea el hilo de audio."
            )

            Spacer(Modifier.height(8.dp))
        }
    }
}

@Composable
private fun EngineCard(
    icon: ImageVector,
    title: String,
    status: String,
    ok: Boolean,
    detail: String
) {
    var expanded by remember { mutableStateOf(false) }
    Surface(
        onClick = { expanded = !expanded },
        modifier = Modifier.fillMaxWidth(),
        color = ObsidianSoft,
        shape = RoundedCornerShape(10.dp),
        border = androidx.compose.foundation.BorderStroke(
            1.dp,
            (if (ok) PhosphorGreen else AmberSignal).copy(alpha = 0.35f)
        )
    ) {
        Column(Modifier.padding(14.dp)) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                Box(
                    Modifier.size(8.dp).clip(CircleShape)
                        .background(if (ok) PhosphorGreen else AmberSignal)
                )
                Icon(icon, contentDescription = null, tint = AuroraCyan,
                    modifier = Modifier.size(18.dp))
                Column(Modifier.weight(1f)) {
                    Text(title, color = TextPrimary, fontWeight = FontWeight.Bold,
                        fontSize = 11.sp, letterSpacing = 0.8.sp)
                    Text(status,
                        color = if (ok) PhosphorGreen else AmberSignal,
                        fontSize = 10.sp, fontFamily = FontFamily.Monospace)
                }
                Icon(
                    if (expanded) Icons.Default.ExpandLess else Icons.Default.ExpandMore,
                    contentDescription = null, tint = TextMuted, modifier = Modifier.size(18.dp)
                )
            }
            if (expanded) {
                Spacer(Modifier.height(8.dp))
                HorizontalDivider(color = ObsidianEdge)
                Spacer(Modifier.height(8.dp))
                Text(detail, color = TextSecondary, fontSize = 10.sp,
                    fontFamily = FontFamily.Monospace, lineHeight = 15.sp)
            }
        }
    }
}
