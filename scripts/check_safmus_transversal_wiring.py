#!/usr/bin/env python3
"""Fail CI if SAFMUS numeric gates become disconnected from UI, control, HRTF/RIR or evolution paths."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
errors = []

def source(path):
    p = ROOT / path
    if not p.is_file():
        errors.append(f"missing required file: {path}")
        return ""
    return p.read_text(encoding="utf-8")

def require(path, *needles):
    text = source(path)
    for needle in needles:
        if needle not in text:
            errors.append(f"{path}: missing SAFMUS contract token {needle!r}")

# One Compose Slider wrapper owns all direct Slider calls in the main UI package.
require("app/src/main/java/com/ivanna/omega/ui/SafmusComposeSlider.kt",
        "fun Slider(", "SafmusUniversalKernel.sanitizeControlValue", "enabled = enabled && rangeValid")
require("app/src/main/java/com/ivanna/omega/ui/oem/OemShared.kt",
        "sanitizeControlValue(value, min, max)", "sanitizeControlValue(proposed, min, max)")
ui_root = ROOT / "app/src/main/java/com/ivanna/omega/ui"
for p in ui_root.rglob("*.kt"):
    if p.name == "SafmusComposeSlider.kt":
        continue
    if re.search(r"^import androidx\.compose\.material3\.Slider(?:\s|$)", p.read_text(encoding="utf-8"), re.M):
        errors.append(f"{p.relative_to(ROOT)}: explicit Material3 Slider import bypasses the shared SAFMUS wrapper")

# Every shared numeric command must reject non-finite JSON values; high-risk bridges also clamp domain values.
bridge = "app/src/main/java/com/ivanna/omega/magisk/OmegaEngineBridge.kt"
require(bridge, "private fun hasFiniteNumericValues", "if (!hasFiniteNumericValues(payload))",
        "sanitizeControlValue(rt60S, 0.1f, 1.5f)", "val safeQ = FloatArray(7)",
        "sanitizeControlValue(compressor, -24f, 0f)", "scores.any { !it.isFinite() }",
        "private fun areFiniteNumericArgs", "sanitizeControlValue(warpedLatticeBlDrive, 0f, 1f)",
        "sanitizeControlValue(msoItdNanoseconds, -50000f, 50000f)")

# Native SAF latent boundaries must validate before mutating HRTF/RIR state.
for path, tokens in {
    "app/src/main/cpp/omega_effect.cpp": ("isFiniteLatent",),
    "app/src/main/cpp/IvannaFusionCore.cpp": ("isFiniteLatent(q)",),
    "app/src/main/cpp/saf_latent_bridge.cpp": ("isFiniteLatent(q)",),
    "app/src/main/cpp/HrtfManager.hpp": ("isFiniteLatent(q)", "size < 7"),
    "app/src/main/cpp/spatial/synthetic_hrtf.hpp": ("isFiniteLatent(q)",),
    "app/src/main/cpp/SafSpatialModifier.hpp": ("isFiniteLatent(q.data())",),
    "app/src/main/cpp/spatial/RirConvolver.cpp": ("isFiniteLatent(q)",),
    "app/src/main/cpp/spatial/ivanna_object_renderer.hpp": ("isFiniteLatent(q)",),
    "app/src/main/cpp/spatial/ivanna_object_renderer.cpp": ("isFiniteLatent(q.data())",),
    "app/src/main/cpp/spatial/hrtf_convolver.hpp": ("isFiniteLatent(q)",),
}.items():
    require(path, *tokens)

# Evolution can only change audio with finite cues, bounded gain and measured block headroom.
require("app/src/main/cpp/include/perceptual_optimizer_v2.hpp",
        "solveMapQuadratic", "projectedKktResidual", "std::exp(exponent)")
require("app/src/main/cpp/include/saf_room_optimizer.hpp",
        "perceptual_v2::solveMapQuadratic", "Fail closed", "return 1.0f")
require("app/src/main/cpp/tests/test_perceptual_optimizer_v2.cpp",
        "ClosedFormMatchesMapQuadraticExactMinimizer",\n        "ClosedFormBeatsLegacyIterationAgainstSameExactObjective",
        "BoxConstraintProducesConstrainedOptimum",
        "ExponentialContextRegularizationIsPositiveAndMonotonic",
        "RejectsNonFiniteTargetAndInvalidBounds")
require("app/src/main/cpp/tests/CMakeLists.txt", "ivanna_add_test(test_perceptual_optimizer_v2)")

require("app/src/main/cpp/spatial/SafmusEvolutionGate.hpp", "mayApplyEvolutionCandidate",
        "std::isfinite(e.fitness)", "e.headroomValid")
require("app/src/main/cpp/audio_orchestrator.cpp", "SafmusEvolutionGate.hpp",
        "mayApplyEvolutionCandidate(evidence)", "std::clamp(output, -0.98f, 0.98f)",
        "if (routeEnabled && g_orch.genome_valid && signalFinite")
require("app/src/main/cpp/evolutionary_kernel_v2.cpp", "!std::isfinite(F)",
        "!std::isfinite(loudness)", "std::clamp(dynamicRange, 0.0f, 1.0f)")
require("app/src/main/cpp/tests/test_evolutionary_kernel.cpp",
        "SAFMUSEvolutionGate", "NonFiniteAudioCuesAreRejectedWithoutPoisoningFitness")
require("app/src/test/java/com/ivanna/omega/safmus/SafmusUniversalKernelTest.kt",
        "sliderControlValuesRejectNonFiniteAndClampToDeclaredRange",
        "evolutionRequiresFiniteFitnessHeadroomStabilityAndLatency")

require("app/src/main/java/com/ivanna/omega/safmus/SafmusUniversalKernel.kt",
        "gateHrtfDataset", "gateRirConfiguration")
require("app/src/main/java/com/ivanna/omega/safmus/SafmusUniversalKernel.kt",
        "enum class HardwareResource", "gateHardwareOptimization", "confirmHardwareOptimization",
        "owningSessionConfirmed", "telemetryFresh", "rollbackAvailable")
require("app/src/test/java/com/ivanna/omega/safmus/SafmusUniversalKernelTest.kt",
        "hardwareOptimizationRequiresPermissionFreshnessBudgetAndRollback",
        "hardwareOptimizationReadbackMustMatchBeforeSuccessIsReported")
require("app/src/main/java/com/ivanna/omega/spatial/IvannaSpatialManager.kt", "gateHrtfDataset")
require("app/src/main/java/com/ivanna/omega/magisk/OmegaEngineBridge.kt", "gateRirConfiguration")
require("app/src/main/java/com/ivanna/omega/ui/SupremeAxesPrefs.kt", "if (value.isFinite()) value else def")
require("app/src/main/java/com/ivanna/omega/ui/SupremeAxesPrefs.kt", "internal fun sanitizeState(input: SupremeAxesState)", "val safeState = sanitizeState(s)", "safeSetWarpedLatticeBlDrive(safeState.warpedLatticeBlDrive)")
require("app/src/main/java/com/ivanna/omega/audio/RoomMeasurement.kt", "gateRoomCorrection", "candidateFiniteAndStable = candidateSafe")
require("app/src/main/cpp/spatial/RoomCorrectionEq.hpp", "safetyGain_", "std::clamp(left, -0.98f, 0.98f)")
require("app/src/main/cpp/spatial/RirConvolver.hpp", "tailLatencyWithinBudgetFor", "tailLatencyBudgetExceeded_")
require("app/src/main/cpp/spatial/RirConvolver.cpp", "if (tailOverBudget) irLen = MAX_IR")
require("app/src/main/cpp/jni/ivanna_omega_jni.cpp", "nativeGetLocalRirTailStatus", "tailLatencyBudgetExceeded()")
require("app/src/main/java/com/ivanna/omega/magisk/OmegaEngineBridge.kt", "nativeGetLocalRirTailStatus", "fun localRirTailStatus(): Int")
require("app/src/main/java/com/ivanna/omega/perceptual/PerceptualSceneRuntime.kt", "localRirTailStatus", "omitió la cola retrasada")
require("app/src/test/java/com/ivanna/omega/perceptual/PerceptualSceneRuntimeTest.kt",
        "localRirTailBudgetDegradationCannotBeReportedAsApplied",
        "unknownLocalRirTailStatusCannotBeReportedAsApplied")
require("app/src/main/cpp/tests/test_supreme_five_axes.cpp", "SAFMUSRirLatencyGate", "OmitsDelayedTailWhenSampleRateExceedsBudget")
require("app/src/main/cpp/tests/test_room_probe.cpp", "ActiveCorrectionSanitizesAndKeepsStereoPeakBelowCeiling")


require("app/src/main/cpp/spatial/ivanna_object_renderer.cpp", "rirConvolver_->setSampleRate(targetSr)")
require("app/src/main/cpp/omega_effect.cpp", "ctx->rirConvolver->setSampleRate(static_cast<int>(srSession))")
require("app/src/main/cpp/jni/ivanna_omega_jni.cpp", "conv->setSampleRate(sessionSr)")

# Magisk environment coordinator is wired into the existing service and stays observation-only.
require("magisk_module/core/safmus_environment_optimizer.sh",
        "environment_profile.env", "resource_budget=$budget", "actuation=observation_only",
        "Camera/media device nodes", "Room acoustics need a user-authorized in-app measurement",
        'TMP="$PROFILE.tmp.$$"', 'RTMP="$REPORT.tmp.$$"', 'CTMP="$CAPABILITIES.tmp.$$"',
        "cpu_frequency_policies", "gpu_nodes", "display_peak_refresh_setting", "sensor_nodes", "battery_nodes")
require("magisk_module/service.sh", "safmus_environment_optimizer.sh",
        "SAFMUS: map whole-device resource conditions", '"$SAFMUS_ENV"')

# Whole-device adapters must be started by Application and feed the existing DSP governor.
require("app/src/main/java/com/ivanna/omega/safmus/SafmusHardwareCoordinator.kt",
        "object SafmusHardwareCoordinator", "HardwareProbe.probe(context)",
        "setExternalResourceBudget(", "actuation=delegated_to_existing_ivanna_subsystems",
        "thermal_status", "available_memory_kb", "camera_count", "audio_outputs", "sensor_count")
require("app/src/main/java/com/ivanna/omega/core/IVANNAApplication.kt",
        "SafmusHardwareCoordinator.start(this, appScope)")
require("app/src/main/java/com/ivanna/omega/audio/ThermalGovernor.kt",
        "fun setExternalResourceBudget(level: Int)", "applyCombinedPolicy", "externalResourcePressure")

if errors:
    print("SAFMUS transversal wiring audit FAILED:")
    for error in errors:
        print(f" - {error}")
    sys.exit(1)
print("SAFMUS transversal wiring audit PASSED: UI slider, bridge, HRTF/RIR, evolution and regression guards are connected.")
