import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "check_hws_release_approval.py"
PHASES = ("HWS-01", "HWS-02", "HWS-03", "HWS-04")


class HardwareSovereigntyReleaseGateTests(unittest.TestCase):
    def run_manifest(self, phases):
        with tempfile.TemporaryDirectory() as tmp:
            manifest = Path(tmp) / "approval.json"
            manifest.write_text(json.dumps({"schema_version": 1, "phases": phases}), encoding="utf-8")
            return subprocess.run(
                [sys.executable, str(SCRIPT), str(manifest)],
                text=True, capture_output=True, check=False,
            )

    def approved_phases(self):
        return {phase: {"status": "APPROVED", "evidence": ["scripts/check_hws_release_approval.py"]} for phase in PHASES}

    def test_current_manifest_blocks_production(self):
        result = self.run_manifest({
            "HWS-01": {"status": "IN_PROGRESS", "evidence": ["audit.md"]},
            "HWS-02": {"status": "NOT_APPROVED", "evidence": ["audit.md"]},
            "HWS-03": {"status": "PARTIAL", "evidence": ["thermal_governor.hpp"]},
            "HWS-04": {"status": "PARTIAL", "evidence": ["app/src/main/cpp/pi_lstm_bridge_jni.cpp"]},
        })
        self.assertEqual(result.returncode, 1)
        self.assertIn("BLOCKED", result.stdout)

    def test_all_approved_with_evidence_passes(self):
        result = self.run_manifest(self.approved_phases())
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("PASS", result.stdout)

    def test_missing_phase_blocks(self):
        phases = self.approved_phases()
        del phases["HWS-04"]
        self.assertEqual(self.run_manifest(phases).returncode, 1)

    def test_empty_evidence_blocks(self):
        phases = self.approved_phases()
        phases["HWS-03"]["evidence"] = []
        self.assertEqual(self.run_manifest(phases).returncode, 1)

    def test_missing_evidence_file_blocks(self):
        phases = self.approved_phases()
        phases["HWS-02"]["evidence"] = ["evidence/does-not-exist.txt"]
        self.assertEqual(self.run_manifest(phases).returncode, 1)


if __name__ == "__main__":
    unittest.main()
