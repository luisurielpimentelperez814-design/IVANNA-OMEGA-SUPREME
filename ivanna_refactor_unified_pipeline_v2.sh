#!/usr/bin/env bash
# ivanna_refactor_unified_pipeline_v2.sh
# Version idempotente: no aborta si un patch ya fue aplicado antes,
# y no aborta si un ancla no calza -- reporta y sigue con los demas.
# Ejecutar desde cualquier subdirectorio del repo.
set -uo pipefail
ROOT=$(git rev-parse --show-toplevel)
cd "$ROOT"

JNI="app/src/main/cpp/jni/ivanna_omega_jni.cpp"
CTRL="app/src/main/cpp/audio_control_plane.cpp"
PIPELINE="app/src/main/java/com/ivanna/omega/audio/AudioPipeline.kt"
BACKEND="app/src/main/java/com/ivanna/omega/audio/AdaptiveBackend.kt"
APP="app/src/main/java/com/ivanna/omega/core/IVANNAApplication.kt"
NLIB="app/src/main/java/com/ivanna/omega/core/IvannaNativeLib.kt"
UNIFIED="app/src/main/java/com/ivanna/omega/audio/IvannaUnifiedPipeline.kt"

# ─── Python patcher (idempotente) ────────────────────────────────────────────
python3 - "$ROOT" <<'PYEOF'
import pathlib, sys

ROOT = pathlib.Path(sys.argv[1])
results = []  # (tag, status, detail)

def patch(path, old, new, tag, marker=None):
    """
    marker: substring que, si ya esta presente en el archivo, significa
    que este patch ya fue aplicado (por defecto se usa `new`).
    No lanza excepcion nunca -- registra el resultado y sigue.
    """
    p = ROOT / path
    if not p.exists():
        results.append((tag, "MISSING_FILE", str(path)))
        return
    t = p.read_text(encoding='utf-8')
    check = marker if marker is not None else new
    if check in t:
        results.append((tag, "SKIP", "ya aplicado"))
        return
    cnt = t.count(old)
    if cnt == 1:
        p.write_text(t.replace(old, new, 1), encoding='utf-8')
        results.append((tag, "OK", "aplicado ahora"))
    elif cnt == 0:
        results.append((tag, "FAIL", "ancla no encontrada -- revisar manualmente"))
    else:
        results.append((tag, "FAIL", f"ancla ambigua ({cnt} ocurrencias) -- revisar manualmente"))

# ════════════════════════════════════════════════════════════════════════════
# PATCH 1 — g_activeRoute atomic declaration
# ════════════════════════════════════════════════════════════════════════════
patch(
    "app/src/main/cpp/jni/ivanna_omega_jni.cpp",
    "static std::atomic<uint64_t> g_lastAdaptiveApplied{0};",
    ("static std::atomic<uint64_t> g_lastAdaptiveApplied{0};\n"
     "// 0=NONE 1=RouteA_BridgePlayer 2=RouteB_OmegaEffect\n"
     "static std::atomic<int> g_activeRoute{0};"),
    "P1-g_activeRoute-decl",
    marker="static std::atomic<int> g_activeRoute{0};"
)

# ════════════════════════════════════════════════════════════════════════════
# PATCH 2 — adaptiveSnapshotLoop escribe TODOS los g_lastAdaptive*
# ════════════════════════════════════════════════════════════════════════════
patch(
    "app/src/main/cpp/jni/ivanna_omega_jni.cpp",
    (
        "            g_adaptiveSafetySnapshot.store(\n"
        "                st.safety_margin, std::memory_order_release);\n"
        "        }\n"
        "\n"
        "        std::this_thread::sleep_for(\n"
        "            std::chrono::milliseconds(20));\n"
        "    }\n"
        "}"
    ),
    (
        "            g_adaptiveSafetySnapshot.store(\n"
        "                st.safety_margin, std::memory_order_release);\n"
        "\n"
        "            // FIX (telemetria 0% Ruta B): nativeProcess no corre cuando\n"
        "            // Spotify/YouTube estan activos. Este loop es la unica fuente\n"
        "            // de AdaptiveState independiente de la ruta activa.\n"
        "            g_lastAdaptiveTargetGain .store(st.target_gain,              std::memory_order_release);\n"
        "            g_lastAdaptiveCompAmount .store(st.compressor_amount,        std::memory_order_release);\n"
        "            g_lastAdaptiveExcReduction.store(st.exciter_reduction,       std::memory_order_release);\n"
        "            g_lastAdaptiveSpatialWidth.store(st.spatial_width,           std::memory_order_release);\n"
        "            g_lastAdaptiveSafetyMargin.store(st.safety_margin,           std::memory_order_release);\n"
        "            g_lastAdaptiveVoiceProtect.store(st.voice_protection_amount, std::memory_order_release);\n"
        "        }\n"
        "\n"
        "        std::this_thread::sleep_for(\n"
        "            std::chrono::milliseconds(20));\n"
        "    }\n"
        "}"
    ),
    "P2-snapshot-loop-fix",
    marker="FIX (telemetria 0% Ruta B): nativeProcess no corre cuando"
)

# ════════════════════════════════════════════════════════════════════════════
# PATCH 3 — marcar Ruta B activa en audioRouteBridgeLoop
# ════════════════════════════════════════════════════════════════════════════
patch(
    "app/src/main/cpp/jni/ivanna_omega_jni.cpp",
    (
        "        g_lastRawRms.store(rms,   std::memory_order_relaxed);\n"
        "        g_lastRawPeak.store(peak, std::memory_order_relaxed);\n"
        "        g_lastRawGrDb.store(grDb, std::memory_order_relaxed);\n"
        "\n"
        "        // FIX (Opci"
    ),
    (
        "        g_lastRawRms.store(rms,   std::memory_order_relaxed);\n"
        "        g_lastRawPeak.store(peak, std::memory_order_relaxed);\n"
        "        g_lastRawGrDb.store(grDb, std::memory_order_relaxed);\n"
        "        g_activeRoute.store(2, std::memory_order_relaxed);\n"
        "\n"
        "        // FIX (Opci"
    ),
    "P3-route-b-active",
    marker="g_activeRoute.store(2, std::memory_order_relaxed);"
)

# ════════════════════════════════════════════════════════════════════════════
# PATCH 4 — marcar Ruta A activa en nativeProcess
# ════════════════════════════════════════════════════════════════════════════
patch(
    "app/src/main/cpp/jni/ivanna_omega_jni.cpp",
    (
        "        // Snapshot para telemetr\u00eda (getters JNI, fuera del audio thread).\n"
        "        g_lastRawRms.store(rms,     std::memory_order_relaxed);\n"
        "        g_lastRawPeak.store(peakAbs, std::memory_order_relaxed);\n"
        "        g_lastRawGrDb.store(grDb,    std::memory_order_relaxed);\n"
        "\n"
        "        // 5) Consumir"
    ),
    (
        "        // Snapshot para telemetr\u00eda (getters JNI, fuera del audio thread).\n"
        "        g_lastRawRms.store(rms,     std::memory_order_relaxed);\n"
        "        g_lastRawPeak.store(peakAbs, std::memory_order_relaxed);\n"
        "        g_lastRawGrDb.store(grDb,    std::memory_order_relaxed);\n"
        "        g_activeRoute.store(1, std::memory_order_relaxed);\n"
        "\n"
        "        // 5) Consumir"
    ),
    "P4-route-a-active",
    marker="g_activeRoute.store(1, std::memory_order_relaxed);"
)

# ════════════════════════════════════════════════════════════════════════════
# PATCH 5 — nativeGetUnifiedPipelineStatus JNI
# ════════════════════════════════════════════════════════════════════════════
patch(
    "app/src/main/cpp/jni/ivanna_omega_jni.cpp",
    "    env->SetFloatArrayRegion(arr, 0, 3, v);\n    return arr;\n}\n\n} // extern \"C\"",
    (
        "    env->SetFloatArrayRegion(arr, 0, 3, v);\n"
        "    return arr;\n"
        "}\n"
        "\n"
        "// \u2500\u2500 nativeGetUnifiedPipelineStatus \u2014 estado consolidado de ambas rutas \u2500\u2500\u2500\u2500\u2500\u2500\n"
        "// FloatArray[8]:\n"
        "//   [0] activeRoute       (0=NONE 1=RouteA_BridgePlayer 2=RouteB_OmegaEffect)\n"
        "//   [1] rms\n"
        "//   [2] peak\n"
        "//   [3] voiceProtect      (0..1)\n"
        "//   [4] compAmount        (0..1)\n"
        "//   [5] excReduction      (0..1)\n"
        "//   [6] spatialWidth      (0..1.5)\n"
        "//   [7] adaptiveActive    (1.0 si ADE running y applied>0)\n"
        "JNIEXPORT jfloatArray JNICALL\n"
        "Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetUnifiedPipelineStatus(\n"
        "    JNIEnv* env, jobject) {\n"
        "    jfloatArray arr = env->NewFloatArray(8);\n"
        "    if (!arr) return nullptr;\n"
        "    const float v[8] = {\n"
        "        (float)g_activeRoute.load(std::memory_order_relaxed),\n"
        "        g_lastRawRms.load(std::memory_order_relaxed),\n"
        "        g_lastRawPeak.load(std::memory_order_relaxed),\n"
        "        g_lastAdaptiveVoiceProtect.load(std::memory_order_relaxed),\n"
        "        g_lastAdaptiveCompAmount.load(std::memory_order_relaxed),\n"
        "        g_lastAdaptiveExcReduction.load(std::memory_order_relaxed),\n"
        "        g_lastAdaptiveSpatialWidth.load(std::memory_order_relaxed),\n"
        "        (g_adaptiveEngineStarted.load(std::memory_order_acquire) &&\n"
        "         g_lastAdaptiveApplied.load(std::memory_order_relaxed) > 0) ? 1.0f : 0.0f\n"
        "    };\n"
        "    env->SetFloatArrayRegion(arr, 0, 8, v);\n"
        "    return arr;\n"
        "}\n"
        "\n"
        "} // extern \"C\""
    ),
    "P5-unified-status-jni",
    marker="Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetUnifiedPipelineStatus"
)

# ════════════════════════════════════════════════════════════════════════════
# PATCH 6 — audio_control_plane.cpp: PhaseOracle coherence=0.5 en silencio
# ════════════════════════════════════════════════════════════════════════════
patch(
    "app/src/main/cpp/audio_control_plane.cpp",
    "    const float coherence = std::clamp(1.f / (1.f + g_phase_oracle_refined.P0 * 4.f), 0.f, 1.f);",
    (
        "    // FIX (PhaseOracle inflado en silencio): phase_vel==0 converge P0->0\n"
        "    // via Kalman -> coherence=1.0 en silencio absoluto. Neutral=0.5.\n"
        "    const float coherence = (phase_vel == 0.0f) ? 0.5f :\n"
        "        std::clamp(1.f / (1.f + g_phase_oracle_refined.P0 * 4.f), 0.f, 1.f);"
    ),
    "P6-phase-oracle-coherence",
    marker="FIX (PhaseOracle inflado en silencio)"
)

# ════════════════════════════════════════════════════════════════════════════
# PATCH 7 — IvannaNativeLib.kt: declarar nativeGetUnifiedPipelineStatus
# ════════════════════════════════════════════════════════════════════════════
patch(
    "app/src/main/java/com/ivanna/omega/core/IvannaNativeLib.kt",
    "    external fun nativeGetBandEnergies(): FloatArray?",
    (
        "    external fun nativeGetBandEnergies(): FloatArray?\n"
        "\n"
        "    // FloatArray[8]: [activeRoute(0/1/2), rms, peak, voiceProtect,\n"
        "    //                  compAmount, excReduction, spatialWidth, adaptiveActive(0/1)]\n"
        "    external fun nativeGetUnifiedPipelineStatus(): FloatArray?"
    ),
    "P7-nativelib-decl",
    marker="external fun nativeGetUnifiedPipelineStatus(): FloatArray?"
)

# ════════════════════════════════════════════════════════════════════════════
# PATCH 8 — AudioPipeline.kt: notificar Ruta A start/stop + log 96kHz
# ════════════════════════════════════════════════════════════════════════════
patch(
    "app/src/main/java/com/ivanna/omega/audio/AudioPipeline.kt",
    (
        "    fun start() {\n"
        "        if (isRunning) return\n"
        "        isRunning = true\n"
        "        DSPBridge.init(SAMPLE_RATE)\n"
        "        dspState.pushToNative()"
    ),
    (
        "    fun start() {\n"
        "        if (isRunning) return\n"
        "        isRunning = true\n"
        "        DSPBridge.init(SAMPLE_RATE)\n"
        "        IvannaUnifiedPipeline.notifyRouteAStarted()\n"
        "        dspState.pushToNative()"
    ),
    "P8a-pipeline-start",
    marker="IvannaUnifiedPipeline.notifyRouteAStarted()"
)

patch(
    "app/src/main/java/com/ivanna/omega/audio/AudioPipeline.kt",
    (
        "    fun stop() {\n"
        "        isRunning = false\n"
        "        job?.cancel()"
    ),
    (
        "    fun stop() {\n"
        "        isRunning = false\n"
        "        IvannaUnifiedPipeline.notifyRouteAStopped()\n"
        "        job?.cancel()"
    ),
    "P8b-pipeline-stop",
    marker="IvannaUnifiedPipeline.notifyRouteAStopped()"
)

patch(
    "app/src/main/java/com/ivanna/omega/audio/AudioPipeline.kt",
    (
        '        if (minIn <= 0 || minOut <= 0) {\n'
        '            Log.e(tag, "Hardware no soporta ${SAMPLE_RATE}Hz"); isRunning = false; return\n'
        '        }'
    ),
    (
        '        if (minIn <= 0 || minOut <= 0) {\n'
        '            Log.e(tag, "Hardware rechaz\u00f3 ${SAMPLE_RATE}Hz \u2014 Ruta A no disponible")\n'
        '            IvannaUnifiedPipeline.notifyRouteAStopped()\n'
        '            isRunning = false; return\n'
        '        }'
    ),
    "P8c-pipeline-96khz-log",
    marker="Hardware rechaz\u00f3 ${SAMPLE_RATE}Hz \u2014 Ruta A no disponible"
)

# ════════════════════════════════════════════════════════════════════════════
# PATCH 9 — AdaptiveBackend.kt: fallback a IvannaUnifiedPipeline en Ruta B
# ════════════════════════════════════════════════════════════════════════════
patch(
    "app/src/main/java/com/ivanna/omega/audio/AdaptiveBackend.kt",
    (
        "    private fun pollTelemetry() {\n"
        "        try {\n"
        "            val raw = IvannaNativeLib.nativeGetAdaptiveTelemetry() ?: return\n"
        "            if (raw.size < 10) return\n"
        "            _telemetry.value = AdaptiveTelemetry(\n"
        "                rms          = raw[0],\n"
        "                peakDb       = raw[1],\n"
        "                grDb         = raw[2],\n"
        "                targetGain   = raw[3],\n"
        "                compAmount   = raw[4],\n"
        "                excReduction = raw[5],\n"
        "                spatialWidth = raw[6],\n"
        "                safetyMargin = raw[7],\n"
        "                voiceProtect = raw[8],\n"
        "                motorRunning = IvannaNativeLib.nativeIsAdaptiveEngineRunning()\n"
        "            )\n"
        "        } catch (e: Throwable) {\n"
        "            // Motor no inicializado todav\u00eda \u2014 no es error\n"
        "        }\n"
        "    }"
    ),
    (
        "    private fun pollTelemetry() {\n"
        "        try {\n"
        "            val raw = IvannaNativeLib.nativeGetAdaptiveTelemetry() ?: return\n"
        "            if (raw.size < 10) return\n"
        "            val motorActive = IvannaNativeLib.nativeIsAdaptiveEngineRunning()\n"
        "            // FIX (telemetria 0% Ruta B): compAmount[4] y voiceProtect[8] en 0\n"
        "            // con motor activo = Ruta B sin Ruta A. Usar IvannaUnifiedPipeline.\n"
        "            val src = if (motorActive && raw[4] == 0f && raw[8] == 0f)\n"
        "                IvannaUnifiedPipeline.toAdaptiveTelemetryArray() else raw\n"
        "            _telemetry.value = AdaptiveTelemetry(\n"
        "                rms          = src[0],\n"
        "                peakDb       = src[1],\n"
        "                grDb         = src[2],\n"
        "                targetGain   = src[3],\n"
        "                compAmount   = src[4],\n"
        "                excReduction = src[5],\n"
        "                spatialWidth = src[6],\n"
        "                safetyMargin = src[7],\n"
        "                voiceProtect = src[8],\n"
        "                motorRunning = motorActive\n"
        "            )\n"
        "        } catch (e: Throwable) {\n"
        "            // Motor no inicializado todav\u00eda \u2014 no es error\n"
        "        }\n"
        "    }"
    ),
    "P9-backend-unified-fallback",
    marker="IvannaUnifiedPipeline.toAdaptiveTelemetryArray()"
)

# ════════════════════════════════════════════════════════════════════════════
# PATCH 10 — IVANNAApplication.kt: arrancar IvannaUnifiedPipeline en onCreate
# ════════════════════════════════════════════════════════════════════════════
patch(
    "app/src/main/java/com/ivanna/omega/core/IVANNAApplication.kt",
    "        com.ivanna.omega.audio.AudioRouteManager.start(this)",
    (
        "        com.ivanna.omega.audio.AudioRouteManager.start(this)\n"
        "        com.ivanna.omega.audio.IvannaUnifiedPipeline.start(this)"
    ),
    "P10-app-unified-start",
    marker="com.ivanna.omega.audio.IvannaUnifiedPipeline.start(this)"
)

# ════════════════════════════════════════════════════════════════════════════
# Resumen final
# ════════════════════════════════════════════════════════════════════════════
print("\n=== RESUMEN ===")
ok = skip = fail = missing = 0
for tag, status, detail in results:
    print(f"[{status:12}] {tag:28} {detail}")
    if status == "OK": ok += 1
    elif status == "SKIP": skip += 1
    elif status == "MISSING_FILE": missing += 1
    else: fail += 1

print(f"\nOK={ok}  SKIP(ya aplicados)={skip}  FAIL(revisar a mano)={fail}  MISSING_FILE={missing}")
if fail > 0 or missing > 0:
    print("\n>>> Hay patches que necesitan revision manual. No se abortó nada mas.")
PYEOF

# ─── Crear IvannaUnifiedPipeline.kt (solo si no existe) ──────────────────────
if [ -f "$UNIFIED" ]; then
    echo "[SKIP] $UNIFIED ya existe -- no se sobreescribe"
else
cat > "$UNIFIED" << 'KTEOF'
package com.ivanna.omega.audio

import android.content.Context
import android.util.Log
import com.ivanna.omega.core.IvannaNativeLib
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

/** Ruta de audio activa detectada por nativeGetUnifiedPipelineStatus. */
enum class ActiveRoute { NONE, ROUTE_A, ROUTE_B }

data class PipelineState(
    val activeRoute: ActiveRoute = ActiveRoute.NONE,
    val rms: Float = 0f,
    val peak: Float = 0f,
    val voiceProtect: Float = 0f,
    val compAmount: Float = 0f,
    val excReduction: Float = 0f,
    val spatialWidth: Float = 1f,
    val adaptiveActive: Boolean = false
) {
    fun toAdaptiveTelemetryArray(): FloatArray = floatArrayOf(
        rms, peak, 0f, 1f, compAmount, excReduction, spatialWidth, 1f, voiceProtect, 0f
    )
}

object IvannaUnifiedPipeline {

    private const val TAG = "IvannaUnifiedPipeline"
    private const val POLL_MS = 50L   // 20 Hz

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private var job: Job? = null

    private val _state = MutableStateFlow(PipelineState())
    val state: StateFlow<PipelineState> = _state

    @Volatile private var routeARunning = false

    fun start(context: Context) {
        if (job?.isActive == true) return
        job = scope.launch {
            Log.i(TAG, "Unified pipeline monitor @20Hz")
            while (isActive) {
                poll()
                delay(POLL_MS)
            }
        }
    }

    fun stop() {
        job?.cancel()
        job = null
    }

    fun notifyRouteAStarted() {
        routeARunning = true
        Log.i(TAG, "Route A started (IvannaBridgePlayer active)")
    }

    fun notifyRouteAStopped() {
        routeARunning = false
        Log.i(TAG, "Route A stopped")
    }

    fun toAdaptiveTelemetryArray(): FloatArray = _state.value.toAdaptiveTelemetryArray()

    private fun poll() {
        try {
            val raw = IvannaNativeLib.nativeGetUnifiedPipelineStatus() ?: return
            if (raw.size < 8) return
            val route = when (raw[0].toInt()) {
                1    -> ActiveRoute.ROUTE_A
                2    -> ActiveRoute.ROUTE_B
                else -> ActiveRoute.NONE
            }
            _state.value = PipelineState(
                activeRoute   = route,
                rms           = raw[1],
                peak          = raw[2],
                voiceProtect  = raw[3],
                compAmount    = raw[4],
                excReduction  = raw[5],
                spatialWidth  = raw[6],
                adaptiveActive = raw[7] > 0.5f
            )
        } catch (_: Throwable) {
            // Motor aún no inicializado — ignorar
        }
    }
}
KTEOF
echo "[NEW] IvannaUnifiedPipeline.kt created"
fi

# ─── git ─────────────────────────────────────────────────────────────────────
git add "$JNI" "$CTRL" "$PIPELINE" "$BACKEND" "$APP" "$NLIB" "$UNIFIED" 2>/dev/null || true

if git diff --cached --quiet; then
    echo ""
    echo "No hay cambios nuevos para commitear (todo ya estaba aplicado o falló y no se tocó nada)."
else
    git commit -m "fix: unified pipeline + telemetry 0% route B

- adaptiveSnapshotLoop now writes all g_lastAdaptive* atomics
  (root cause: nativeProcess Route A never called when Spotify/YT active)
- g_activeRoute atomic marks active route in nativeProcess (1) and
  audioRouteBridgeLoop (2)
- nativeGetUnifiedPipelineStatus() FloatArray[8] consolidates both routes
- IvannaUnifiedPipeline.kt singleton: polls @20Hz, exposes StateFlow<PipelineState>
- AdaptiveBackend.pollTelemetry: fallback to IvannaUnifiedPipeline when
  compAmount==0 && voiceProtect==0 with motor active (Route B signal)
- AudioPipeline: notifyRouteAStarted/Stopped + 96kHz rejection log
"
    echo "Commit creado."
fi
