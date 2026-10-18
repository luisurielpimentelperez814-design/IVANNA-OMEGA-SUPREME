"""Command-line runner: python -m deh6_optics.cli --config ... --out ..."""
from __future__ import annotations
import argparse, json, hashlib, platform, sys
from pathlib import Path
import numpy as np
from .solver import gerchberg_saxton, reconstruction_metrics


def make_target(cfg: dict) -> np.ndarray:
    n = int(cfg.get("grid_size", 128))
    if n < 16 or n > 1024:
        raise ValueError("grid_size must be in [16,1024]")
    target = np.zeros((n, n), dtype=np.float64)
    for shape in cfg.get("targets", [{"type":"disk", "x":0.0, "y":0.0, "radius":0.22, "amplitude":1.0}]):
        amp = float(shape.get("amplitude", 1.0))
        x = np.linspace(-0.5, 0.5, n, endpoint=False)
        xx, yy = np.meshgrid(x, x)
        typ = shape.get("type", "disk")
        cx, cy = float(shape.get("x", 0)), float(shape.get("y", 0))
        if typ == "disk":
            mask = (xx-cx)**2 + (yy-cy)**2 <= float(shape.get("radius", 0.1))**2
        elif typ == "rectangle":
            mask = (np.abs(xx-cx) <= float(shape.get("width", 0.1))/2) & (np.abs(yy-cy) <= float(shape.get("height", 0.1))/2)
        else:
            raise ValueError(f"unsupported target type: {typ}")
        target[mask] = np.maximum(target[mask], amp)
    if target.max() <= 0:
        raise ValueError("target definition produced an empty target")
    return target


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--config", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args(argv)
    cfg_path = Path(args.config)
    cfg = json.loads(cfg_path.read_text(encoding="utf-8"))
    out = Path(args.out); out.mkdir(parents=True, exist_ok=True)
    target = make_target(cfg)
    phase, field, errors = gerchberg_saxton(target, float(cfg["wavelength_m"]), float(cfg["pixel_pitch_m"]),
        float(cfg["propagation_distance_m"]), iterations=int(cfg.get("iterations", 100)), seed=int(cfg.get("seed", 7)))
    metrics = reconstruction_metrics(target, field)
    metrics.update({"iterations": len(errors), "initial_error": errors[0], "last_constraint_error": errors[-1],
                    "wavelength_m": float(cfg["wavelength_m"]), "pixel_pitch_m": float(cfg["pixel_pitch_m"]),
                    "propagation_distance_m": float(cfg["propagation_distance_m"]), "grid_size": int(target.shape[0])})
    np.save(out/"target.npy", target); np.save(out/"reconstruction.npy", np.abs(field)); np.save(out/"phase.npy", phase)
    (out/"metrics.json").write_text(json.dumps(metrics, indent=2)+"\n", encoding="utf-8")
    digest = hashlib.sha256(cfg_path.read_bytes()).hexdigest()
    manifest = {"prototype":"IVANNA-DEH6-optics-0.1", "config_sha256":digest, "python":sys.version,
                "platform":platform.platform(), "numpy":np.__version__, "config":cfg}
    (out/"run_manifest.json").write_text(json.dumps(manifest, indent=2)+"\n", encoding="utf-8")
    print(json.dumps(metrics, indent=2))
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
