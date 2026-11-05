import importlib.util
import pathlib
import unittest

SCRIPT = pathlib.Path(__file__).resolve().parents[1] / "scripts" / "detect_release_bump.py"
SPEC = importlib.util.spec_from_file_location("detect_release_bump", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class ReleaseBumpDetectionTests(unittest.TestCase):
    def test_same_manifest_does_not_publish_again(self):
        manifest = {"versionName": "2.5.351", "versionCode": "642"}
        self.assertFalse(MODULE.should_publish(manifest, manifest))

    def test_release_is_detected_across_multiple_commits_after_bump(self):
        current = {"versionName": "2.5.353", "versionCode": "644"}
        baseline = {"versionName": "2.5.351", "versionCode": "642"}
        self.assertTrue(MODULE.should_publish(current, baseline))

    def test_first_release_has_no_baseline(self):
        self.assertTrue(MODULE.should_publish(
            {"versionName": "1.0.0", "versionCode": "1"}, None))

    def test_rejects_code_rollback(self):
        with self.assertRaises(ValueError):
            MODULE.should_publish(
                {"versionName": "2.5.350", "versionCode": "641"},
                {"versionName": "2.5.351", "versionCode": "642"})

    def test_rejects_name_change_without_code_increment(self):
        with self.assertRaises(ValueError):
            MODULE.should_publish(
                {"versionName": "2.5.352", "versionCode": "642"},
                {"versionName": "2.5.351", "versionCode": "642"})

    def test_rejects_code_increment_without_semver_advance(self):
        with self.assertRaises(ValueError):
            MODULE.should_publish(
                {"versionName": "2.5.351", "versionCode": "643"},
                {"versionName": "2.5.351", "versionCode": "642"})


if __name__ == "__main__":
    unittest.main()
