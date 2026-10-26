#!/usr/bin/env python3
"""Measure PCM WAV captures for IVANNA. File analysis only; not a live-route certificate."""
import argparse
import json
import math
import wave
from pathlib import Path


def load_wav(path):
    with wave.open(str(path), "rb") as wav:
        channels, width, rate, frames = wav.getnchannels(), wav.getsampwidth(), wav.getframerate(), wav.getnframes()
        if wav.getcomptype() != "NONE" or width not in (1, 2, 3, 4):
            raise ValueError("Expected uncompressed integer PCM WAV, 8/16/24/32-bit")
        raw = wav.readframes(frames)
    if channels < 1 or rate < 1 or len(raw) != frames * channels * width:
        raise ValueError("Invalid or truncated WAV")
    scale = float(1 << (8 * width - 1))
    data = [[] for _ in range(channels)]
    clipped = 0
    for off in range(0, len(raw), width):
        chunk = raw[off:off + width]
        if width == 1:
            value = chunk[0] - 128
        else:
            value = int.from_bytes(chunk, "little", signed=False)
            if value & int(scale):
                value -= int(scale * 2)
        clipped += int(value >= scale - 1 or value <= -scale)
        data[(off // width) % channels].append(value / scale)
    return {"channels": channels, "bits_per_sample": width * 8, "sample_rate_hz": rate,
            "frames": frames, "duration_s": frames / rate, "samples": data,
            "clipped_samples": clipped}


def rms(x):
    return math.sqrt(sum(v * v for v in x) / len(x)) if x else 0.0


def dbfs(x):
    return 20 * math.log10(x) if x > 0 else None


def fit_tone(x, rate, hz):
    if not 0 < hz < rate / 2:
        raise ValueError("tone-hz must be between 0 and Nyquist")
    n = len(x)
    if n < max(64, int(rate / hz * 8)):
        raise ValueError("Capture too short for a stable tone estimate")
    # Least-squares fit y = a*sin(wt) + b*cos(wt) + dc, including cross terms.
    ss = cc = sc = s1 = c1 = sy = cy = y1 = 0.0
    for i, y in enumerate(x):
        s, c = math.sin(2 * math.pi * hz * i / rate), math.cos(2 * math.pi * hz * i / rate)
        ss += s*s; cc += c*c; sc += s*c; s1 += s; c1 += c
        sy += s*y; cy += c*y; y1 += y
    matrix = [[ss, sc, s1], [sc, cc, c1], [s1, c1, float(n)]]
    rhs = [sy, cy, y1]
    for col in range(3):
        pivot = max(range(col, 3), key=lambda row: abs(matrix[row][col]))
        if abs(matrix[pivot][col]) < 1e-14:
            raise ValueError("Degenerate tone fit")
        matrix[col], matrix[pivot] = matrix[pivot], matrix[col]
        rhs[col], rhs[pivot] = rhs[pivot], rhs[col]
        div = matrix[col][col]
        matrix[col] = [v / div for v in matrix[col]]
        rhs[col] /= div
        for row in range(3):
            if row == col:
                continue
            factor = matrix[row][col]
            matrix[row] = [matrix[row][j] - factor * matrix[col][j] for j in range(3)]
            rhs[row] -= factor * rhs[col]
    a, b, dc = rhs
    fundamental = math.sqrt((a*a + b*b) / 2)
    residual = 0.0
    for i, y in enumerate(x):
        estimate = a * math.sin(2 * math.pi * hz * i / rate) + b * math.cos(2 * math.pi * hz * i / rate) + dc
        residual += (y - estimate) ** 2
    residual_rms = math.sqrt(residual / n)
    return {"fundamental_rms_dbfs": dbfs(fundamental), "residual_rms_dbfs": dbfs(residual_rms),
            "thdn_percent": 100 * residual_rms / fundamental if fundamental else None,
            "dc_from_fit": dc}


def analyze(path, reference=None, tone_hz=None):
    measured = load_wav(path)
    samples = measured.pop("samples")
    result = dict(measured)
    result["file"] = str(path)
    result["channel_metrics"] = []
    for idx, x in enumerate(samples):
        peak = max(abs(v) for v in x) if x else 0
        result["channel_metrics"].append({"channel": idx + 1, "rms_dbfs": dbfs(rms(x)),
            "peak_dbfs": dbfs(peak), "dc_offset": sum(x) / len(x) if x else 0})
    total = result["frames"] * result["channels"]
    result["clipping_percent"] = 100 * result["clipped_samples"] / max(1, total)
    if len(samples) >= 2:
        l, r = result["channel_metrics"][:2]
        result["channel_rms_imbalance_db"] = abs(l["rms_dbfs"] - r["rms_dbfs"]) if l["rms_dbfs"] is not None and r["rms_dbfs"] is not None else None
    if tone_hz is not None:
        result["tone_analysis"] = [fit_tone(x, result["sample_rate_hz"], tone_hz) for x in samples]
        result["tone_analysis_warning"] = "THD+N estimate applies only to a clean known-frequency sine; capture-chain distortion is included."
    if reference:
        ref = load_wav(reference)
        if (result["sample_rate_hz"], result["channels"]) != (ref["sample_rate_hz"], ref["channels"]):
            raise ValueError("Reference and capture must have the same sample rate and channel count")
        ref_samples = ref.pop("samples")
        n = min(result["frames"], ref["frames"])
        if n == 0:
            raise ValueError("Empty reference or capture")
        snrs = []
        for captured, expected in zip(samples, ref_samples):
            captured, expected = captured[:n], expected[:n]
            err = rms([a-b for a, b in zip(captured, expected)])
            snrs.append(dbfs(rms(expected) / err) if err else None)
        result["reference_comparison"] = {"reference_file": str(reference), "compared_frames_per_channel": n,
            "length_delta_frames": result["frames"] - ref["frames"], "snr_db_per_channel": snrs,
            "warning": "Requires sample-aligned identical signals. Gain/latency mismatch counts as error; null SNR means zero residual."}
    result["scope_warning"] = "WAV metrics only; this does not prove live Android routing, negotiated Bluetooth codec, end-to-end latency, acoustic SPL, or calibrated device THD."
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("wav", help="PCM WAV capture")
    parser.add_argument("--reference", help="sample-aligned reference WAV")
    parser.add_argument("--tone-hz", type=float, help="known sine frequency for THD+N estimate")
    parser.add_argument("--json-out", help="write JSON report")
    args = parser.parse_args()
    report = analyze(args.wav, args.reference, args.tone_hz)
    text = json.dumps(report, indent=2, ensure_ascii=False, allow_nan=False)
    print(text)
    if args.json_out:
        Path(args.json_out).write_text(text + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
