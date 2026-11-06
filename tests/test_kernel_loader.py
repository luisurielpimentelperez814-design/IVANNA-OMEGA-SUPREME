#!/usr/bin/env python3
"""
test_kernel_loader.py — Pruebas positivas y negativas de la puerta de carga
de kernels Volterra H1/H2 (Fase VI, items 6, 7, 8, 9, 10).

Los sistemas Volterra sintéticos usados aquí se generan matemáticamente,
están etiquetados como sintéticos y validan SOLO el algoritmo/puerta:
nunca calibran físicamente el Genezy.
"""
import struct
import sys
import unittest
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "volterra_h2"))

import kernel_loader as KL  # noqa: E402

DEV = "MOTO_G85_GENEZY"
K = 8
FS = 48000


def good_payload():
    # h1 con l1 <= 1 (estable segun la propia puerta): 0.8 + 0.2
    h1 = [0.8] + [0.0] * (K - 1)
    h1[1] = 0.2
    tri = K * (K + 1) // 2
    h2 = [0.0] * tri
    h2[0] = 0.05
    return KL.build_payload(K, FS, DEV, h1, h2)


class KernelLoaderTests(unittest.TestCase):
    def test_accepts_wellformed_kernel_for_authorized_device(self):
        k = KL.load_kernel(good_payload(), K, FS, DEV)
        self.assertIsNotNone(k)
        self.assertEqual(k.kernel_length, K)
        self.assertEqual(k.sample_rate, FS)
        self.assertEqual(k.device_id, DEV)
        self.assertAlmostEqual(k.h1[1], 0.2)
        self.assertAlmostEqual(k.h2_triangular[0], 0.05)

    def test_rejects_corrupted_payload(self):
        p = bytearray(good_payload())
        p[60] ^= 0xFF  # flip a coefficient byte
        self.assertIsNone(KL.load_kernel(bytes(p), K, FS, DEV))

    def test_rejects_foreign_device(self):
        other = KL.build_payload(K, FS, "OTHER_DEVICE_999999", [1.0] + [0.0] * (K - 1),
                                 [0.0] * (K * (K + 1) // 2))
        self.assertIsNone(KL.load_kernel(other, K, FS, DEV))

    def test_rejects_dimension_mismatch(self):
        self.assertIsNone(KL.load_kernel(good_payload(), K + 1, FS, DEV))

    def test_rejects_sample_rate_mismatch(self):
        self.assertIsNone(KL.load_kernel(good_payload(), K, 96000, DEV))

    def test_rejects_nan_and_inf(self):
        tri = K * (K + 1) // 2
        for bad in (float("nan"), float("inf")):
            h1 = [1.0] + [0.0] * (K - 1)
            h1[2] = bad
            p = KL.build_payload(K, FS, DEV, h1, [0.0] * tri)
            self.assertIsNone(KL.load_kernel(p, K, FS, DEV))

    def test_rejects_unstable_h1(self):
        tri = K * (K + 1) // 2
        h1 = [0.9] * K  # l1 = 7.2 > 1
        p = KL.build_payload(K, FS, DEV, h1, [0.0] * tri)
        self.assertIsNone(KL.load_kernel(p, K, FS, DEV))

    def test_rejects_out_of_range_amplitude(self):
        tri = K * (K + 1) // 2
        h2 = [0.0] * tri
        h2[0] = 99.0  # > MAX_COEFF
        p = KL.build_payload(K, FS, DEV, [1.0] + [0.0] * (K - 1), h2)
        self.assertIsNone(KL.load_kernel(p, K, FS, DEV))

    def test_rejects_truncated_and_bad_magic(self):
        p = good_payload()
        self.assertIsNone(KL.load_kernel(p[:20], K, FS, DEV))
        self.assertIsNone(KL.load_kernel(b"XXXX" + p[4:], K, FS, DEV))


if __name__ == "__main__":
    unittest.main()
