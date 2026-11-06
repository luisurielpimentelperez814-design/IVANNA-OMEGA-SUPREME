#!/usr/bin/env python3
"""Parity tests for Python H2 packing and the C++ row-major upper triangle."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "volterra_h2"))
from volterra_h2_identify import triangular_index_map, triangular_size, apply_volterra


class VolterraTriangularIndexTests(unittest.TestCase):
    @staticmethod
    def cxx_index(k: int, l: int, K: int) -> int:
        # Exact expression in volterra_h2_symmetric.cpp:
        # k*K + l - k*(k+1)/2
        return k * K + l - (k * (k + 1)) // 2

    def test_python_order_matches_cpp_flat_offsets(self):
        for K in range(1, 33):
            pairs = triangular_index_map(K)
            self.assertEqual(len(pairs), triangular_size(K))
            for offset, (k, l) in enumerate(pairs):
                self.assertEqual(offset, self.cxx_index(k, l, K))

    def test_off_diagonal_terms_match_full_symmetric_cpp_sum(self):
        import numpy as np
        x = np.array([0.25, -0.5, 0.75, 0.125], dtype=np.float64)
        h1 = np.zeros(2, dtype=np.float64)
        h2 = np.array([0.0, 0.4, 0.0], dtype=np.float64)  # K=2: (0,1) is off-diagonal
        got = apply_volterra(x, h1, h2)
        expected = np.zeros_like(x)
        expected[1:] = 2.0 * 0.4 * x[:-1] * x[1:]
        np.testing.assert_allclose(got, expected, rtol=0.0, atol=1e-12)

    def test_first_rows_are_cpp_row_major_order(self):
        self.assertEqual(
            triangular_index_map(4),
            [(0, 0), (0, 1), (0, 2), (0, 3),
             (1, 1), (1, 2), (1, 3),
             (2, 2), (2, 3), (3, 3)]
        )


if __name__ == "__main__":
    unittest.main()
