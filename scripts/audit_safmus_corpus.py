#!/usr/bin/env python3
"""Read-only SAFMUS corpus audit for packaged IVANNA acoustic assets.

Uses only Python's standard library. It does not download, rewrite, train on, or
redistribute any data. SOFA validation here checks container signatures only;
full SOFA metadata validation requires an HDF5/SOFA-aware dependency.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import struct
import wave
from pathlib import Path

HDF5_MAGIC = b"\x89HDF\r\n\x1a\n"


def inspect_sofa(path: Path) -> dict:
    try:
        size = path.stat().st_size
        with path.open("rb") as stream:
            signature = stream.read(8)
        return {"path": str(path), "bytes": size,
                "hdf5_signature": signature == HDF5_MAGIC,
                "status": "container-signature-only"}
    except OSError as exc:
        return {"path": str(path), "status": "error", "error": str(exc)}


def inspect_ihr1(path: Path) -> dict:
    try:
        raw_size = path.stat().st_size
        with path.open("rb") as stream:
            header = stream.read(16)
        if len(header) < 16 or header[:4] != b"IHR1":
            return {"path": str(path), "bytes": raw_size, "status": "invalid-header"}
        positions, ir_len, sample_rate = struct.unpack("<iii", header[4:16])
        valid = positions > 0 and ir_len > 0 and sample_rate > 0
        expected_az = 16 + positions * (4 + 2 * ir_len * 4)
        expected_azel = 16 + positions * (8 + 2 * ir_len * 4)
        valid = valid and raw_size in (expected_az, expected_azel)
        return {"path": str(path), "bytes": raw_size, "positions": positions,
                "ir_length": ir_len, "sample_rate_hz": sample_rate,
                "status": "header-and-size-valid" if valid else "invalid-dimensions-or-size"}
    except (OSError, struct.error) as exc:
        return {"path": str(path), "status": "error", "error": str(exc)}


def inspect_wav(path: Path) -> dict:
    try:
        with wave.open(str(path), "rb") as wav:
            rate, channels, width, frames = (
                wav.getframerate(), wav.getnchannels(), wav.getsampwidth(), wav.getnframes()
            )
        return {"path": str(path), "sample_rate_hz": rate, "channels": channels,
                "sample_width_bytes": width, "frames": frames,
                "duration_s": frames / rate if rate else None,
                "status": "valid-header" if rate > 0 and channels > 0 and frames > 0 else "invalid"}
    except (OSError, wave.Error, EOFError) as exc:
        return {"path": str(path), "status": "error", "error": str(exc)}


def safe_csv_audit(csv_path: Path, rir_dir: Path) -> dict:
    if not csv_path.exists():
        return {"csv": str(csv_path), "status": "not-found"}
    rows, missing, malformed = 0, [], []
    try:
        with csv_path.open(newline="", encoding="utf-8-sig") as stream:
            reader = csv.DictReader(stream)
            headers = reader.fieldnames or []
            filename_key = "filename"
            if filename_key not in headers:
                return {"csv": str(csv_path), "status": "missing-filename-column",
                        "headers": headers}
            for line_no, row in enumerate(reader, start=2):
                rows += 1
                filename = (row.get(filename_key) or "").strip()
                if not filename:
                    malformed.append({"line": line_no, "reason": "empty filename"})
                    continue
                # Reject paths escaping the RIR directory.
                candidate = (rir_dir / filename).resolve()
                if rir_dir.resolve() not in candidate.parents:
                    malformed.append({"line": line_no, "reason": "path escapes RIR directory"})
                elif not candidate.is_file():
                    missing.append({"line": line_no, "filename": filename})
                for key in ("RT60_s", "distance_m", "room_width_m", "room_height_m", "room_depth_m"):
                    if key in row and (row[key] or "").strip():
                        try:
                            value = float(row[key])
                            if not math.isfinite(value) or value < 0:
                                raise ValueError("non-finite or negative")
                        except ValueError:
                            malformed.append({"line": line_no, "field": key, "value": row[key]})
        return {"csv": str(csv_path), "status": "ok" if not missing and not malformed else "issues",
                "rows": rows, "missing_files": missing, "malformed_fields": malformed}
    except OSError as exc:
        return {"csv": str(csv_path), "status": "error", "error": str(exc)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path, help="Optional JSON report path")
    args = parser.parse_args()
    root = args.root.resolve()
    assets = root / "app" / "src" / "main" / "assets"
    omega = assets / "ivanna_omega"
    sofa_files = sorted(set(omega.glob("sofa/**/*.sofa")) | set((assets / "sofa").glob("*.sofa")))
    ihr_files = sorted(omega.glob("hrtf/**/*.ihr1"))
    rir_dir = omega / "rir"
    rir_files = sorted(rir_dir.glob("*.wav"))
    report = {
        "schema_version": 1,
        "root": str(root),
        "read_only": True,
        "counts": {"sofa": len(sofa_files), "ihr1": len(ihr_files), "rir_wav": len(rir_files)},
        "sofa": [inspect_sofa(p) for p in sofa_files],
        "ihr1": [inspect_ihr1(p) for p in ihr_files],
        "rir_wav": [inspect_wav(p) for p in rir_files],
        "rir_metadata": safe_csv_audit(rir_dir / "metadata.csv", rir_dir),
        "notes": [
            "SOFA signature check is not full HDF5/SOFA metadata validation.",
            "A valid file is not evidence of license permission or scientific validity.",
            "Measured, simulated and heuristic-derived labels must remain distinct.",
            "This audit does not train a neural network."
        ]
    }
    rendered = json.dumps(report, indent=2, ensure_ascii=False)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered + "\n", encoding="utf-8")
    print(rendered)
    problems = any(x.get("status") in ("error", "invalid", "invalid-header",
                                       "invalid-dimensions-or-size")
                   for group in ("sofa", "ihr1", "rir_wav") for x in report[group])
    problems = problems or report["rir_metadata"].get("status") in ("error", "issues",
                                                                    "missing-filename-column")
    return 1 if problems else 0


if __name__ == "__main__":
    raise SystemExit(main())
