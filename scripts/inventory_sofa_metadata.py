#!/usr/bin/env python3
"""Inventario reproducible de metadatos SOFA/HDF5; no transforma ni reinterpreta audio."""
from __future__ import annotations
import argparse
import csv
import hashlib
import json
import sys
from pathlib import Path
import numpy as np

try:
    import h5py
except ImportError:
    print("Falta h5py. Instale con: python -m pip install h5py", file=sys.stderr)
    raise SystemExit(2)

DATASETS = (
    "Data.IR", "Data.SamplingRate", "Data.Delay",
    "SourcePosition", "ReceiverPosition", "ListenerPosition",
    "EmitterPosition", "ListenerView", "ListenerUp",
    "ListenerRotation", "SourceView", "SourceUp",
)
ATTRS = (
    "SOFAConventions", "SOFAConventionsVersion", "DataType",
    "RoomType", "ListenerShortName", "ListenerDescription",
    "DatabaseName", "Title", "Author", "Organization",
    "DateCreated", "DateModified", "License", "References",
    "Comment", "History", "ApplicationName",
)

def plain(value):
    if hasattr(value, "tolist"):
        value = value.tolist()
    if isinstance(value, bytes):
        return value.decode("utf-8", "replace")
    if isinstance(value, (list, tuple)):
        return [plain(x) for x in value]
    if isinstance(value, dict):
        return {str(k): plain(v) for k, v in value.items()}
    if isinstance(value, (str, int, float, bool)) or value is None:
        return value
    return str(value)

def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()

def classify(path: Path, attrs: dict, shapes: dict) -> str:
    text = " ".join([
        path.name, str(attrs.get("SOFAConventions", "")),
        str(attrs.get("DataType", "")), str(attrs.get("RoomType", "")),
    ]).lower()
    if "sos" in text or "second order section" in text:
        return "SOS/filter coefficients — no tratar como HRIR"
    if "hpir" in text or "headphone" in text or "headphones" in text:
        return "HPIR/headphone response — verificar convención"
    if "room" in text or "rir" in text or "reverber" in text:
        return "RIR/room response — verificar convención"
    if "Data.IR" in shapes and ("hrir" in text or "freefield" in text or "free field" in text):
        return "probable HRIR/free-field — confirmar semántica"
    if "Data.IR" in shapes:
        return "Data.IR presente — familia no concluyente"
    return "otra/indeterminada — inspección requerida"

def inspect(path: Path, root: Path) -> dict:
    record = {
        "path": path.relative_to(root).as_posix(),
        "bytes": path.stat().st_size,
        "sha256": sha256_file(path),
        "readable_hdf5": False,
        "classification": "no clasificada",
        "global_attributes": {},
        "group_attributes": {},
        "datasets": {},
        "errors": [],
    }
    try:
        with h5py.File(path, "r") as f:
            record["readable_hdf5"] = True
            record["global_attributes"] = {
                key: plain(f.attrs[key]) for key in ATTRS if key in f.attrs
            }
            def visitor(name, obj):
                if isinstance(obj, h5py.Group) and name and obj.attrs:
                    record["group_attributes"][name] = {
                        str(k): plain(v) for k, v in obj.attrs.items()
                    }
                if isinstance(obj, h5py.Dataset):
                    # Indexar todos los datasets y sus atributos, incluidos metadatos específicos de cada corpus.
                    attrs = {str(k): plain(v) for k, v in obj.attrs.items()}
                    item = {
                        "shape": list(obj.shape),
                        "dtype": str(obj.dtype),
                        "attrs": attrs,
                    }
                    # Leer solo arrays pequeños de metadatos; nunca cargar Data.IR.
                    if name in DATASETS and name != "Data.IR" and obj.size <= 20000:
                        item["values"] = plain(obj[()])
                    if name == "Data.IR" and len(obj.shape) >= 2:
                        item["measurements"] = int(obj.shape[0])
                        item["receiver_count"] = int(obj.shape[-2])
                        item["ir_sample_count"] = int(obj.shape[-1])
                    if name in ("SourcePosition", "ReceiverPosition") and obj.size:
                        try:
                            arr = np.asarray(obj[()], dtype=float)
                            cols = int(arr.shape[-1]) if arr.ndim else 1
                            coords = arr.reshape((-1, cols))
                            item["point_count"] = int(coords.shape[0])
                            item["unique_point_count"] = int(np.unique(coords, axis=0).shape[0])
                            item["coordinate_min"] = coords.min(axis=0).tolist()
                            item["coordinate_max"] = coords.max(axis=0).tolist()
                            typ = str(attrs.get("Type", "")).lower()
                            units = str(attrs.get("Units", "")).lower()
                            if "cartesian" in typ and ("metre" in units or "meter" in units) and cols == 3:
                                radii = np.linalg.norm(coords, axis=1)
                                item["radius_m_min"] = float(radii.min())
                                item["radius_m_max"] = float(radii.max())
                        except (TypeError, ValueError, OverflowError) as exc:
                            item["summary_error"] = str(exc)
                    record["datasets"][name] = item
            f.visititems(visitor)
            record["classification"] = classify(path, record["global_attributes"], record["datasets"])
    except Exception as exc:
        record["errors"].append(f"{type(exc).__name__}: {exc}")
    return record

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--root", type=Path, default=Path("."))
    ap.add_argument("--output-dir", type=Path, default=Path("build/sofa-inventory"))
    ap.add_argument("--expected-count", type=int, default=0)
    args = ap.parse_args()
    root = args.root.resolve()
    files = sorted(p for p in root.rglob("*") if p.is_file() and p.suffix.lower() == ".sofa")
    records = [inspect(p, root) for p in files]
    unreadable = [r for r in records if not r["readable_hdf5"]]
    classes = {}
    for r in records:
        classes[r["classification"]] = classes.get(r["classification"], 0) + 1
    summary = {
        "schema_version": 1,
        "scope": "todos los archivos .sofa bajo la raíz indicada; solo metadatos y hashes",
        "root": ".",
        "files_found": len(records),
        "readable_hdf5": len(records) - len(unreadable),
        "unreadable_hdf5": len(unreadable),
        "expected_count": args.expected_count or None,
        "expected_count_matches": (len(records) == args.expected_count) if args.expected_count else None,
        "classification_counts": dict(sorted(classes.items())),
        "limitations": [
            "La clasificación por nombre/metadatos es heurística y no sustituye revisión científica.",
            "Un HDF5 legible no demuestra conformidad semántica completa con AES69/SOFA.",
            "No se remuestrea, reorienta, alinea ni normaliza audio en este inventario.",
            "Los campos ausentes se registran como ausentes; no se inventan valores.",
            "El hash identifica exactamente el archivo auditado y permite detectar cambios posteriores.",
        ],
        "files": records,
    }
    args.output_dir.mkdir(parents=True, exist_ok=True)
    (args.output_dir / "sofa_metadata_inventory.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    (args.output_dir / "sofa_metadata_summary.json").write_text(
        json.dumps({k: v for k, v in summary.items() if k != "files"}, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    with (args.output_dir / "sofa_metadata_inventory.csv").open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=[
            "path", "bytes", "sha256", "readable_hdf5", "classification",
            "SOFAConventions", "SOFAConventionsVersion", "DataType", "RoomType",
            "ListenerShortName", "DatabaseName", "License", "Data.IR_shape",
            "Data.SamplingRate_shape", "SourcePosition_shape", "ReceiverPosition_shape", "errors",
        ])
        writer.writeheader()
        for r in records:
            a, d = r["global_attributes"], r["datasets"]
            row = {k: json.dumps(a.get(k), ensure_ascii=False) if k in a else "" for k in
                   ("SOFAConventions", "SOFAConventionsVersion", "DataType", "RoomType",
                    "ListenerShortName", "DatabaseName", "License")}
            row.update({
                "path": r["path"], "bytes": r["bytes"], "sha256": r["sha256"],
                "readable_hdf5": r["readable_hdf5"], "classification": r["classification"],
                "Data.IR_shape": json.dumps(d.get("Data.IR", {}).get("shape")),
                "Data.SamplingRate_shape": json.dumps(d.get("Data.SamplingRate", {}).get("shape")),
                "SourcePosition_shape": json.dumps(d.get("SourcePosition", {}).get("shape")),
                "ReceiverPosition_shape": json.dumps(d.get("ReceiverPosition", {}).get("shape")),
                "errors": "; ".join(r["errors"]),
            })
            writer.writerow(row)
    print(json.dumps({k: v for k, v in summary.items() if k != "files"}, ensure_ascii=False, indent=2))
    if unreadable:
        print("Archivos ilegibles:", *(r["path"] for r in unreadable), sep="\n- ", file=sys.stderr)
    if args.expected_count and len(records) != args.expected_count:
        print(f"ERROR: se esperaban {args.expected_count} archivos y se encontraron {len(records)}", file=sys.stderr)
        return 1
    return 1 if unreadable else 0

if __name__ == "__main__":
    raise SystemExit(main())
