import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "check_hws_release_approval.py"
PHASES = ("HWS-01", "HWS-02", "HWS-03", "HWS-04")


class HardwareSovereigntyReleaseGateTests(unittest.TestCase):
    def run_manifest(self, phases, schema_version=1):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for phase in PHASES:
                evidence = root / "evidence" / phase.lower() / "ci-proof.txt"
                evidence.parent.mkdir(parents=True, exist_ok=True)
                evidence.write_text(f"{phase} PASS\\n", encoding="utf-8")
            manifest = root / "approval.json"
            manifest.write_text(json.dumps({"schema_version": schema_version, "phases": phases}), encoding="utf-8")
            return subprocess.run(
                [sys.executable, str(SCRIPT), str(manifest), "--root", str(root)],
                text=True, capture_output=True, check=False,
            )

    def approved_phases(self):
        return {
            phase: {"status": "APPROVED", "evidence": [f"evidence/{phase.lower()}/ci-proof.txt"]}
            for phase in PHASES
        }

    def test_current_manifest_blocks_production_for_status(self):
        phases = self.approved_phases()
        phases["HWS-02"]["status"] = "NOT_APPROVED"
        result = self.run_manifest(phases)
        self.assertEqual(result.returncode, 1)
        self.assertIn("HWS-02: status='NOT_APPROVED'", result.stdout)

    def test_all_approved_with_unique_phase_evidence_passes(self):
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
        phases["HWS-02"]["evidence"] = ["evidence/hws-02/missing.txt"]
        self.assertEqual(self.run_manifest(phases).returncode, 1)

    def test_evidence_cannot_be_reused_across_phases(self):
        phases = self.approved_phases()
        phases["HWS-02"]["evidence"] = phases["HWS-01"]["evidence"]
        result = self.run_manifest(phases)
        self.assertEqual(result.returncode, 1)
        self.assertIn("reused by multiple phases", result.stdout)

    def test_evidence_must_be_phase_scoped(self):
        phases = self.approved_phases()
        phases["HWS-03"]["evidence"] = ["evidence/hws-02/ci-proof.txt"]
        self.assertEqual(self.run_manifest(phases).returncode, 1)

    def test_boolean_or_float_schema_version_blocks(self):
        self.assertEqual(self.run_manifest(self.approved_phases(), True).returncode, 1)
        self.assertEqual(self.run_manifest(self.approved_phases(), 1.0).returncode, 1)

    def test_unknown_manifest_schema_blocks(self):
        self.assertEqual(self.run_manifest(self.approved_phases(), 99).returncode, 1)


if __name__ == "__main__":
    unittest.main()
