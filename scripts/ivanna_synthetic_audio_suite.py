#!/usr/bin/env python3
"""Generate adversarial PCM stimuli and verify the analyzer catches injected defects."""
import argparse
import json
import math
import struct
import tempfile
import wave
from pathlib import Path
from scripts.ivanna_audio_certify import analyze


def write_stereo(path, kind, seconds=0.25, rate=48000):
    frames = int(seconds * rate)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(rate)
        out = bytearray()
        for i in range(frames):
            t = i / rate
            base = math.sin(2 * math.pi * 1000 * t)
            if kind == "clean_sine":
                left = right = 0.5 * base
            elif kind == "clipping":
                left = right = 1.25 * base
            elif kind == "dc_offset":
                left = right = 0.2 * base + 0.03
            elif kind == "channel_imbalance":
                left, right = 0.6 * base, 0.12 * base
            elif kind == "harmonic_distortion":
                left = right = 0.45 * base + 0.04 * math.sin(2 * math.pi * 3000 * t)
            elif kind == "near_full_scale":
                left = right = 0.92 * base
            elif kind == "silent":
                left = right = 0.0
            else:
                raise ValueError("Unknown synthetic stimulus: " + kind)
            for value in (left, right):
                sample = max(-32768, min(32767, round(value * 32767)))
                out.extend(struct.pack("<h", sample))
        w.writeframes(out)


CASES = [
    {"name": "clean_sine", "tone_hz": 1000, "must_find": [], "must_not_find": ["PCM_CLIPPING", "CHANNEL_IMBALANCE", "DC_OFFSET", "TONE_THDN_HIGH"]},
    {"name": "clipping", "tone_hz": 1000, "must_find": ["PCM_CLIPPING"], "must_not_find": []},
    {"name": "dc_offset", "tone_hz": 1000, "must_find": ["DC_OFFSET"], "must_not_find": []},
    {"name": "channel_imbalance", "tone_hz": 1000, "must_find": ["CHANNEL_IMBALANCE"], "must_not_find": []},
    {"name": "harmonic_distortion", "tone_hz": 1000, "must_find": ["TONE_THDN_HIGH"], "must_not_find": []},
    {"name": "near_full_scale", "tone_hz": 1000, "must_find": ["LOW_PEAK_HEADROOM"], "must_not_find": ["PCM_CLIPPING"]},
    {"name": "silent", "tone_hz": None, "must_find": [], "must_not_find": ["PCM_CLIPPING", "CHANNEL_IMBALANCE", "DC_OFFSET"]},
]


def run_suite(output_dir):
    output_dir = Path(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    cases = []
    failed = []
    with tempfile.TemporaryDirectory() as td:
        for case in CASES:
            wav_path = Path(td) / (case["name"] + ".wav")
            write_stereo(wav_path, case["name"])
            report = analyze(wav_path, tone_hz=case["tone_hz"])
            found = {f["code"] for f in report["findings"]}
            missing = sorted(set(case["must_find"]) - found)
            unexpected = sorted(set(case["must_not_find"]) & found)
            item = {"stimulus": case["name"], "expected_findings": case["must_find"],
                    "actual_findings": sorted(found), "missing_expected": missing,
                    "unexpected_findings": unexpected,
                    "diagnostic_status": report["diagnostic_status"]}
            cases.append(item)
            if missing or unexpected:
                failed.append(case["name"])
    result = {"suite": "IVANNA adversarial synthetic audio", "case_count": len(cases),
              "passed": len(cases) - len(failed), "failed": len(failed),
              "status": "PASS" if not failed else "FAIL", "failed_cases": failed,
              "interpretation": "Synthetic algorithm verification only; never physical-device, Bluetooth, DAC or perceptual certification.",
              "cases": cases}
    (output_dir / "ivanna_synthetic_audio_suite.json").write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output-dir", default="ivanna_synthetic_audio_results")
    args = p.parse_args()
    result = run_suite(args.output_dir)
    print(json.dumps(result, indent=2, ensure_ascii=False))
    raise SystemExit(0 if result["status"] == "PASS" else 1)


if __name__ == "__main__":
    main()
