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
    fotoholo_engine = read("app/src/main/java/com/ivanna/omega/fotoholo/FotoHoloEngine.kt")
    fotoholo_routeb = read("app/src/main/java/com/ivanna/omega/fotoholo/FotoHoloRouteB.kt")
    dispatcher_h = read("app/src/main/cpp/spatial/PhysicalTargetDispatcher.hpp")

    checks = [
        (app, ("AudioRouteManager.start(this)", "RouteDspCalibrator.start(this)",
               "IvannaUnifiedPipeline.start(this)", "AudioBackendSelector.start(this)"),
         "application route bootstrap"),
        (activity, ("IvannaNativeLib.nativeInitDSP(sr)",
                    "IvannaNativeLib.nativeCreateAdaptiveEngine()",
                    "AdaptiveDecisionEngine iniciado"),
         "process bootstrap for the real adaptive decision engine"),
        (self_heal, ("val adaptiveRequested",
                     "!AudioStateManager.audioState.value.manualModeEnabled",
                     "IvannaNativeLib.nativeIsAdaptiveEngineRunning()",
                     "!engineRunning",
                     "IvannaNativeLib.nativeCreateAdaptiveEngine()",
                     "DSP intacto"),
         "preference-first adaptive lifecycle recovery independent of PCM activity"),
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
                  "bufferPll.observe(", "resampler.setClockCorrectionPpm(correctionPpm)",
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
        heal_start = self_heal.find("// ── 2. Motor adaptativo:")
        if heal_start < 0:
            raise AssertionError("SelfHealingAgent adaptive watchdog section is missing")
        heal_end = self_heal.find("// ── 3. Clipping sostenido", heal_start)
        if heal_end < 0 or heal_end <= heal_start:
            raise AssertionError("SelfHealingAgent clipping section marker is missing or out of order")
        if "IvannaNativeLib.nativeResetDSP()" in self_heal[heal_start:heal_end]:
            raise AssertionError("adaptive watchdog must not reset the complete DSP graph")

        init_at = activity.find("IvannaNativeLib.nativeInitDSP(sr)")
        if init_at < 0:
            raise AssertionError("MainActivity native DSP initialization call is missing")
        create_at = activity.find("IvannaNativeLib.nativeCreateAdaptiveEngine()", init_at)
        if create_at < 0:
            raise AssertionError("MainActivity adaptive engine creation call after DSP init is missing")
        evo_at = activity.find("IvannaNativeLib.nativeStartEvoThread()", create_at)
        if evo_at < 0 or not init_at < create_at < evo_at:
            raise AssertionError("nativeInitDSP must precede nativeCreateAdaptiveEngine and nativeStartEvoThread")

        # FotoHolo state publication must heal from the idle loop too: the retry
        # has to run BEFORE the idle branch `continue`s, or a failed publish while
        # FotoHolo is off/without signal stays frozen.
        retry_at = fotoholo_engine.find("FotoHoloRouteB.retryStatePublishIfNeeded()")
        idle_at = fotoholo_engine.find("if (!shouldSample && !modulating && !systemWideRouteActive)")
        if retry_at < 0 or idle_at < 0 or not retry_at < idle_at:
            raise AssertionError(
                "FotoHoloEngine must call retryStatePublishIfNeeded() before the idle "
                "branch that skips the rest of the loop")

        # The retry must read the desired state INSIDE the serial executor (latest
        # intent wins) and must not republish through setFotoHoloState().
        retry_def = fotoholo_routeb.find("fun retryStatePublishIfNeeded()")
        if retry_def < 0:
            raise AssertionError("FotoHoloRouteB.retryStatePublishIfNeeded() is missing")
        retry_end = fotoholo_routeb.find("\n    fun ", retry_def + 1)
        retry_body = fotoholo_routeb[retry_def:retry_end if retry_end > 0 else len(fotoholo_routeb)]
        exec_at = retry_body.find("io.execute")
        desired_at = retry_body.find("desiredEnabled")
        if exec_at < 0 or desired_at < 0 or not exec_at < desired_at:
            raise AssertionError(
                "FotoHolo state retry must read desiredEnabled inside io.execute, not before queuing")
        if "setFotoHoloState(" in retry_body:
            raise AssertionError(
                "FotoHolo state retry must not call setFotoHoloState(): it would overwrite a newer UI intent")

        # nativeInitDSP is the bootstrap path MainActivity uses: it must start the
        # adaptive engine once the DSP graph is initialized (ON preference may have
        # been stored before DSP init), and the helper itself respects OFF/manual.
        dsp_def = jni.find("Java_com_ivanna_omega_core_IvannaNativeLib_nativeInitDSP(")
        if dsp_def < 0:
            raise AssertionError("IvannaNativeLib_nativeInitDSP JNI entrypoint is missing")
        dsp_end = jni.find("\n}\n", dsp_def)
        dsp_body = jni[dsp_def:dsp_end if dsp_end > 0 else len(jni)]
        if "ensureAdaptiveEngineStarted()" not in dsp_body:
            raise AssertionError(
                "nativeInitDSP must call ensureAdaptiveEngineStarted() after the DSP graph is initialized")

        # Explicitly reject a false claim that the experimental 16->5 kernel is
        # already connected to the production system effect or JNI audio route.
        if "PhysicalTargetDispatcher" in effect or "PhysicalTargetDispatcher" in jni:
            raise AssertionError(
                "PhysicalTargetDispatcher call-site appeared: add a real route-capability "
                "gate, output channel negotiation, matrix validation and runtime tests "
                "before marking 5-channel output production-ready."
            )
        # Route-A WFS must obey the same AUX > binaural-WFS priority as
        # Route B while retaining the user's WFS preference for restoration.
        wfs_policy_start = jni.find("static inline bool routeAWfsAllowed()")
        wfs_policy_end = jni.find("\\n}", wfs_policy_start) if wfs_policy_start >= 0 else -1
        wfs_policy = jni[wfs_policy_start:wfs_policy_end] if wfs_policy_end > wfs_policy_start else ""
        if ("g_wfs_enabled.load(std::memory_order_acquire)" not in wfs_policy or
                "!g_aux_field_enabled.load(std::memory_order_acquire)" not in wfs_policy):
            raise AssertionError("effective Route-A WFS state must preserve WFS preference and yield to AUX")
        if ("g_routeAWfsEngine.setEnabled(routeAWfsAllowed());" not in jni or
                "g_routeAWfsEngine.process(left, right, frames);" not in jni):
            raise AssertionError("Route-A renderer must consume the shared effective WFS state")
        if jni.count("processRouteAWfs(g_ats.pdOutL, g_ats.pdOutR, n);") != 1 or jni.count("processRouteAWfs(oL, oR, n);") != 1:
            raise AssertionError("both live Route-A JNI audio consumers must use the AUX-aware WFS policy")
        if jni.count("!routeAWfsAllowed();") != 2 or "||\\n                                              routeAWfsAllowed());" not in jni:
            raise AssertionError("both Route-A spatial gates and the M/S hierarchy must use effective WFS state")
    except AssertionError as exc:
        print(f"FAIL audio-route-wiring: {exc}", file=sys.stderr)
        return 1

    print("PASS application route bootstrap: route manager, calibrator, unified state, backend selector")
    print("PASS Route A: AudioFlinger snapshot -> FusionCore EQ/HRTF/SAF/WFS/NPE/RIR/spatial pipeline")
    print("PASS app-owned JNI entrypoint: in-process spatial pipeline (not a third output route)")
    print("PASS owned-player ASRC: AudioTrack timestamps -> bounded PLL -> stereo FIR resampler")
    print("PASS FotoHolo state retry: runs before the idle branch and publishes the latest intent from the serial executor")
    print("PASS adaptive engine start: nativeInitDSP starts the ADE once the DSP graph is initialized")
    print("PASS HAL/PAL boundary: no uncorrelated PCM source; system-wide native ASRC remains fail-closed")
    print("PASS physical-output boundary: 16->5 remains logical/experimental, not a claimed phone 5.0 route")
    print("NOTE static source contract only; Moto G85 runtime validation is still required.")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
