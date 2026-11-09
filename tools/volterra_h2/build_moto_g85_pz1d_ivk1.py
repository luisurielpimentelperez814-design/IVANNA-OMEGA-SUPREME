#!/usr/bin/env python3
"""
build_moto_g85_pz1d_ivk1.py — Generador del paquete IVK1 (repo) para el
kernel Volterra H1/H2 REFINADO de la cadena Moto G85 5G + DAC USB-C +
Sony Genezi MHC-PZ1D.

Fuente de los coeficientes:
  Caracterización electroacústica integral del Motorola Moto G85 5G
  (Snapdragon 6s Gen 3, SM6375-AC) + DAC USB-C (UAC2 asíncrono) + Sony
  Genezi MHC-PZ1D, compartida por Esse el 2026-11-08. Es la REFINACIÓN
  del kernel Genezi anterior (build_genezy_mhc_pz1d_ivk1.py, desde el PDF
  ESS Farina): mismo dataset GENEZY01, misma familia de medición, pero
  h1 re-tabulada para la cadena completa (G85 + DAC + Genezi) y h2
  DIAGONAL SOLAMENTE (16 valores; el kernel Genezi anterior traía además
  sub-diagonal). El device_id cambia a "MOTO_G85_PZ1D" para distinguirlo.

  Los valores de abajo son COPIA EXACTA de la caracterización compartida.
  h1[64] y h2 diagonal[16] se verifican contra los números declarados
  (L1=5.00097, DC=3.76669, max|h1|=0.7142 tap 15, sum|h2|=0.144146).

Formato IVK1: el DEL REPO (app/src/main/cpp/neuromorphic/
volterra_kernel_package.hpp y tools/volterra_h2/kernel_loader.py::
build_payload), NO el del mensaje (ese define otra cabecera con
order/memory_depth/target_device/scale_factor/soft_ceiling, h2 como
matriz completa 64x64 y CRC al final; es incompatible y NO se usa).
El formato repo NO tiene campos scale_factor / soft_ceiling: la
seguridad fail-closed la da el saturador del motor
(VolterraH2Symmetric, rodilla 0.90 / techo 0.995) y el envelope; ver
docs/MOTO_G85_PZ1D_KERNEL.md. El scale 0.985 / soft ceiling -0.15 dBFS
del contenedor del mensaje NO se empaquetan.

  off  0: magic "IVK1" (4B)
  off  4: format_version u16 = 1
  off  6: kernel_length u16 = 64
  off  8: sample_rate u32 = 48000
  off 12: crc32 u32 (CRC-32 IEEE sobre bytes [24, fin) = device_id + h1 + h2)
  off 16: dataset_id 8B ASCII NUL ("GENEZY01")
  off 24: device_id 16B ASCII NUL ("MOTO_G85_PZ1D")
  off 40: h1[64] float32
  luego: h2 triangular superior por filas, K(K+1)/2 = 2080 coef float32,
         índice h2Index(k,l,K) = k*K + l - k(k+1)/2
  Tamaño total exacto = 40 + 4*64 + 4*2080 = 8616 bytes.

NORMALIZACIÓN DE h1 (decisión documentada, NO negociable):
  El h1 tal cual tiene sum|h1| (L1) = 5.00097 y ganancia DC = 3.76669.
  Eso RECHAZA la puerta de producción (max_h1_l1 = 1.000001,
  UnstableH1) y también la BIBO del propio mensaje
  ((sum_h1+sum_h2)*scale = (5.00097+0.144146)*0.985 = 5.067 >= 1.0).
  Por eso se normaliza h1 dividiendo por su norma L1 = 5.00097. Así
  sum|h1_norm| = 1.0 (pasa la puerta) y se preserva la FORMA espectral;
  la ganancia DC queda en 0.753 (-2.46 dB). Los límites de la puerta
  NO se tocan. La h2 NO se normaliza (ya pasa: sum|h2| = 0.144146 <=
  presupuesto 0.25; energía triangular 0.002425 <= 64.0; peor caso
  1.144146 <= 1.5).

Expansión de h2 a triangular superior (solo 16 coeficientes no nulos):
  - diagonal (k==l): diag[k] para k=0..15, resto 0.
  - resto 0 (NO hay sub-diagonal en esta especificación; el kernel
    Genezi anterior sí la traía). La matriz completa 64x64 es
    diagonal en sus primeros 16 taps.
  Esta dispersión es lo que hace viable el hot path disperso del motor
  optimizado: 16 MACs H2 por muestra en vez de 2080 (ver
  docs/MOTO_G85_PZ1D_KERNEL.md).

Uso:
  python3 tools/volterra_h2/build_moto_g85_pz1d_ivk1.py [--out RUTA]
  Por defecto escribe app/src/main/assets/kernels/moto_g85_pz1d.ivk1
  (asset del APK; el cargador Kotlin lo lee y lo pasa al JNI; es el
  kernel DEFAULT de Volterra en v2.5.369).
  Imprime tamaño, SHA-256 y los números de puerta. Devuelve 0 solo si
  el paquete pasa la puerta Python (load_kernel) con la política de
  producción (GENEZY01 / MOTO_G85_PZ1D).
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
DEVICE_ID = "MOTO_G85_PZ1D"

# h1[64] lineal, COPIA EXACTA de la caracterización G85 + DAC + Genezi.
H1 = [
    0.001420, 0.003120, 0.005890, 0.010410, 0.018250, 0.030140,
    0.048920, 0.075410, 0.112450, 0.162100, 0.228940, 0.314120,
    0.412500, 0.521400, 0.628900, 0.714200, 0.658410, 0.201240,
    -0.098520, -0.174210, -0.138500, -0.071240, -0.019840, 0.016250,
    0.038920, 0.045120, 0.041250, 0.032140, 0.021500, 0.011240,
    0.002850, -0.004210, -0.039820, -0.028940, -0.018520, -0.011240,
    -0.006850, -0.003520, -0.001420, -0.000310, 0.000380, 0.000820,
    0.001120, 0.001650, 0.002140, 0.002520, 0.002840, 0.003010,
    0.003120, 0.002650, 0.001980, 0.001450, 0.001020, 0.000710,
    0.000480, 0.000320, 0.000210, 0.000140, 0.000090, 0.000050,
    0.000030, 0.000010, 0.000000, 0.000000,
]

# h2 diagonal h2[n,n], n=0..15 (COPIA EXACTA). Resto de la matriz = 0.
H2_DIAG = [
    -0.000512, -0.000984, -0.002140, -0.004520,
    -0.008940, -0.015210, -0.022410, -0.025800,
    -0.021400, -0.016200, -0.011200, -0.007140,
    -0.004120, -0.002140, -0.001020, -0.000410,
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


def expand_h2_triangular(diag, k=K):
    """Expande la diagonal a triangular superior por filas.

    tri[h2Index(i, j, K)] con j >= i:
      j == i    -> diag[i] si i < 16, si no 0
      resto     -> 0  (sin sub-diagonal en esta especificación)
    Devuelve la lista de K(K+1)/2 float32.
    """
    tri = [0.0] * (k * (k + 1) // 2)
    for i in range(k):
        idx = i * k + i - (i * (i + 1)) // 2
        tri[idx] = f32(diag[i]) if i < len(diag) else 0.0
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
    default_out = repo / "app/src/main/assets/kernels/moto_g85_pz1d.ivk1"
    ap.add_argument("--out", type=Path, default=default_out,
                    help="ruta de salida del .ivk1")
    args = ap.parse_args()

    assert len(H1) == K and len(H2_DIAG) == 16

    h1n, l1, dc = normalize_h1(H1)
    h2tri = expand_h2_triangular(H2_DIAG)
    nums = gate_numbers(h1n, h2tri)

    print(f"[pz1d] h1 L1 original (doble) = {l1:.6f}  (esperado 5.000970)")
    print(f"[pz1d] h1 DC original        = {dc:.6f}  (esperado 3.766690)")
    print(f"[pz1d] h1 DC normalizada     = {sum(h1n):.6f}  (esperado 0.753)")
    print(f"[pz1d] sum|h1_norm| float32  = {nums['l1_h1']:.9f}  (límite 1.000001)")
    print(f"[pz1d] sum|h2| simétrica     = {nums['h2_abs_full']:.6f}  (presupuesto 0.25)")
    print(f"[pz1d] h2 energía triangular = {nums['h2_energy_tri']:.6f}  (límite 64.0)")
    print(f"[pz1d] peor caso (A=1)       = {nums['worst_case']:.6f}  (límite 1.5)")
    print(f"[pz1d] max|coef|             = {nums['max_abs']:.6f}  (límite 8.0)")
    print(f"[pz1d] coef h2 no nulos      = {nums['nonzero_h2']}  (esperado 16)")
    if nums["l1_h1"] > 1.0 + 1e-6:
        print("[pz1d] ERROR: h1 normalizada no pasa max_h1_l1")
        return 1
    if nums["h2_abs_full"] > 0.25 or nums["worst_case"] > 1.5:
        print("[pz1d] ERROR: h2 fuera de presupuesto")
        return 1

    payload = build_payload(K, FS, DEVICE_ID, h1n, h2tri, dataset_id=DATASET_ID)
    expected = 40 + 4 * K + 4 * (K * (K + 1) // 2)
    assert len(payload) == expected == 8616, f"tamaño {len(payload)} != 8616"

    # Puerta Python (misma política de producción que la C++).
    loaded = load_kernel(payload, K, FS, DEVICE_ID,
                         allowed_dataset_ids=("GENEZY01",))
    if loaded is None:
        print("[pz1d] ERROR: la puerta Python rechazó el paquete")
        return 1
    assert loaded.kernel_length == K and loaded.sample_rate == FS
    assert loaded.device_id == DEVICE_ID
    assert loaded.h1 == h1n and loaded.h2_triangular == h2tri

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(payload)
    sha = hashlib.sha256(payload).hexdigest()
    print(f"[pz1d] escrito {args.out} ({len(payload)} bytes)")
    print(f"[pz1d] SHA-256 {sha}")
    print("[pz1d] OK: paquete aceptado por la puerta Python (producción)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
