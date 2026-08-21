#!/usr/bin/env python3
# sofa_to_ihr1.py — Conversor SOFA (AES69) → IHR1 para HRTFBinLoader.
#
# Formato IHR1 (leído de app/src/main/cpp/HRTFBinLoader.cpp, no asumido):
#   magic[4] = "IHR1"
#   uint32 numPositions  (little-endian — ARM/x86 ambos LE)
#   uint32 irLen         (512 taps fijo — ver FASE 1)
#   uint32 sampleRateHz  (48000 — el pipeline corre a 48kHz)
#   [float az_deg, float el_deg] × numPositions      (tabla angular)
#   [float32 L×irLen + float32 R×irLen] × numPositions
#
# Reglas DSP:
#   · Resample a 48 kHz con scipy.signal.resample_poly (fase preservada,
#     filtro anti-alias FIR — no destruye el impulso como una FFT truncada).
#   · Truncado/padding a 512 taps — las HRIR SOFA miden 200-512 taps; el
#     loader espera irLen constante por dataset.
#   · Normalización: ganancia unitaria en el pico del promedio L+R de TODAS
#     las posiciones (evita que una posición con grazing incidence infle
#     el resto) — headroom 0.89 para que el SafetyLimiter no trabaje.
#   · CIPIC/SOFA usan coordenadas esféricas interaural (az -180..+180,
#     el -90..+90) — se mantienen tal cual; el interpolador angular del
#     motor (spatial/HRTFInterpolator.hpp) las consume directamente.
import sys, os, json, hashlib, struct
import numpy as np

def load_sofa(path):
    """Lee SOFA vía h5py (SOFA = HDF5). Devuelve (DataIR [M,R,N], SourcePosition [M,3], sr_hz)."""
    import h5py
    with h5py.File(path, 'r') as f:
        data = np.array(f['Data.IR'])                       # [M, R, N]
        sr   = float(np.array(f['Data.SamplingRate']).flat[0])
        pos  = np.array(f['SourcePosition'])                # [M, 3]
        pos_type = f['SourcePosition'].attrs.get('Type', b'spherical')
        if isinstance(pos_type, bytes): pos_type = pos_type.decode()
    return data, pos, sr, pos_type

def to_ihr1(sofa_path, out_path, target_sr=48000, taps=512):
    from scipy.signal import resample_poly
    from math import gcd
    data, pos, sr, pos_type = load_sofa(sofa_path)
    M, R, N = data.shape
    if R < 2: raise RuntimeError(f"{sofa_path}: solo {R} canales, se requieren 2 (L/R)")
    # Resample por posición si el dataset no está a 48 kHz
    if abs(sr - target_sr) > 1:
        g = gcd(int(sr), target_sr)
        up, down = target_sr // g, int(sr) // g
        data = resample_poly(data, up, down, axis=2)
        N = data.shape[2]
    # Truncado / padding a taps fijos
    if N >= taps: data = data[:, :, :taps]
    else:
        pad = np.zeros((M, R, taps - N), dtype=data.dtype)
        data = np.concatenate([data, pad], axis=2)
    # Normalización global (pico del promedio absoluto L+R sobre todo el set)
    peak = np.max(np.abs(data[:, 0, :]) + np.abs(data[:, 1, :]))
    if peak > 0: data = data * (0.89 / peak)
    # Posiciones angulares
    if pos_type.startswith('spherical'):
        az = pos[:, 0].astype(np.float32)
        el = pos[:, 1].astype(np.float32)
    else:  # cartesian → esférico
        x, y, z = pos[:, 0], pos[:, 1], pos[:, 2]
        az = np.degrees(np.arctan2(y, x)).astype(np.float32)
        el = np.degrees(np.arcsin(np.clip(z / np.maximum(np.linalg.norm(pos, axis=1), 1e-9), -1, 1))).astype(np.float32)
    # Escribir IHR1 (little-endian explícito)
    with open(out_path, 'wb') as f:
        f.write(b'IHR1')
        f.write(struct.pack('<III', M, taps, target_sr))
        for i in range(M):
            f.write(struct.pack('<ff', float(az[i]), float(el[i])))
        data32 = data.astype('<f4')
        for i in range(M):
            f.write(data32[i, 0, :].tobytes())   # L
            f.write(data32[i, 1, :].tobytes())   # R
    return M, sr

DATASETS = [
    # (archivo_sofa, nombre_ihr1, sujeto_ui)
    ("MIT_KEMAR_normal_pinna.sofa",  "kemar.ihr1",          "KEMAR — pinna normal (MIT 1994)"),
    ("MIT_KEMAR_large_pinna.sofa",   "kemar_large.ihr1",    "KEMAR — pinna grande (MIT 1994)"),
    ("TU-Berlin_QU_KEMAR_anechoic_radius_0.5m.sofa", "tu_berlin_kemar.ihr1", "TU Berlin QU KEMAR anecoico"),
    ("subject_003.sofa", "cipic_003.ihr1", "CIPIC sujeto 003"),
    ("subject_008.sofa", "cipic_008.ihr1", "CIPIC sujeto 008"),
    ("subject_009.sofa", "cipic_009.ihr1", "CIPIC sujeto 009"),
    ("subject_010.sofa", "cipic_010.ihr1", "CIPIC sujeto 010"),
    ("subject_011.sofa", "cipic_011.ihr1", "CIPIC sujeto 011"),
    ("subject_012.sofa", "cipic_012.ihr1", "CIPIC sujeto 012"),
    ("subject_165.sofa", "cipic_165.ihr1", "CIPIC sujeto 165"),
    ("Pulse.sofa",       "pulse.ihr1",     "Pulse HRTF"),
]

if __name__ == "__main__":
    src_dir, dst_dir = sys.argv[1], sys.argv[2]
    os.makedirs(dst_dir, exist_ok=True)
    index = {"version": "2.0", "format": "IHR1", "sampleRate": 48000, "taps": 512, "subjects": []}
    for sofa, ihr1, label in DATASETS:
        sp = os.path.join(src_dir, sofa)
        if not os.path.exists(sp):
            print(f"SKIP {sofa} (no existe)"); continue
        op = os.path.join(dst_dir, ihr1)
        M, sr_orig = to_ihr1(sp, op)
        sha = hashlib.sha256(open(op, 'rb').read()).hexdigest()
        index["subjects"].append({"id": ihr1.replace(".ihr1",""), "file": ihr1,
                                  "label": label, "positions": M,
                                  "sourceSampleRate": sr_orig, "sha256": sha})
        print(f"OK  {sofa} → {ihr1}  ({M} pos, {sr_orig}→48000 Hz, sha256={sha[:12]}…)")
    with open(os.path.join(dst_dir, "hrtf_index.json"), "w") as f:
        json.dump(index, f, indent=2)
    print(f"INDEX hrtf_index.json — {len(index['subjects'])} sujetos")
