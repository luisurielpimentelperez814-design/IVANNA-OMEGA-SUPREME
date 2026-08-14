#!/usr/bin/env python3
"""Deriva la base PCA V (K=7) del dataset IHR1 para el morph SAF exacto.

HRIR_personal(az) = HRIR_dataset(az) + V · q   (q = vector latente SAF)

Entrada: hrtf_dataset.ihr1  [magic IHR1][numDirs][irLen][sr] + por dirección
         [azimuth f32][L irLen f32][R irLen f32]
Salida : pca_basis_V.bin
         [4B "PCAV"][i32 K][i32 irLen] + por componente: [irLen f32 L][irLen f32 R]
"""
import argparse, struct, sys
import numpy as np

ap = argparse.ArgumentParser()
ap.add_argument('ihr1')
ap.add_argument('-o', '--out', required=True)
ap.add_argument('-k', '--components', type=int, default=7)
a = ap.parse_args()

raw = open(a.ihr1, 'rb').read()
assert raw[:4] == b'IHR1', 'no es IHR1'
numDirs, irLen, sr = struct.unpack_from('<iii', raw, 4)
off = 16
X = np.zeros((numDirs, 2 * irLen), dtype=np.float64)
for d in range(numDirs):
    (az,) = struct.unpack_from('<f', raw, off); off += 4
    L = np.frombuffer(raw, dtype='<f4', count=irLen, offset=off); off += 4 * irLen
    R = np.frombuffer(raw, dtype='<f4', count=irLen, offset=off); off += 4 * irLen
    X[d, :irLen] = L
    X[d, irLen:] = R

Xc = X - X.mean(axis=0, keepdims=True)
_, S, Vt = np.linalg.svd(Xc, full_matrices=False)
V = Vt[:a.components]                       # K × 2·irLen
explained = (S[:a.components] ** 2).sum() / (S ** 2).sum()

with open(a.out, 'wb') as f:
    f.write(b'PCAV')
    f.write(struct.pack('<i', a.components))
    f.write(struct.pack('<i', irLen))
    for k in range(a.components):
        f.write(V[k, :irLen].astype('<f4').tobytes())
        f.write(V[k, irLen:].astype('<f4').tobytes())
print(f'OK {a.out}: K={a.components} irLen={irLen} dirs={numDirs} '
      f'varianza_explicada={100*explained:.1f}% sr={sr}')
