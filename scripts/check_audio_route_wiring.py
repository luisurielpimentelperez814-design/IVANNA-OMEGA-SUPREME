#!/usr/bin/env python3
"""Static contract for IVANNA's real audio-route wiring.

This is a source-graph guard, not proof of device runtime or acoustic output.
Device gates remain the Moto G85 beacon/AudioFlinger/PAL probes.
"""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]

def read(path: str) -> str:
    p = ROOT / path
    if not p.is_file():
        raise AssertionError(f"missing source: {path}")
    return p.read_text(encoding="utf-8", errors="replace")

def require(source: str, needles: tuple[str, ...], label: str) -> None:
    missing = [needle for needle in needles if needle not in source]
    if missing:
        raise AssertionError(f"{label}: missing call-site(s): {missing}")

def main() -> int:
    app = read("app/src/main/java/com/ivanna/omega/core/IVANNAApplication.kt")
    activity = read("app/src/main/java/com/ivanna/omega/MainActivity.kt")
    self_heal = read("app/src/main/java/com/ivanna/omega/agent/SelfHealingAgent.kt")
    route = read("app/src/main/java/com/ivanna/omega/audio/AudioRouteManager.kt")
    calibrator = read("app/src/main/java/com/ivanna/omega/audio/RouteDspCalibrator.kt")
    player = read("app/src/main/java/com/ivanna/omega/audio/IvannaBridgePlayer.kt")
    resampler = read("app/src/main/java/com/ivanna/omega/audio/StereoAudioResampler.kt")
    pll = read("app/src/main/java/com/ivanna/omega/audio/AudioTrackBufferPll.kt")
    effect = read("app/src/main/cpp/omega_effect.cpp")
    jni = read("app/src/main/cpp/jni/ivanna_omega_jni.cpp")
    hal = read("app/src/main/cpp/daemon/hal_clock_producer.cpp")
    hal_h = read("app/src/main/cpp/daemon/hal_clock_producer.hpp")
    asrc_doc = read("docs/ASRC_SOFTWARE_CLOCK_WIRING.md")
    dispatcher_h = read("app/src/main/cpp/spatial/PhysicalTargetDispatcher.hpp")

    checks = [
        (app, ("AudioRouteManager.start(this)", "RouteDspCalibrator.start(this)",
               "IvannaUnifiedPipeline.start(this)", "AudioBackendSelector.start(this)"),
         "application route bootstrap"),
        (activity, ("IvannaNativeLib.nativeInitDSP(sr)",
                    "IvannaNativeLib.nativeCreateAdaptiveEngine()",
                    "AdaptiveDecisionEngine iniciado"),
         "process bootstrap for the real adaptive decision engine"),
        (self_heal, ("val routeActive", "it[0].toInt() in 1..2",
                     "IvannaNativeLib.nativeCreateAdaptiveEngine()",
                     "DSP preservado"),
         "non-destructive route-aware adaptive recovery"),
        (route, ("AudioEngine.nativeSetRouteProfileStatic", "OmegaEngineBridge.setRouteProfile"),
         "route profile fan-out to app JNI and system daemon"),
        (calibrator, ("nativeSetHrtfWetDryStatic", "nativeSetHRTFEnabled",
                      "applyAsyncDaemonStages", "setRoom"),
         "route-specific HRTF/RIR calibration"),
        (effect, ("fc->setRouteProfile(", "fc->setEqGains(", "fc->setHrtfEnabled(",
                   "fc->setSafLatentParams(", "fc->setWfsEnabled(", "fc->setNpe(",
                   "ctx->audioPipeline->processLiveSpatialAxes(", "ctx->rirConvolver->process("),
         "Route A / AudioFlinger consumers"),
        (jni, ("pipe.processLiveSpatialAxes(", "Java_com_ivanna_omega_dsp_DSPBridge_nativeProcess"),
         "in-process JNI audio consumer"),
        (player, ("StereoAudioResampler(sampleRate)", "AudioTrackBufferPll(sampleRate)",
                  "bufferPll.observe(", "resampler.setClockCorrectionPpm(",
                  "track.getTimestamp(outputTimestamp)", "resampler.process(inputChunk)"),
         "owned-player software ASRC loop"),
        (resampler, ("fun setClockCorrectionPpm(", "clockCorrectionPpm"),
         "owned-player variable-ratio FIR actuator"),
        (pll, ("fun observe(", "MAX_MEASURED_DRIFT_PPM", "MAX_PAIR_POINTS"),
         "bounded output-clock estimator"),
        (hal, ("bool HalClockProducer::ensureSource()", "ProducerState::kNoSource",
               "sin publicador ligado al stream HAL/PAL propietario"),
         "HAL/PAL fail-closed clock source"),
        (hal_h, ("PairedSample", "streamId", "routeGeneration", "source = nullptr"),
         "correlated HAL sample identity contract"),
        (asrc_doc, ("not the actuator", "does not control the Qualcomm HAL",
                    "system-wide correction", "PAL/HAL")),
        (dispatcher_h, ("Experimental DSP kernel", "five logical output buses",
                        "does not open an Android audio device")),
    ]

    try:
        for item in checks:
            if len(item) == 3:
                require(item[0], item[1], item[2])
            else:
                require(item[0], item[1], "documentation/physical-output contract")
        # The adaptive watchdog must never reset the whole DSP graph just
        # because no adaptive block has been observed yet (silence/cold start).
        heal_start = self_heal.index("// ── 2. Motor adaptativo:")
        heal_end = self_heal.index("// ── 3. Clipping sostenido", heal_start)
        if "IvannaNativeLib.nativeResetDSP()" in self_heal[heal_start:heal_end]:
            raise AssertionError("adaptive watchdog must not reset the complete DSP graph")
        init_at = activity.index("IvannaNativeLib.nativeInitDSP(sr)")
        create_at = activity.index("IvannaNativeLib.nativeCreateAdaptiveEngine()", init_at)
        evo_at = activity.index("IvannaNativeLib.nativeStartEvoThread()", create_at)
        if not init_at < create_at < evo_at:
            raise AssertionError("adaptive engine must be started after DSP init and before process control loop")

        # Explicitly reject a false claim that the experimental 16->5 kernel is
        # already connected to the production system effect or JNI audio route.
        if "PhysicalTargetDispatcher" in effect or "PhysicalTargetDispatcher" in jni:
            raise AssertionError(
                "PhysicalTargetDispatcher call-site appeared: add a real route-capability "
                "gate, output channel negotiation, matrix validation and runtime tests "
                "before marking 5-channel output production-ready."
            )
    except AssertionError as exc:
        print(f"FAIL audio-route-wiring: {exc}", file=sys.stderr)
        return 1

    print("PASS application route bootstrap: route manager, calibrator, unified state, backend selector")
    print("PASS Route A: AudioFlinger snapshot -> FusionCore EQ/HRTF/SAF/WFS/NPE/RIR/spatial pipeline")
    print("PASS app-owned JNI entrypoint: in-process spatial pipeline (not a third output route)")
    print("PASS owned-player ASRC: AudioTrack timestamps -> bounded PLL -> stereo FIR resampler")
    print("PASS HAL/PAL boundary: no uncorrelated PCM source; system-wide native ASRC remains fail-closed")
    print("PASS physical-output boundary: 16->5 remains logical/experimental, not a claimed phone 5.0 route")
    print("NOTE static source contract only; Moto G85 runtime validation is still required.")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
