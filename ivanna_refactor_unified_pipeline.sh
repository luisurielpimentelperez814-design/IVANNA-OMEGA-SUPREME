#!/usr/bin/env bash
# ivanna_refactor_unified_pipeline.sh
# Aplica todos los parches del diagnóstico de fragmentación de vías + telemetría 0%.
# Ejecutar desde cualquier subdirectorio del repo.
set -euo pipefail
ROOT=$(git rev-parse --show-toplevel)
cd "$ROOT"

JNI="app/src/main/cpp/jni/ivanna_omega_jni.cpp"
CTRL="app/src/main/cpp/audio_control_plane.cpp"
PIPELINE="app/src/main/java/com/ivanna/omega/audio/AudioPipeline.kt"
BACKEND="app/src/main/java/com/ivanna/omega/audio/AdaptiveBackend.kt"
APP="app/src/main/java/com/ivanna/omega/core/IVANNAApplication.kt"
NLIB="app/src/main/java/com/ivanna/omega/core/IvannaNativeLib.kt"
UNIFIED="app/src/main/java/com/ivanna/omega/audio/IvannaUnifiedPipeline.kt"

# ─── Python patcher ──────────────────────────────────────────────────────────
python3 - "$ROOT" <<'PYEOF'
import pathlib, sys, textwrap

ROOT = pathlib.Path(sys.argv[1])

def patch(path, old, new, tag):
    p = ROOT / path
    t = p.read_text(encoding='utf-8')
    cnt = t.count(old)
    assert cnt == 1, f"[{tag}] expected 1 occurrence, found {cnt}\nAnchor: {old[:80]!r}"
    p.write_text(t.replace(old, new, 1), encoding='utf-8')
    print(f"[{tag}] OK")

# ════════════════════════════════════════════════════════════════════════════════
# PATCH 1 — ivanna_omega_jni.cpp: g_activeRoute atomic declaration
# ════════════════════════════════════════════════════════════════════════════════
patch(
    "app/src/main/cpp/jni/ivanna_omega_jni.cpp",
    "static std::atomic<uint64_t> g_lastAdaptiveApplied{0};",
    ("static std::atomic<uint64_t> g_lastAdaptiveApplied{0};\n"
     "// 0=NONE 1=RouteA_BridgePlayer 2=RouteB_OmegaEffect\n"
     "static std::atomic<int> g_activeRoute{0};"),
    "P1-g_activeRoute-decl"
)

# ════════════════════════════════════════════════════════════════════════════════
# PATCH 2 — ivanna_omega_jni.cpp: adaptiveSnapshotLoop escribe TODOS los
# g_lastAdaptive* (causa raíz de telemetría 0% en Ruta B)
# ════════════════════════════════════════════════════════════════════════════════
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
    "P2-snapshot-loop-fix"
)

# ════════════════════════════════════════════════════════════════════════════════
# PATCH 3 — ivanna_omega_jni.cpp: marcar Ruta B activa en audioRouteBridgeLoop
# ════════════════════════════════════════════════════════════════════════════════
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
    "P3-route-b-active"
)

# ════════════════════════════════════════════════════════════════════════════════
# PATCH 4 — ivanna_omega_jni.cpp: marcar Ruta A activa en nativeProcess
# ════════════════════════════════════════════════════════════════════════════════
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
    "P4-route-a-active"
)

# ════════════════════════════════════════════════════════════════════════════════
# PATCH 5 — ivanna_omega_jni.cpp: nativeGetUnifiedPipelineStatus JNI
# ════════════════════════════════════════════════════════════════════════════════
patch(
    "app/src/main/cpp/jni/ivanna_omega_jni.cpp",
    "    env->SetFloatArrayRegion(arr, 0, 3, v);\n    return arr;\n}\n\n} // extern \"C\"",
    (
        "    env->SetFloatArrayRegion(arr, 0, 3, v);\n"
        "    return arr;\n"
        "}\n"
        "\n"
        "// ── nativeGetUnifiedPipelineStatus — estado consolidado de ambas rutas ──────\n"
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
    "P5-unified-status-jni"
)

# ════════════════════════════════════════════════════════════════════════════════
# PATCH 6 — audio_control_plane.cpp: PhaseOracle coherence=0.5 en silencio
# ════════════════════════════════════════════════════════════════════════════════
patch(
    "app/src/main/cpp/audio_control_plane.cpp",
    "    const float coherence = std::clamp(1.f / (1.f + g_phase_oracle_refined.P0 * 4.f), 0.f, 1.f);",
    (
        "    // FIX (PhaseOracle inflado en silencio): phase_vel==0 converge P0->0\n"
        "    // via Kalman -> coherence=1.0 en silencio absoluto. Neutral=0.5.\n"
        "    const float coherence = (phase_vel == 0.0f) ? 0.5f :\n"
        "        std::clamp(1.f / (1.f + g_phase_oracle_refined.P0 * 4.f), 0.f, 1.f);"
    ),
    "P6-phase-oracle-coherence"
)

# ════════════════════════════════════════════════════════════════════════════════
# PATCH 7 — IvannaNativeLib.kt: declarar nativeGetUnifiedPipelineStatus
# ════════════════════════════════════════════════════════════════════════════════
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
    "P7-nativelib-decl"
)

# ════════════════════════════════════════════════════════════════════════════════
# PATCH 8 — AudioPipeline.kt: notificar Ruta A start/stop + log 96kHz
# ════════════════════════════════════════════════════════════════════════════════
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
    "P8a-pipeline-start"
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
    "P8b-pipeline-stop"
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
    "P8c-pipeline-96khz-log"
)

# ════════════════════════════════════════════════════════════════════════════════
# PATCH 9 — AdaptiveBackend.kt: fallback a IvannaUnifiedPipeline en Ruta B
# ════════════════════════════════════════════════════════════════════════════════
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
    "P9-backend-unified-fallback"
)

# ════════════════════════════════════════════════════════════════════════════════
# PATCH 10 — IVANNAApplication.kt: arrancar IvannaUnifiedPipeline en onCreate
# ════════════════════════════════════════════════════════════════════════════════
patch(
    "app/src/main/java/com/ivanna/omega/core/IVANNAApplication.kt",
    "        com.ivanna.omega.audio.AudioRouteManager.start(this)",
    (
        "        com.ivanna.omega.audio.AudioRouteManager.start(this)\n"
        "        com.ivanna.omega.audio.IvannaUnifiedPipeline.start(this)"
    ),
    "P10-app-unified-start"
)

print("\nAll patches applied successfully.")
PYEOF

# ─── Crear IvannaUnifiedPipeline.kt ─────────────────────────────────────────
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
    /**
     * Layout compatible con nativeGetAdaptiveTelemetry[0..9] para que
     * AdaptiveBackend pueda usarlo como fuente de verdad cuando Ruta B
     * está activa y Ruta A no está corriendo.
     */
    fun toAdaptiveTelemetryArray(): FloatArray = floatArrayOf(
        rms,          // [0] rms
        peak,         // [1] peak
        0f,           // [2] gr_db (no disponible desde shared mem)
        1f,           // [3] target_gain (neutro)
        compAmount,   // [4] comp_amount
        excReduction, // [5] exc_reduction
        spatialWidth, // [6] spatial_width
        1f,           // [7] safety_margin (neutro)
        voiceProtect, // [8] voice_protect
        0f            // [9] adaptive_applied_count
    )
}

/**
 * IvannaUnifiedPipeline — orquestador singleton.
 *
 * Sondea nativeGetUnifiedPipelineStatus() a 20Hz, detecta qué ruta
 * está produciendo audio (A = IvannaBridgePlayer, B = omega_effect /
 * Spotify/YouTube, NONE = silencio), y expone StateFlow<PipelineState>
 * para AdaptiveBackend y cualquier componente de UI.
 *
 * Arrancar: IVANNAApplication.onCreate() llama start(this) síncrono
 * en el hilo principal, justo después de AudioRouteManager.start(this).
 *
 * AudioPipeline llama notifyRouteAStarted/Stopped() para sincronizar
 * el estado local sin depender del poll de 50ms.
 */
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

    /** Array compatible con nativeGetAdaptiveTelemetry para AdaptiveBackend. */
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

# ─── git ─────────────────────────────────────────────────────────────────────
git add \
  "$JNI" \
  "$CTRL" \
  "$PIPELINE" \
  "$BACKEND" \
  "$APP" \
  "$NLIB" \
  "$UNIFIED"

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
- IVANNAApplication: IvannaUnifiedPipeline.start() after AudioRouteManager
- PhaseOracle: coherence=0.5 (neutral) when phase_vel==0 to prevent
  spurious coherence=1.0 during absolute silence"

git push origin main
