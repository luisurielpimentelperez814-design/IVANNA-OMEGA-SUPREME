#!/usr/bin/env python3
# IVANNA OMEGA SUPREME — pulido + conexión real (BridgePlayer / captura)
# Ejecutar desde la raíz del repo:  python3 patch_ivanna_bridge.py
import io, sys

MAIN = "app/src/main/java/com/ivanna/omega/MainActivity.kt"
SVC  = "app/src/main/java/com/ivanna/omega/audio/PlaybackCaptureService.kt"

def rd(p):
    return io.open(p, encoding="utf-8").read()
def wr(p, s):
    io.open(p, "w", encoding="utf-8").write(s)
def rep(s, old, new, tag):
    assert s.count(old) == 1, f"[{tag}] count={s.count(old)} (esperado 1)"
    return s.replace(old, new)

# ─────────────────────────────────────────────────────────────
# PARCHE A — PlaybackCaptureService: estado real observable
# ─────────────────────────────────────────────────────────────
s = rd(SVC)
s = rep(s,
"import kotlinx.coroutines.*",
"import kotlinx.coroutines.*\n"
"import kotlinx.coroutines.flow.MutableStateFlow\n"
"import kotlinx.coroutines.flow.StateFlow\n"
"import kotlinx.coroutines.flow.asStateFlow",
"A1 imports flow")

s = rep(s,
'        private const val TAG = "PlaybackCaptureService"',
'        private const val TAG = "PlaybackCaptureService"\n'
'        // FIX (conexión real): el dashboard mostraba STANDBY/ACTIVO segun un\n'
'        // boolean local de Compose que nunca sabia si el servicio seguia vivo.\n'
'        // Esta es la unica fuente de verdad: la setea startCapture/stopCapture.\n'
'        private val _isCapturing = MutableStateFlow(false)\n'
'        val isCapturing: StateFlow<Boolean> = _isCapturing.asStateFlow()',
"A2 stateflow")

s = rep(s,
"            audioRecord?.startRecording()\n            isRunning = true",
"            audioRecord?.startRecording()\n            isRunning = true\n            _isCapturing.value = true",
"A3 set true")

s = rep(s,
"        if (!isRunning && audioRecord == null && mediaProjection == null) return // idempotente\n        isRunning = false",
"        if (!isRunning && audioRecord == null && mediaProjection == null) return // idempotente\n        isRunning = false\n        _isCapturing.value = false",
"A4 set false")
wr(SVC, s)

# ─────────────────────────────────────────────────────────────
# PARCHE B — OmegaApp: captureActive derivado del servicio real
# ─────────────────────────────────────────────────────────────
m = rd(MAIN)
m = rep(m,
"        var captureActive by remember { mutableStateOf(false) }",
"        // captureRequested: evita relanzar el dialogo en cada recomposicion.\n"
"        // captureActive: estado REAL del servicio (StateFlow) — si el usuario\n"
"        // mata la captura desde la notificacion, el banner STANDBY vuelve solo.\n"
"        var captureRequested by remember { mutableStateOf(false) }\n"
"        val captureActive by PlaybackCaptureService.isCapturing.collectAsState()",
"B1 state")

m = rep(m,
"                context.startForegroundService(intent)\n                captureActive = true",
"                context.startForegroundService(intent)\n                captureRequested = true",
"B2 callback")

m = rep(m,
"                LaunchedEffect(Unit) {\n                    if (!captureActive) {",
"                LaunchedEffect(Unit) {\n                    if (!captureActive && !captureRequested) {",
"B3 autolaunch")
wr(MAIN, m)

# ─────────────────────────────────────────────────────────────
# PARCHE C — DashboardScreen: quitar AdaptiveBackend duplicado
# ─────────────────────────────────────────────────────────────
m = rd(MAIN)
m = rep(m,
"    val context = LocalContext.current\n    val adaptiveBackend = remember { AdaptiveBackend(context) }\n",
"    val context = LocalContext.current\n"
"    // FIX: aqui se instanciaba un SEGUNDO AdaptiveBackend que sombreaba el\n"
"    // parametro — dos motores de telemetria compitiendo y el del NavHost\n"
"    // sin consumidor. Se usa el que ya llega por parametro.\n",
"C1 dedupe backend")
wr(MAIN, m)

# ─────────────────────────────────────────────────────────────
# PARCHE D — BridgePlayerCard: cablear posicion/duracion/seek
# ─────────────────────────────────────────────────────────────
m = rd(MAIN)
m = rep(m,
"            onPickFile  = { singlePicker.launch(\"audio/*\") },",
"            onPickFile  = { singlePicker.launch(\"audio/*\") },\n"
"            // FIX: la card ya tenia barra de progreso + seek, pero MainActivity\n"
"            // recogia los StateFlow y no se los pasaba — barra invisible.\n"
"            currentPositionMs = playerPositionMs,\n"
"            durationMs        = playerDurationMs,\n"
"            onSeek            = { player.seekTo(it) },",
"D1 seek wiring")
wr(MAIN, m)

# ─────────────────────────────────────────────────────────────
# PARCHE E — tira de metricas OMEGA bajo el player (omegaMetrics vivo)
# ─────────────────────────────────────────────────────────────
m = rd(MAIN)
m = rep(m,
"""                    queueIdx = prevIdx; currentUri = queue[prevIdx]; player.play(queue[prevIdx])
                }
            }
        )
""",
"""                    queueIdx = prevIdx; currentUri = queue[prevIdx]; player.play(queue[prevIdx])
                }
            }
        )

        // ── Tira OMEGA — omegaMetrics ya se recogia y no se pintaba ──────────
        Row(
            Modifier.fillMaxWidth().horizontalScroll(rememberScrollState())
                .padding(horizontal = 16.dp, vertical = 2.dp),
            horizontalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            listOf(
                "RMS"  to String.format("%.2f", omegaMetrics.rmsLevel),
                "PEAK" to String.format("%.2f", omegaMetrics.peakLevel),
                "CLIP" to omegaMetrics.clipCount.toString(),
                "CPU"  to String.format("%.0f%%", omegaMetrics.cpuPercent),
                "LAT"  to String.format("%.1fms", omegaMetrics.latencyMs),
                "SR"   to "${omegaMetrics.sampleRate / 1000}k",
                "DSP"  to if (omegaMetrics.dspActive) "ON" else "OFF",
                "HRTF" to if (omegaMetrics.hrtfActive) "ON" else "OFF",
                "AI"   to omegaMetrics.yamnetCategory
            ).forEach { (k, v) ->
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(k, color = TextSec, fontSize = 8.sp, letterSpacing = 0.8.sp)
                    Spacer(Modifier.width(3.dp))
                    Text(v, color = CyanGlow, fontSize = 9.sp, fontWeight = FontWeight.Bold)
                }
            }
        }
""",
"E1 metrics strip")
wr(MAIN, m)

# ─────────────────────────────────────────────────────────────
# PARCHE F — visualizer: no navegar a una pantalla en negro
# ─────────────────────────────────────────────────────────────
m = rd(MAIN)
m = rep(m,
'            onOpenVisualizer = { nav.navigate("visualizer") },',
'            // FIX: la ruta "visualizer" solo lanzaba la MediaProjection y\n'
'            // dejaba una pantalla vacia. Ahora reusa el mismo callback del\n'
'            // banner: pide captura sin sacar al usuario del dashboard.\n'
'            onOpenVisualizer = onStartCapture,',
"F1 visualizer")
wr(MAIN, m)

print("OK — todos los parches aplicados (assert count==1 en cada uno)")
