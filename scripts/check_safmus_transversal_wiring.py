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
        "sanitizeControlValue(rt60S, 0f, 1.5f)", "val safeQ = FloatArray(7)",
        "sanitizeControlValue(compressor, -24f, 0f)", "scores.any { !it.isFinite() }")

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
require("app/src/main/java/com/ivanna/omega/spatial/IvannaSpatialManager.kt", "gateHrtfDataset")
require("app/src/main/java/com/ivanna/omega/magisk/OmegaEngineBridge.kt", "gateRirConfiguration")

if errors:
    print("SAFMUS transversal wiring audit FAILED:")
    for error in errors:
        print(f" - {error}")
    sys.exit(1)
print("SAFMUS transversal wiring audit PASSED: UI slider, bridge, HRTF/RIR, evolution and regression guards are connected.")
