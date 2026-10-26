import unittest
from scripts.ivanna_auditory_gate import evaluate


def manifest(status="PASS", physical=True, listening=True):
    return {"commit": "test-sha", "app_version": "test",
            "checks": [{"id": "native_tests", "status": status, "critical": True},
                       {"id": "apk_integrity", "status": "PASS", "critical": True}],
            "device_scope": {"routes": ["speaker"], "required_routes": ["speaker"],
                             "physical_device_evidence": physical,
                             "level_matched_listening_evidence": listening}}


class GateTests(unittest.TestCase):
    def test_missing_device_evidence_is_amber(self):
        r = evaluate(manifest(physical=False))
        self.assertEqual(r["status"], "AMBER")
        self.assertIn("physical_device_evidence", r["missing_evidence"])

    def test_critical_failure_is_red(self):
        r = evaluate(manifest(status="FAIL"))
        self.assertEqual(r["status"], "RED")
        self.assertIn("native_tests", r["failed_checks"])

    def test_missing_route_is_amber(self):
        m = manifest()
        m["device_scope"]["required_routes"] = ["speaker", "bluetooth"]
        r = evaluate(m)
        self.assertEqual(r["status"], "AMBER")
        self.assertIn("route:bluetooth", r["missing_evidence"])

    def test_complete_evidence_is_green(self):
        self.assertEqual(evaluate(manifest())["status"], "GREEN")

    def test_invalid_status_rejected(self):
        m = manifest()
        m["checks"][0]["status"] = "MAYBE"
        with self.assertRaises(ValueError):
            evaluate(m)


if __name__ == "__main__":
    unittest.main()
