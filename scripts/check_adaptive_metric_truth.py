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
        bridge_start = jni.index("static void audioRouteBridgeLoop()")
        bridge_end = jni.index("static inline float adaptive_mode_base_strength", bridge_start)
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
            "ctx->adaptiveDecisionTargetGain",
            "ctx->adaptiveDecisionCompAmount",
            "const float effectiveRatio = std::max(v2Ratio, adeRatio);",
            "adaptiveGainTarget = std::clamp(overallTrim * isoGain * adeGain, 0.50f, 1.0f);",
        ):
            if needle not in effect:
                raise AssertionError(f"AudioEffect route metric/actuator wiring missing: {needle}")

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
