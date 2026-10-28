import math
import unittest
import numpy as np
import sync_asrc


class SyncAsrcTests(unittest.TestCase):
    def test_table_has_unit_dc_and_finite_coefficients(self):
        table = sync_asrc.make_table()
        self.assertEqual(table.shape, (1026, 64))
        self.assertTrue(np.isfinite(table).all())
        self.assertLess(float(np.max(np.abs(table.sum(axis=1) - 1.0))), 2e-6)

    def test_asrc_step_preserves_dc_and_advances_one_input_frame_at_unity(self):
        table = sync_asrc.make_table()
        out, index, phase = sync_asrc.asrc_step(np.ones(128, dtype=np.float32), 64, 0, 1.0, table)
        self.assertAlmostEqual(out, 1.0, delta=2e-6)
        self.assertEqual((index, phase), (65, 0))

    def test_phase_accumulator_tracks_fractional_advance(self):
        rho = 1.0 + 10e-6
        mu = index = 0
        for _ in range(200_000):
            total = mu + int(rho * sync_asrc.Q32)
            index += total >> 32
            mu = total & 0xFFFFFFFF
        self.assertLess(abs(index + mu / sync_asrc.Q32 - 200_000 * rho), 1e-3)

    def test_selector_matches_documented_feasible_example(self):
        wn = sync_asrc.seleccionar_wn(48_000, 0.05, 0.01, 10e-6, 2000, 0, 4000,
                                      2, 30, 1e-6 / 60, 0.01)
        self.assertAlmostEqual(wn, 0.3394, delta=0.002)

    def test_selector_rejects_infeasible_jitter_headroom(self):
        with self.assertRaisesRegex(ValueError, "INFACTIBLE"):
            sync_asrc.seleccionar_wn(48_000, 0.05, 0.01, 10e-6, 100, 0, 200,
                                     40, 30, 1e-6 / 60, 0.01)

    def test_loop_output_is_bounded_and_covariance_stays_symmetric(self):
        loop = sync_asrc.SyncLoop(48_000, 0.01, 0.1, 0.5)
        for k in range(1000):
            u = loop.step(0.1 * math.sin(k * 0.1))
            self.assertTrue(math.isfinite(u))
            self.assertLessEqual(abs(u), sync_asrc.UMAX)
            self.assertTrue(np.isfinite(loop.P).all())
            self.assertTrue(np.allclose(loop.P, loop.P.T, atol=1e-12))

    def test_gcc_phat_fractional_delay_sign_and_bound(self):
        rng = np.random.default_rng(42)
        x = rng.normal(size=8192)
        delay = 7
        left = x
        right = np.concatenate([np.zeros(delay), x[:-delay]])
        tau = sync_asrc.gcc_phat(left, right, 48_000, up=8)
        self.assertLess(tau, 0.0)
        self.assertLess(abs(tau + delay / 48_000), 1.5 / (48_000 * 8))

    def test_gcc_phat_rejects_malformed_inputs(self):
        with self.assertRaises(ValueError):
            sync_asrc.gcc_phat(np.zeros(4), np.zeros(5), 48_000)

    def test_self_test(self):
        sync_asrc.self_test()


if __name__ == "__main__":
    unittest.main()
