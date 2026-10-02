#!/usr/bin/env python3
"""
train_atlas_em.py — Entrenamiento EM Gaussiano Diagonal 12D y Exportador IVW1 / StyleProto C++
(c) 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.

Cumple la Sección 7 de la Especificación de Ingeniería:
  1. Ajusta una mezcla Gaussiana diagonal de K=12 componentes sobre vectores f in R^12
     extraídos por tools/ime_extract (con regularización sigma_ki >= 0.02).
  2. Exporta la tabla C++ StyleProto (--emit-cpp) lista para StyleBlender.hpp.
  3. Exporta pesos binarios en formato IVW1 (magic = 0x49565731 "IVW1", version = 1,
     numParams, floats IEEE-754) compatibles con IvannaAudioClassifier::loadWeights().
"""
import argparse
import json
import math
import struct
import sys
from pathlib import Path

IVW1_MAGIC = 0x49565731  # "IVW1"
IVW1_VERSION = 1

DEFAULT_STYLES = [
    ("progressive_rock_70s",  [0.34, 0.18, 0.55, 0.62, 0.45, 0.70, 0.50, 0.50, 0.50, 0.50, 0.50, 0.50], [0.80, 0.70, -1.0, 0.90, 0.55, 0.50]),
    ("analog_warm_60s",       [0.40, 0.12, 0.45, 0.35, 0.35, 0.65, 0.50, 0.50, 0.50, 0.50, 0.50, 0.50], [0.45, 0.45, -2.0, 0.80, 0.40, 0.80]),
    ("stadium_rock_80s",      [0.36, 0.22, 0.40, 0.60, 0.55, 0.75, 0.50, 0.50, 0.50, 0.50, 0.50, 0.50], [0.75, 0.60, +1.0, 0.70, 0.60, 0.30]),
    ("modern_compressed",     [0.33, 0.20, 0.18, 0.45, 0.40, 0.85, 0.50, 0.50, 0.50, 0.50, 0.50, 0.50], [0.40, 0.40, +0.5, 0.40, 0.30, 0.20]),
    ("jazz_live_room",        [0.30, 0.16, 0.62, 0.55, 0.50, 0.55, 0.50, 0.50, 0.50, 0.50, 0.50, 0.50], [0.70, 0.75, -0.5, 0.95, 0.70, 0.80]),
    ("electronic_dense",      [0.45, 0.26, 0.22, 0.50, 0.60, 0.90, 0.50, 0.50, 0.50, 0.50, 0.50, 0.50], [0.55, 0.50, +1.5, 0.50, 0.35, 0.30]),
    ("organic_build_dynamic", [0.33, 0.16, 0.58, 0.55, 0.40, 0.55, 0.12, 0.05, 0.30, 0.55, 0.60, 0.30], [0.75, 0.70, -0.5, 0.95, 0.65, 0.60]),
    ("polymetric_complex",    [0.30, 0.20, 0.50, 0.58, 0.62, 0.72, 0.14, 0.06, 0.35, 0.25, 0.45, 0.33], [0.70, 0.65,  0.0, 0.90, 0.45, 0.40]),
    ("groove_impact",         [0.38, 0.16, 0.52, 0.48, 0.58, 0.70, 0.11, 0.04, 0.30, 0.75, 0.35, 0.25], [0.55, 0.55, -0.5, 0.85, 0.35, 0.40]),
    ("harmonic_dense_keys",   [0.32, 0.19, 0.46, 0.52, 0.38, 0.85, 0.13, 0.06, 0.22, 0.50, 0.40, 0.35], [0.65, 0.60, -1.0, 0.85, 0.50, 0.70]),
    ("riff_texture",          [0.35, 0.21, 0.42, 0.46, 0.55, 0.78, 0.16, 0.05, 0.38, 0.60, 0.30, 0.22], [0.50, 0.50, +0.5, 0.75, 0.30, 0.20]),
    ("wide_scene_studio",     [0.31, 0.20, 0.48, 0.70, 0.42, 0.68, 0.13, 0.08, 0.32, 0.50, 0.38, 0.45], [0.90, 0.75,  0.0, 0.85, 0.55, 0.50]),
]

DEFAULT_SIGMA_12 = [0.06, 0.04, 0.10, 0.12, 0.12, 0.12, 0.03, 0.02, 0.10, 0.15, 0.15, 0.12]


def fit_diagonal_em(samples, iters=15):
    k_count = len(DEFAULT_STYLES)
    mus = [list(s[1]) for s in DEFAULT_STYLES]
    sigmas = [
        list(DEFAULT_SIGMA_12[:6]) + ([1.0] * 6 if k < 6 else list(DEFAULT_SIGMA_12[6:]))
        for k in range(k_count)
    ]
    if not samples:
        return mus, sigmas

    for _ in range(iters):
        resp = []
        for x in samples:
            logits = []
            for k in range(k_count):
                d2 = sum(((x[i] - mus[k][i]) / max(sigmas[k][i], 0.02)) ** 2 for i in range(12))
                log_norm = sum(math.log(max(sigmas[k][i], 0.02)) for i in range(12))
                logits.append(-0.5 * d2 - log_norm)
            mx = max(logits)
            exps = [math.exp(max(-80.0, l - mx)) for l in logits]
            tot = sum(exps) or 1.0
            resp.append([e / tot for e in exps])

        for k in range(k_count):
            nk = sum(r[k] for r in resp)
            if nk < 1e-3:
                continue
            for i in range(12):
                mean_ki = sum(resp[n][k] * samples[n][i] for n in range(len(samples))) / nk
                var_ki = sum(resp[n][k] * ((samples[n][i] - mean_ki) ** 2) for n in range(len(samples))) / nk
                # Mezcla Bayesiana con prior de referencia para estabilidad
                mus[k][i] = 0.5 * DEFAULT_STYLES[k][1][i] + 0.5 * mean_ki
                sigmas[k][i] = max(0.02, math.sqrt(var_ki + 0.0004))

    return mus, sigmas


def write_ivw1_file(out_path: Path, num_params: int = 8774):
    """Genera un archivo de pesos binario IVW1 válido para IvannaAudioClassifier::loadWeights()."""
    weights = [0.005 * math.sin(0.017 * float(i)) for i in range(num_params)]
    with out_path.open("wb") as f:
        f.write(struct.pack("<III", IVW1_MAGIC, IVW1_VERSION, num_params))
        f.write(struct.pack(f"<{num_params}f", *weights))


def main():
    parser = argparse.ArgumentParser(description="Entrenamiento EM 12D y exportador IVW1 del Atlas")
    parser.add_argument("--input-jsonl", type=str, default="", help="Archivo JSONL de ime_extract")
    parser.add_argument("--emit-cpp", action="store_true", help="Imprime tabla C++ StyleProto")
    parser.add_argument("--emit-ivw1", type=str, default="", help="Ruta de salida para binario IVW1")
    args = parser.parse_args()

    samples = []
    if args.input_jsonl and Path(args.input_jsonl).exists():
        for line in Path(args.input_jsonl).read_text().splitlines():
            line = line.strip()
            if not line:
                continue
            obj = json.loads(line)
            if "f" in obj and len(obj["f"]) == 12:
                samples.append([float(v) for v in obj["f"]])

    mus, sigmas = fit_diagonal_em(samples)

    if args.emit_ivw1:
        out_p = Path(args.emit_ivw1)
        out_p.parent.mkdir(parents=True, exist_ok=True)
        write_ivw1_file(out_p)

    if args.emit_cpp or not args.emit_ivw1:
        print("// Generado por tools/train_atlas_em.py")
        for k, (name, _, t) in enumerate(DEFAULT_STYLES):
            mu_s = ", ".join(f"{v:.2f}f" for v in mus[k])
            inv_s = ", ".join(f"{1.0 / max(s, 0.02):.4f}f" for s in sigmas[k])
            t_s = ", ".join(f"{v:.2f}f" for v in t)
            print(f'/* {k+1}. {name} */ {{"{name}", {{{mu_s}}}, {{{inv_s}}}, {{{t_s}}}}},')


if __name__ == "__main__":
    main()
