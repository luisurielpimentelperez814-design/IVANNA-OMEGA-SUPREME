import tempfile
import unittest
from scripts.ivanna_synthetic_audio_suite import run_suite


class SyntheticAudioSuiteTests(unittest.TestCase):
    def test_injected_defects_are_detected_and_clean_signal_stays_clean(self):
        with tempfile.TemporaryDirectory() as d:
            result = run_suite(d)
            self.assertEqual(result["status"], "PASS", result["cases"])
            self.assertEqual(result["case_count"], 7)
            self.assertEqual(result["failed"], 0)


if __name__ == "__main__":
    unittest.main()
