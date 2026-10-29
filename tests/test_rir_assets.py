"""Integridad de los activos RIR reales (200 salas) y de la columna RT60 medida.
Solo numpy + wave (sin h5py), apto para el job `python3 -m unittest discover -s tests`.
Incluye casos negativos: el detector debe rechazar el RT60 nominal y un metadata manipulado."""
import csv, io, subprocess, sys, unittest, wave
from pathlib import Path
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/rir"))
import measure_rt60 as m  # noqa: E402

APP = ROOT / "app/src/main/assets/ivanna_omega/rir"
MAG = ROOT / "magisk_module/system/etc/ivanna_omega/rir"

def rows(p): return list(csv.DictReader(io.StringIO(p.read_bytes().decode("utf-8"))))

class RirAssets(unittest.TestCase):
    def test_estimator_selftest(self):
        self.assertEqual(m.selftest(), 0)

    def test_200_rooms_real_wavs(self):
        r = rows(APP / "metadata.csv"); self.assertEqual(len(r), 200)
        for row in r:
            x, fs = m.load_ch0(APP / row["filename"])
            self.assertEqual(fs, 16000); self.assertTrue(np.isfinite(x).all()); self.assertGreater(np.abs(x).max(), 0.0)

    def test_measured_column_matches_remeasurement(self):
        self.assertEqual(subprocess.run([sys.executable, str(ROOT / "tools/rir/measure_rt60.py"), "--check"]).returncode, 0)

    def test_nominal_rt60_is_not_the_decay_time(self):
        ratios = np.array([float(x[m.COL]) / float(x["RT60_s"]) for x in rows(APP / "metadata.csv")])
        self.assertTrue(2.2 < ratios.mean() < 2.4 and ratios.min() > 2.0 and ratios.max() < 2.6, (ratios.mean(), ratios.min(), ratios.max()))

    def test_negative_control_tampered_metadata_is_detected(self):
        meas = {x["filename"]: float(x[m.COL]) for x in rows(APP / "metadata.csv")}
        bad = [f for f, v in meas.items() if abs(v * 1.05 - v) <= 0.01 * v]  # un +5 % debe ser rechazado (tol 1 %)
        self.assertEqual(bad, [])

    def test_magisk_copy_in_sync(self):
        a, b = rows(APP / "metadata.csv"), rows(MAG / "metadata.csv"); self.assertEqual(a, b)
        for row in a[:10]:
            self.assertEqual((APP / row["filename"]).read_bytes(), (MAG / row["filename"]).read_bytes())

if __name__ == "__main__": unittest.main()
