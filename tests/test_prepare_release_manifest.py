import json
import tempfile
import unittest
from pathlib import Path

from scripts.prepare_release_manifest import prepare_manifest


class PrepareReleaseManifestTest(unittest.TestCase):
    def test_builds_canonical_candidate_without_requiring_public_manifest_bump(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            module = root / "module.prop"
            version = root / "version.properties"
            update = root / "update.json"
            module.write_text("version=v2.5.343\nversionCode=634\n", encoding="utf-8")
            version.write_text("versionName=2.5.343\nversionCode=634\n", encoding="utf-8")
            update.write_text(json.dumps({"version": "v2.5.342", "versionCode": 633}), encoding="utf-8")

            result = prepare_manifest(module, version, update, "owner/repository")

            self.assertEqual(result["version"], "v2.5.343")
            self.assertEqual(result["versionCode"], 634)
            self.assertEqual(
                result["zipUrl"],
                "https://github.com/owner/repository/releases/download/v2.5.343/ivanna_omega_supreme_v2.5.343.zip",
            )
            self.assertEqual(
                result["changelog"],
                "https://raw.githubusercontent.com/owner/repository/main/CHANGELOG.md",
            )

    def test_rejects_mismatched_version_metadata(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            module = root / "module.prop"
            version = root / "version.properties"
            update = root / "update.json"
            module.write_text("version=v2.5.343\nversionCode=634\n", encoding="utf-8")
            version.write_text("versionName=2.5.342\nversionCode=633\n", encoding="utf-8")
            update.write_text("{}", encoding="utf-8")
            with self.assertRaises(ValueError):
                prepare_manifest(module, version, update, "owner/repository")


if __name__ == "__main__":
    unittest.main()
