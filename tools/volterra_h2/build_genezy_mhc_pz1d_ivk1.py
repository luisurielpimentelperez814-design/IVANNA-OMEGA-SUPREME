#!/usr/bin/env python3
"""
build_genezy_mhc_pz1d_ivk1.py — Generador del paquete IVK1 (repo) para el
kernel Volterra H1/H2 del Sony Genezi MHC-PZ1D.

Fuente de los coeficientes:
  ~/workspace/user/files/Sony_MHC-PZ1D_Volterra_Kernel_0_z5nb.pdf
  (caracterización ESS Farina; tablas h1[64], h2 diagonal y sub-diagonal).
  Los valores de abajo son COPIA EXACTA del PDF (verificados contra el texto
  extraído del PDF; la página 3 del PDF es una figura, no contiene coeficientes).

Formato IVK1: el DEL REPO (app/src/main/cpp/neuromorphic/
volterra_kernel_package.hpp y tools/volterra_h2/kernel_loader.py::
build_payload), NO el del PDF (ese define otra cabecera, h2 como matriz
completa 64x64 y CRC al final; es incompatible y NO se usa).

  off  0: magic "IVK1" (4B)
  off  4: format_version u16 = 1
  off  6: kernel_length u16 = 64
  off  8: sample_rate u32 = 48000
  off 12: crc32 u32 (CRC-32 IEEE sobre bytes [24, fin) = device_id + h1 + h2)
  off 16: dataset_id 8B ASCII NUL ("GENEZY01")
  off 24: device_id 16B ASCII NUL ("MOTO_G85_GENEZY")
  off 40: h1[64] float32
  luego: h2 triangular superior por filas, K(K+1)/2 = 2080 coef float32,
         índice h2Index(k,l,K) = k*K + l - k(k+1)/2
  Tamaño total exacto = 40 + 4*64 + 4*2080 = 8616 bytes.

NORMALIZACIÓN DE h1 (decisión documentada, NO negociable):
  El h1 tal cual tiene sum|h1| (L1) = 4.391953 y ganancia DC = 3.070853
  (~9.75 dB). Eso RECHAZA la puerta de producción (max_h1_l1 = 1.000001,
  UnstableH1) y también el LoadGate BIBO del propio PDF
  ((sum_h1+sum_h2)*scale >= 1.0). Por eso se normaliza h1 dividiendo por
  su norma L1 = 4.391953. Así sum|h1_norm| = 1.0 (pasa la puerta) y se
  preserva la FORMA espectral; la ganancia DC queda en 0.699 (-3.1 dB).
  Los límites de la puerta NO se tocan. La h2 NO se normaliza (ya pasa:
  sum|h2| simétrica = 0.172590 <= presupuesto 0.25; energía triangular
  0.001934 <= 64.0; peor caso 1.172590 <= 1.5).

Expansión de h2 a triangular superior (solo 32 coeficientes no nulos):
  - diagonal (k==l): diag[k] para k=0..15, resto 0.
  - super-diagonal (l==k+1): sub[k] para k=0..15, donde sub[k] es el valor
    de la sub-diagonal (k+1,k) del PDF; por simetría h2[k,k+1] = h2[k+1,k].
  - resto 0. La matriz completa 64x64 es simétrica.

Uso:
  python3 tools/volterra_h2/build_genezy_mhc_pz1d_ivk1.py [--out RUTA]
  Por defecto escribe app/src/main/assets/kernels/genezy_mhc_pz1d.ivk1
  (asset del APK; el cargador Kotlin lo lee y lo pasa al JNI).
  Imprime tamaño, SHA-256 y los números de puerta. Devuelve 0 solo si
  el paquete pasa la puerta Python (load_kernel) con la política de
  producción (GENEZY01 / MOTO_G85_GENEZY).
"""
from __future__ import annotations

import argparse
import hashlib
import struct
import sys
from pathlib import Path

# El loader del repo vive en este mismo directorio.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from kernel_loader import build_payload, load_kernel  # noqa: E402

K = 64
FS = 48000
DATASET_ID = b"GENEZY01"
DEVICE_ID = "MOTO_G85_GENEZY"

# h1[64] lineal, COPIA EXACTA del PDF (tabla h_1[n], float32).
H1 = [
    0.001248, 0.002845, 0.005120, 0.009410, 0.016250, 0.026840,
    0.042150, 0.063890, 0.093410, 0.132150, 0.181240, 0.241580,
    0.312450, 0.392100, 0.478940, 0.568410, 0.684210, 0.215430,
    -0.104320, -0.185410, -0.142100, -0.078450, -0.021400, 0.018500,
    0.041200, 0.048900, 0.045600, 0.036800, 0.025400, 0.013800,
    0.003400, -0.005120, -0.045120, -0.031250, -0.020140, -0.012850,
    -0.007920, -0.004210, -0.001850, -0.000410, 0.000480, 0.000950,
    0.001240, 0.001890, 0.002410, 0.002850, 0.003120, 0.003340,
    0.003410, 0.002890, 0.002150, 0.001620, 0.001180, 0.000840,
    0.000590, 0.000410, 0.000280, 0.000190, 0.000120, 0.000080,
    0.000050, 0.000030, 0.000010, 0.000000,
]

# h2 diagonal h2[n,n], n=0..15 (COPIA EXACTA del PDF).
H2_DIAG = [
    -0.000412, -0.000854, -0.001840, -0.003920,
    -0.007850, -0.013410, -0.019850, -0.022410,
    -0.018940, -0.014210, -0.009850, -0.006240,
    -0.003510, -0.001840, -0.000890, -0.000350,
]

# h2 sub-diagonal h2[n,n-1], n=1..16; índice 0 = (1,0) (COPIA EXACTA del PDF).
H2_SUB = [
    0.000125, 0.000341, 0.000682, 0.001124,
    0.001890, 0.002840, 0.003910, 0.004120,
    0.003250, 0.002140, 0.001280, 0.000740,
    0.000390, 0.000180, 0.000075, 0.000020,
]


def f32(v: float) -> float:
    """Redondea a float32 (lo que realmente se escribe y valida la puerta)."""
    return struct.unpack("<f", struct.pack("<f", v))[0]


def normalize_h1(h1):
    """Divide por la norma L1 (doble precisión) y devuelve (h1_norm_f32, L1, DC)."""
    l1 = sum(abs(v) for v in h1)
    dc = sum(h1)
    h1n = [f32(v / l1) for v in h1]
    return h1n, l1, dc


def expand_h2_triangular(diag, sub, k=K):
    """Expande diagonal+sub-diagonal a triangular superior por filas.

    tri[h2Index(i, j, K)] con j >= i:
      j == i    -> diag[i] si i < 16, si no 0
      j == i+1  -> sub[i]  si i < 16, si no 0   (simetría: h2[i,i+1]=h2[i+1,i])
      resto     -> 0
    Devuelve la lista de K(K+1)/2 float32.
    """
    tri = [0.0] * (k * (k + 1) // 2)
    for i in range(k):
        for j in range(i, k):
            idx = i * k + j - (i * (i + 1)) // 2
            if j == i:
                v = diag[i] if i < len(diag) else 0.0
            elif j == i + 1:
                v = sub[i] if i < len(sub) else 0.0
            else:
                v = 0.0
            tri[idx] = f32(v)
    return tri


def gate_numbers(h1n, h2tri):
    """Números que comprueban ambas puertas (C++ y Python usan los mismos)."""
    l1 = sum(abs(v) for v in h1n)
    h2_abs_full = 0.0  # sum|h2| de la matriz completa simétrica (off-diag x2)
    h2_energy = 0.0    # sum v^2 sobre el triangular
    for i in range(K):
        for j in range(i, K):
            idx = i * K + j - (i * (i + 1)) // 2
            v = h2tri[idx]
            a = abs(v)
            h2_abs_full += (1.0 if j == i else 2.0) * a
            h2_energy += v * v
    worst = l1 + h2_abs_full  # A=1: A*l1 + A^2*sum|h2|
    return {
        "l1_h1": l1,
        "h2_abs_full": h2_abs_full,
        "h2_energy_tri": h2_energy,
        "worst_case": worst,
        "max_abs": max(max(abs(v) for v in h1n), max(abs(v) for v in h2tri)),
        "nonzero_h2": sum(1 for v in h2tri if v != 0.0),
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    repo = Path(__file__).resolve().parent.parent.parent
    default_out = repo / "app/src/main/assets/kernels/genezy_mhc_pz1d.ivk1"
    ap.add_argument("--out", type=Path, default=default_out,
                    help="ruta de salida del .ivk1")
    args = ap.parse_args()

    assert len(H1) == K and len(H2_DIAG) == 16 and len(H2_SUB) == 16

    h1n, l1, dc = normalize_h1(H1)
    h2tri = expand_h2_triangular(H2_DIAG, H2_SUB)
    nums = gate_numbers(h1n, h2tri)

    print(f"[genezy] h1 L1 original (doble) = {l1:.6f}  (esperado 4.391953)")
    print(f"[genezy] h1 DC original        = {dc:.6f}  (esperado 3.070853)")
    print(f"[genezy] h1 DC normalizada     = {sum(h1n):.6f}  (esperado 0.699)")
    print(f"[genezy] sum|h1_norm| float32  = {nums['l1_h1']:.9f}  (límite 1.000001)")
    print(f"[genezy] sum|h2| simétrica     = {nums['h2_abs_full']:.6f}  (presupuesto 0.25)")
    print(f"[genezy] h2 energía triangular = {nums['h2_energy_tri']:.6f}  (límite 64.0)")
    print(f"[genezy] peor caso (A=1)       = {nums['worst_case']:.6f}  (límite 1.5)")
    print(f"[genezy] max|coef|             = {nums['max_abs']:.6f}  (límite 8.0)")
    print(f"[genezy] coef h2 no nulos      = {nums['nonzero_h2']}  (esperado 32)")
    if nums["l1_h1"] > 1.0 + 1e-6:
        print("[genezy] ERROR: h1 normalizada no pasa max_h1_l1")
        return 1
    if nums["h2_abs_full"] > 0.25 or nums["worst_case"] > 1.5:
        print("[genezy] ERROR: h2 fuera de presupuesto")
        return 1

    payload = build_payload(K, FS, DEVICE_ID, h1n, h2tri, dataset_id=DATASET_ID)
    expected = 40 + 4 * K + 4 * (K * (K + 1) // 2)
    assert len(payload) == expected == 8616, f"tamaño {len(payload)} != 8616"

    # Puerta Python (misma política de producción que la C++).
    loaded = load_kernel(payload, K, FS, DEVICE_ID,
                         allowed_dataset_ids=("GENEZY01",))
    if loaded is None:
        print("[genezy] ERROR: la puerta Python rechazó el paquete")
        return 1
    assert loaded.kernel_length == K and loaded.sample_rate == FS
    assert loaded.device_id == DEVICE_ID
    assert loaded.h1 == h1n and loaded.h2_triangular == h2tri

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(payload)
    sha = hashlib.sha256(payload).hexdigest()
    print(f"[genezy] escrito {args.out} ({len(payload)} bytes)")
    print(f"[genezy] SHA-256 {sha}")
    print("[genezy] OK: paquete aceptado por la puerta Python (producción)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
