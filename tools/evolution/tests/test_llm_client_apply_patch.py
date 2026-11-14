"""Tests del conversor apply_patch -> unified diff (propuesta 0005).

El pool 'ivanna-free' emite a menudo parches estilo `*** Begin Patch`
aunque se le pida unified diff (medido 2026-11-14: 2 vacíos y 1 completo).
El conversor es fail-closed: contexto inexistente o ambiguo, archivo
ausente o `*** Delete File:` devuelven None.
"""
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import llm_client  # noqa: E402


class TestApplyPatchToUnified(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.repo = Path(self._tmp.name)
        subprocess.run(["git", "init", "-q"], cwd=self.repo, check=True)
        (self.repo / "tools" / "evolution").mkdir(parents=True)
        (self.repo / "tools" / "evolution" / "llm_client.py").write_text(
            "def extract_patch(text: str):\n"
            "    if not text:\n"
            "        return None\n",
            encoding="utf-8")

    def tearDown(self):
        self._tmp.cleanup()

    def _apply_check(self, patch):
        r = subprocess.run(["git", "apply", "--check", "--unsafe-paths", "-"],
                           input=patch, capture_output=True, text=True,
                           cwd=self.repo)
        return r.returncode == 0, r.stderr.strip()

    def test_converts_update_file_patch(self):
        text = ("```diff\n*** Begin Patch\n"
                "*** Update File: tools/evolution/llm_client.py\n@@\n"
                " def extract_patch(text: str):\n"
                '+    """Si el LLM no devuelve un diff utilizable, el llamador debe fallar, no inventar un parche."""\n'
                "     if not text:\n"
                "         return None\n"
                "*** End Patch\n```\n")
        patch = llm_client.extract_patch(text, base_dir=self.repo)
        self.assertIsNotNone(patch)
        self.assertIn("diff --git a/tools/evolution/llm_client.py", patch)
        self.assertIn("@@ -1,3 +1,4 @@", patch)
        ok, err = self._apply_check(patch)
        self.assertTrue(ok, err)

    def test_ambiguous_context_returns_none(self):
        (self.repo / "a.txt").write_text("same\nsame\n", encoding="utf-8")
        text = ("*** Begin Patch\n*** Update File: a.txt\n@@\n"
                " same\n+new\n*** End Patch\n")
        self.assertIsNone(llm_client.extract_patch(text, base_dir=self.repo))

    def test_missing_file_and_delete_return_none(self):
        text = ("*** Begin Patch\n*** Update File: noexiste.txt\n@@\n"
                " x\n+y\n*** End Patch\n")
        self.assertIsNone(llm_client.extract_patch(text, base_dir=self.repo))
        text2 = ("*** Begin Patch\n*** Delete File: a.txt\n*** End Patch\n")
        (self.repo / "a.txt").write_text("x\n", encoding="utf-8")
        self.assertIsNone(llm_client.extract_patch(text2, base_dir=self.repo))

    def test_add_file(self):
        text = ("*** Begin Patch\n*** Add File: nuevo.txt\n@@\n"
                "+hola\n+mundo\n*** End Patch\n")
        patch = llm_client.extract_patch(text, base_dir=self.repo)
        self.assertIsNotNone(patch)
        self.assertIn("--- /dev/null", patch)
        self.assertIn("+++ b/nuevo.txt", patch)
        ok, err = self._apply_check(patch)
        self.assertTrue(ok, err)


if __name__ == "__main__":
    unittest.main()
