import json
import tempfile
import unittest
from pathlib import Path

from scripts.prepare_release_manifest import prepare_manifest


class PrepareReleaseManifestTest(unittest.TestCase):
    def test_generates_canonical_manifest_from_aligned_release_metadata(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            module = root / "module.prop"
            version = root / "version.properties"
            update = root / "update.json"
            module.write_text("version=v2.5.342\nversionCode=633\n", encoding="utf-8")
            version.write_text("versionName=2.5.342\nversionCode=633\n", encoding="utf-8")
            update.write_text(json.dumps({"version": "v2.5.341", "versionCode": 632}), encoding="utf-8")

            result = prepare_manifest(
                module, version, update, "owner/repository"
            )

            self.assertEqual(result["version"], "v2.5.342")
            self.assertEqual(result["versionCode"], 633)
            self.assertEqual(
                result["zipUrl"],
                "https://github.com/owner/repository/releases/download/v2.5.342/ivanna_omega_supreme_v2.5.342.zip",
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
            module.write_text("version=v2.5.342\nversionCode=633\n", encoding="utf-8")
            version.write_text("versionName=2.5.341\nversionCode=632\n", encoding="utf-8")
            update.write_text("{}", encoding="utf-8")
            with self.assertRaises(ValueError):
                prepare_manifest(module, version, update, "owner/repository")


if ( __name__ == "__main__" ):
    unittest.main()
