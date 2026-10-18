import unittest
import numpy as np
from deh6_optics.propagation import angular_spectrum
from deh6_optics.solver import gerchberg_saxton, reconstruction_metrics
from deh6_optics.cli import make_target

class PropagationTests(unittest.TestCase):
    def test_zero_distance_identity(self):
        rng = np.random.default_rng(2)
        u = rng.normal(size=(32, 32)) + 1j * rng.normal(size=(32, 32))
        v = angular_spectrum(u, 532e-9, 8e-6, 0.0)
        self.assertTrue(np.allclose(u, v, atol=1e-10))

    def test_energy_conserved_for_propagating_bandlimited_field(self):
        n = 64
        dx = 8e-6
        lam = 532e-9
        x = np.arange(n) - n // 2
        xx, yy = np.meshgrid(x, x)
        u = np.exp(-(xx**2 + yy**2) / (2 * 8**2)).astype(np.complex128)
        v = angular_spectrum(u, lam, dx, 0.01)
        self.assertAlmostEqual(float(np.sum(abs(u)**2)), float(np.sum(abs(v)**2)), delta=1e-8)

    def test_invalid_parameters_rejected(self):
        with self.assertRaises(ValueError):
            angular_spectrum(np.ones((8, 8)), 0, 1e-6, 0.1)

    def test_evanescent_decay(self):
        rng = np.random.default_rng(42)
        u = rng.normal(size=(16, 16)) + 1j * rng.normal(size=(16, 16))
        v = angular_spectrum(u, 532e-9, 100e-9, 1e-6, evanescent='decay')
        self.assertTrue(np.isfinite(v).all())

class SolverTests(unittest.TestCase):
    def test_reproducible_and_finite(self):
        cfg = {'grid_size': 32, 'targets': [{'type': 'disk', 'x': 0, 'y': 0, 'radius': 0.18, 'amplitude': 1}]}
        t = make_target(cfg)
        p1, u1, e1 = gerchberg_saxton(t, 532e-9, 8e-6, 0.005, iterations=5, seed=3)
        p2, u2, e2 = gerchberg_saxton(t, 532e-9, 8e-6, 0.005, iterations=5, seed=3)
        self.assertTrue(np.isfinite(p1).all() and np.isfinite(u1).all())
        self.assertTrue(np.allclose(p1, p2))
        self.assertTrue(np.allclose(u1, u2))
        self.assertEqual(e1, e2)
        m = reconstruction_metrics(t, u1)
        self.assertGreaterEqual(m['normalized_rmse'], 0)
        self.assertLessEqual(m['normalized_rmse'], 1.5)

    def test_bad_target_rejected(self):
        with self.assertRaises(ValueError):
            gerchberg_saxton(np.zeros((16, 16)), 532e-9, 8e-6, 0.01)

    def test_metrics_bounds(self):
        cfg = {'grid_size': 32, 'targets': [{'type': 'disk', 'x': 0, 'y': 0, 'radius': 0.2, 'amplitude': 1}]}
        t = make_target(cfg)
        m_ident = reconstruction_metrics(t, t.astype(np.complex128))
        self.assertAlmostEqual(m_ident['normalized_rmse'], 0.0, places=5)
        self.assertAlmostEqual(m_ident['amplitude_correlation'], 1.0, places=5)
        self.assertAlmostEqual(m_ident['target_region_energy_fraction'], m_ident['target_energy_fraction_reference'], places=5)

    def test_input_amplitude_validation(self):
        t = np.ones((8, 8))
        bad_amp = np.ones((4, 4))
        with self.assertRaises(ValueError):
            gerchberg_saxton(t, 532e-9, 8e-6, 0.01, input_amplitude=bad_amp)

if __name__ == '__main__':
    unittest.main()
