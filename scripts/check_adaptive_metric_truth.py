#!/usr/bin/env python3
"""Guard the adaptive DSP core against fabricated/unknown audio metrics."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    file = ROOT / path
    if not file.is_file():
        raise AssertionError(f"missing source: {path}")
    return file.read_text(encoding="utf-8", errors="replace")


def main() -> int:
    header = read("app/src/main/cpp/experimental/adaptive_engine/adaptive_decision_engine.hpp")
    engine = read("app/src/main/cpp/experimental/adaptive_engine/adaptive_decision_engine.cpp")
    jni = read("app/src/main/cpp/jni/ivanna_omega_jni.cpp")
    spatial = read("app/src/main/cpp/spatial/IvannaAudioPipeline.hpp")
    effect = read("app/src/main/cpp/omega_effect.cpp")
    wave = read("app/src/main/cpp/include/omega_wave_stages.hpp")
    seed = read("app/src/main/cpp/jni/ivanna_spatial_jni.cpp")
    reality = read("app/src/main/cpp/include/acoustic_reality_hyperengine.hpp")
    control_bus = read("app/src/main/cpp/include/omega_control_bus.h")
    app_boot = read("app/src/main/java/com/ivanna/omega/core/IVANNAApplication.kt")
    activity_boot = read("app/src/main/java/com/ivanna/omega/MainActivity.kt")
    self_heal = read("app/src/main/java/com/ivanna/omega/agent/SelfHealingAgent.kt")
    lifecycle_test = read("app/src/main/cpp/experimental/adaptive_engine/tests/test_adaptive_engine.cpp")
    voice_controller = read("app/src/main/java/com/ivanna/omega/audio/VoiceProtectionController.kt")
    dsp_bridge = read("app/src/main/java/com/ivanna/omega/dsp/DSPBridge.kt")
    command_server = read("app/src/main/cpp/daemon/control/command_server.cpp")
    route_contract = read("scripts/check_audio_route_wiring.py")

    try:
        for needle in (
            "float voice_score        = 0.0f;",
            "bool band_energy_valid   = false;",
            "bool voice_score_valid   = false;",
            "inline float trustedVoiceScore(",
            "inline float trustedBandEnergy(",
        ):
            if needle not in header:
                raise AssertionError(f"RawAudioMetrics truth contract missing: {needle}")

        for needle in (
            "return trustedVoiceScore(m);",
            "const float voiceScore = trustedVoiceScore(m);",
            "m.band_energy_valid",
            "trustedBandEnergy(m, m.band_high_energy)",
        ):
            if needle not in engine:
                raise AssertionError(f"adaptive engine does not honor metric validity: {needle}")
        if "clamp01(m.voice_score)" in engine:
            raise AssertionError("adaptive personality path bypasses voice-score validity")

        forbidden_jni = (
            "rawM.band_low_energy  = rms * 0.35f",
            "rawM.band_mid_energy  = rms * 0.45f",
            "rawM.band_high_energy = rms * 0.20f",
            "rawM.voice_score = std::clamp((rawM.band_mid_energy",
        )
        for needle in forbidden_jni:
            if needle in jni:
                raise AssertionError(f"fabricated Route B metric reintroduced: {needle}")

        for needle in (
            "rawM.band_energy_valid = false;",
            "rawM.voice_score_valid = false;",
            "rawM.voice_score_valid = g_voice_protect_score_valid.load",
        ):
            if needle not in jni:
                raise AssertionError(f"JNI metric provenance is not wired: {needle}")
        bridge_start = jni.find("static void audioRouteBridgeLoop()")
        if bridge_start < 0:
            raise AssertionError("app audio route bridge function is missing")
        bridge_end = jni.find("static inline float adaptive_mode_base_strength", bridge_start)
        if bridge_end < 0 or bridge_end <= bridge_start:
            raise AssertionError("app audio route bridge end marker is missing or out of order")
        bridge = jni[bridge_start:bridge_end]
        for stale_source in ("shared->ai_raw_rms.load", "shared->ai_raw_peak.load",
                             "shared->ai_band_low.load", "shared->ai_yamnet_class_id.load"):
            if stale_source in bridge:
                raise AssertionError(f"app bridge consumes an unwritten/unsynchronized SHM field: {stale_source}")
        if "OMEGA_EFFECT_LOCAL_BUS_PATH" not in bridge:
            raise AssertionError("app fallback bridge has no real per-effect PCM telemetry source")

        if "rawM.voice_score = std::clamp((rawM.band_mid_energy" in spatial:
            raise AssertionError("spatial pipeline still derives voice from mid-band energy")
        if "rawM.band_energy_valid = true;" not in spatial:
            raise AssertionError("spatial pipeline does not mark measured bands valid")

        for needle in (
            "ctx->lastVoiceProbValid",
            "rawM.voice_score_valid = ctx->lastVoiceProbValid;",
            "rawM.band_energy_valid = true;",
            "rawM.gain_reduction_db = ctx->safetyLimiter",
            "if (ctx->adaptive && ctx->adaptiveEnabled)",
            "if (ctx->adaptive && !ctx->adaptive->start())",
            "ctx->adaptiveEnabled = false;",
            "ctx->adaptiveDecisionTargetGain",
            "ctx->adaptiveDecisionCompAmount",
            "ctx->adaptiveDecisionExciterReduction",
            "const float requestedRatio = 1.0f + 7.0f * std::clamp(",
            "const float effectiveRatio = std::max(v2Ratio, std::max(requestedRatio, adeRatio));",
            "const float effectiveThreshold =",
            "ctx->pendingSnap.compressor",
            "const float requestedGain = std::clamp(",
            "adaptiveGainTarget = std::clamp(",
            "overallTrim * isoGain * requestedGain * adeGain, 0.50f, 1.0f);",
            "omega_neutralize_adaptive_snapshot(ctx->pendingSnap);",
            "const float totalExciterReduction =",
            "std::max(requestedExciterReduction, adeExciterReduction);",
        ):
            if needle not in effect:
                raise AssertionError(f"AudioEffect route metric/actuator wiring missing: {needle}")

        for stale_writer in (
            "ctx->pendingSnap.target_gain = ctx->adaptiveDecisionTargetGain",
            "ctx->pendingSnap.comp_amount = ctx->adaptiveDecisionCompAmount",
        ):
            if stale_writer in effect:
                raise AssertionError(f"ADE overwrites control-plane snapshot field: {stale_writer}")
        if effect.count("omega_neutralize_adaptive_snapshot(ctx->pendingSnap)") < 3:
            raise AssertionError("system-wide, initial seed and local routes do not all neutralize adaptive snapshot fields on OFF")
        neutral_start = effect.find("static inline void omega_neutralize_adaptive_snapshot")
        neutral_end = effect.find("\n}", neutral_start) if neutral_start >= 0 else -1
        neutralizer = effect[neutral_start:neutral_end] if neutral_end > neutral_start else ""
        if "s.comp_amount" in neutralizer:
            raise AssertionError("adaptive OFF must preserve the user's manual compressor amount")
        if "s.target_gain = 1.0f;" not in neutralizer or "s.exc_red = 0.0f;" not in neutralizer:
            raise AssertionError("adaptive OFF must neutralize only its separate gain/exciter requests")
        if "ctx->adaptive->rawMetrics.publish(" not in effect:
            raise AssertionError("per-session ADE has no real PCM metric producer")
        if "if (ctx && ctx->adaptive && !ctx->adaptive->running()" not in effect:
            raise AssertionError("per-session ADE ENABLE callback does not retry failed thread creation")

        for needle in (
            "bool voiceScoreValid = false;",
            "rawM.voice_score_valid = voiceScoreValid;",
            "rawM.band_energy_valid = true;",
        ):
            if needle not in wave:
                raise AssertionError(f"wave worker does not publish inference validity: {needle}")

        for fake_seed in ("seedM.rms", "seedM.peak", "seedM.band_low_energy",
                          "seedM.band_mid_energy", "seedM.band_high_energy",
                          "seedM.voice_score"):
            if fake_seed in seed:
                raise AssertionError(f"spatial startup seed must remain neutral: {fake_seed}")
        if "trustedVoiceScore(metrics)" not in reality:
            raise AssertionError("AcousticReality still consumes unvalidated voice score")
        if "rawMetrics.band_high_energy *" in reality:
            raise AssertionError("AcousticReality sibilance proxy bypasses spectral validity")
        if "metrics.voice_score" in reality:
            raise AssertionError("AcousticReality has a direct unvalidated voice-score read")

        # Lifecycle contract: start is retryable, create cannot override a
        # user-disabled state, and UI/watchdog restore the persisted request.
        for needle in (
            "bool AdaptiveDecisionEngine::start() noexcept",
            "controlLifecycleMutex_",
        ):
            source = engine if "AdaptiveDecisionEngine::start" in needle else header
            if needle not in source:
                raise AssertionError(f"adaptive lifecycle contract missing: {needle}")
        if "pthread_create(" not in engine or "createResult != 0" not in engine:
            raise AssertionError("Android -fno-exceptions lifecycle does not handle pthread resource failure")
        if "running_.store(false, std::memory_order_release);" not in engine[engine.find("const int createResult = pthread_create"):]:
            raise AssertionError("pthread creation failure leaves ADE falsely marked running")
        if "controlThreadJoinable_ = false;" not in engine:
            raise AssertionError("pthread lifecycle has no successful join/drain transition")
        if "pthread_t controlThread_" not in header:
            raise AssertionError("Android ADE lifecycle does not own a joinable pthread handle")
        if "if (!ensureAdaptiveEngineStarted()) return 0;" not in jni:
            raise AssertionError("nativeCreateAdaptiveEngine can claim a disabled/failed engine is ready")
        ensure_start = jni.find("static bool ensureAdaptiveEngineStarted")
        ensure_end = jni.find("\n}", ensure_start) if ensure_start >= 0 else -1
        ensure_body = jni[ensure_start:ensure_end] if ensure_end > ensure_start else ""
        if "!g_initialized.load(std::memory_order_acquire)" not in ensure_body:
            raise AssertionError("explicit create/watchdog can start ADE before the native DSP graph is ready")
        if "if (g_adaptiveEngine.running()) g_adaptiveEngine.stop();" not in ensure_body:
            raise AssertionError("ADE thread is not drained when the native DSP graph becomes unavailable")
        if "g_adaptiveEngineUiEnabled.store(true" in jni:
            raise AssertionError("nativeCreateAdaptiveEngine must not override the user's OFF/manual preference")
        if "g_adaptiveEngine.running()" not in jni:
            raise AssertionError("JNI health query does not inspect the real control thread")
        enabled_start = jni.find("static bool setAdaptiveEngineEnabled")
        enabled_end = jni.find("\n}", enabled_start) if enabled_start >= 0 else -1
        enabled_body = jni[enabled_start:enabled_end] if enabled_end > enabled_start else ""
        if "!g_initialized.load(std::memory_order_acquire)" not in enabled_body:
            raise AssertionError("native ON preference can start the control thread before DSP initialization")
        native_init_start = jni.find("Java_com_ivanna_omega_dsp_DSPBridge_nativeInit(")
        native_init_end = jni.find("Java_com_ivanna_omega_dsp_DSPBridge_nativeSet", native_init_start)
        native_init = jni[native_init_start:native_init_end] if native_init_end > native_init_start else ""
        graph_ready = native_init.find("g_initialized.store(true, std::memory_order_release);")
        ade_start = native_init.find("ensureAdaptiveEngineStarted()")
        if graph_ready < 0 or ade_start < graph_ready:
            raise AssertionError("DSPBridge starts ADE before the native graph is initialized")
        unload_start = jni.rfind("JNI_OnUnload")
        if unload_start < 0:
            raise AssertionError("JNI_OnUnload is missing")
        if "(void)setAdaptiveEngineEnabled(false);" not in jni[unload_start:unload_start + 1800]:
            raise AssertionError("JNI unload does not unconditionally stop/join the adaptive engine")
        if "AdaptiveControlsPrefs.load(this).binauralAdaptEnabled" not in app_boot:
            raise AssertionError("Application bootstrap does not restore the persisted adaptive preference")
        if "omega_effect_features_encode(true, false)" not in control_bus:
            raise AssertionError("default control snapshot must fail closed for adaptive actuation")
        if "omega_effect_features_encode(true, true)" in control_bus:
            raise AssertionError("default control snapshot must not silently auto-enable adaptive actuation")
        if "AdaptiveControlsPrefs.load(applicationContext).binauralAdaptEnabled" not in activity_boot:
            raise AssertionError("Activity bootstrap does not honor the persisted adaptive preference")
        if "val preferred =" not in activity_boot or "val requested = preferred && dspReady" not in activity_boot:
            raise AssertionError("Activity conflates user preference with DSP readiness")
        if "nativeSetAdaptiveEngineEnabled(preferred)" not in activity_boot:
            raise AssertionError("Activity discards the user's ON preference when DSP init is temporarily unavailable")
        if "fun currentAdaptiveRequested(): Boolean" not in app_boot:
            raise AssertionError("Application has no live preference reader for daemon reconnect")
        if app_boot.count("queueSystemWideAdaptiveEnabled(currentAdaptiveRequested())") < 2:
            raise AssertionError("daemon startup/reconnect reuses a stale adaptive preference")
        watchdog_start = self_heal.find("// ── 2. Motor adaptativo")
        watchdog_end = self_heal.find("// ── 3. Clipping sostenido", watchdog_start) if watchdog_start >= 0 else -1
        if watchdog_start < 0 or watchdog_end <= watchdog_start:
            raise AssertionError("self-heal adaptive watchdog section markers are missing or out of order")
        watchdog_section = self_heal[watchdog_start:watchdog_end]
        if (
            "adaptiveRequested &&" not in watchdog_section
            or "nativeIsAdaptiveEngineRunning()" not in watchdog_section
            or "!engineRunning" not in watchdog_section
            or "routeActive" in watchdog_section
        ):
            raise AssertionError("self-heal can override user preference or confuse silence with thread death")
        if "!AudioStateManager.audioState.value.manualModeEnabled" not in self_heal:
            raise AssertionError("self-heal does not respect the manual-mode state")
        if "testControlThreadLifecycle();" not in lifecycle_test:
            raise AssertionError("native lifecycle regression test is not in the test runner")
        if "testConcurrentControlThreadLifecycle();" not in lifecycle_test:
            raise AssertionError("concurrent start/stop lifecycle regression test is not in the test runner")
        sound_screen = read("app/src/main/java/com/ivanna/omega/ui/SoundScreen.kt")
        hrtf_toggle = sound_screen.find('requestRouteFeatures(en, routeAdaptiveEnabled)')
        if hrtf_toggle < 0:
            raise AssertionError("HRTF toggle can republish adaptive=true while manual mode is active")

        # Voice inference provenance/race: a late synchronous model call may
        # outlive Job.cancel(), so mode transitions invalidate its generation.
        for needle in (
            "inferenceGeneration++",
            "generation != inferenceGeneration",
            "classifierMutex.withLock",
            "DSPBridge.clearVoiceProtectScore()",
        ):
            if needle not in voice_controller:
                raise AssertionError(f"voice inference invalidation contract missing: {needle}")
        if "nativeClearVoiceProtectScore()" not in dsp_bridge:
            raise AssertionError("DSPBridge does not expose the validity-clearing native operation")
        if "Java_com_ivanna_omega_dsp_DSPBridge_nativeClearVoiceProtectScore" not in jni:
            raise AssertionError("JNI does not implement the voice provenance clear operation")
        if "g_voice_protect_score_valid.store(false" not in jni:
            raise AssertionError("voice reset does not clear native score validity")
        voice_release_start = voice_controller.find("fun release()")
        if voice_release_start < 0 or "classifierMutex.withLock" not in voice_controller[voice_release_start:]:
            raise AssertionError("classifier release can race a blocking inference")

        # The mode decision and native create must be serialized with UI events.
        main_thread_start = activity_boot.find("kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.Main.immediate)")
        main_thread_end = activity_boot.find("Triple(requested, handle, running)", main_thread_start) if main_thread_start >= 0 else -1
        if main_thread_start < 0 or main_thread_end <= main_thread_start:
            raise AssertionError("MainActivity serialized adaptive bootstrap markers are missing")
        main_thread_block = activity_boot[main_thread_start:main_thread_end]
        if "AudioStateManager.audioState.value.manualModeEnabled" not in main_thread_block:
            raise AssertionError("MainActivity reads manual mode outside the serialized main-thread block")
        if "nativeCreateAdaptiveEngine()" not in main_thread_block:
            raise AssertionError("MainActivity does not create the ADE in the same serialized preference block")

        # Missing JSON fields must be rejected, not converted into valid zero scores.
        for needle in (
            '_jsonFloat(json,"speech",std::nanf(""))',
            '_jsonFloat(json,"music",std::nanf(""))',
            '_jsonFloat(json,"confidence",std::nanf(""))',
        ):
            if needle not in command_server:
                raise AssertionError(f"YAMNet command accepts a missing field: {needle}")
        route_watchdog_start = route_contract.find('heal_start = self_heal.find("FutureSentinel")')
        if route_watchdog_start >= 0:
            raise AssertionError("unexpected placeholder in audio route contract")
        if "heal_start = self_heal.find(" not in route_contract or "init_at = activity.find(" not in route_contract:
            raise AssertionError("audio route contract must validate missing source markers without ValueError")

    except AssertionError as exc:
        print(f"FAIL adaptive-metric-truth: {exc}", file=sys.stderr)
        return 1

    print("PASS core metric defaults are neutral and carry source-validity flags")
    print("PASS Route A voice score requires VoiceProtectionController inference")
    print("PASS Route B effect-local bands/voice come from its PCM meter and classifier")
    print("PASS Route B ADE compression/gain decisions reach the same PCM actuators")
    print("PASS app bridge rejects unwritten SHM fields and does not claim system-wide actuation")
    print("PASS spatial and wave workers do not fabricate missing voice scores")
    print("PASS AcousticReality/AdaptiveDecisionEngine consume validity-aware metrics")
    print("NOTE static source contract only; Moto G85 runtime evidence remains a separate gate.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
