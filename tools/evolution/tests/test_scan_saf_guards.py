"""Tests de scan_saf_guards (propuesta 0002, redactada por el LLM vía OmniRoute).

Corrección del draft del LLM al integrar: el import original
(`tools.evolution.evolution_engine`) fallaba con `python3 -m unittest
discover -s tools/evolution/tests` porque la raíz del repo no está en
sys.path y tools/ no es un paquete. Se importa como módulo top-level
con la raíz del repo insertada en sys.path; el test hace monkeypatch de
evolution_engine.REPO al árbol falso y lo restaura en tearDown.

Casos (falsos positivos medidos 2026-11-14):
- .cpp sin funciones y sin guarda -> NO es hit (regla 1).
- .cpp con función, sin guarda en el .cpp pero con isfinite en el header
  hermano -> NO es hit (regla 3).
- .cpp con función repetida (declaración + definición), sin guarda en .cpp
  ni header -> UN solo hit, función una sola vez (reglas 1-4).
"""
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import evolution_engine  # noqa: E402


class TestScanSafGuards(unittest.TestCase):
    def setUp(self):
        self._old_repo = evolution_engine.REPO
        self._tmp = tempfile.TemporaryDirectory()
        self.repo = Path(self._tmp.name)
        (self.repo / "app/src/main/cpp/include").mkdir(parents=True, exist_ok=True)
        evolution_engine.REPO = self.repo

    def tearDown(self):
        evolution_engine.REPO = self._old_repo
        self._tmp.cleanup()

    def test_scan_saf_guards_filters_false_positives_and_deduplicates(self):
        (self.repo / "app/src/main/cpp/saf_empty.cpp").write_text(
            "int x = 0;\n", encoding="utf-8"
        )

        (self.repo / "app/src/main/cpp/saf_header_guard.cpp").write_text(
            "void guarded(void) {\n  return;\n}\n", encoding="utf-8"
        )
        (self.repo / "app/src/main/cpp/include/saf_header_guard.h").write_text(
            "#pragma once\ninline bool safe_header() { return std::isfinite(1.0); }\n",
            encoding="utf-8",
        )

        (self.repo / "app/src/main/cpp/saf_latent_bridge.cpp").write_text(
            "void ivanna_saf_apply_latent();\n"
            "void ivanna_saf_apply_latent() {\n"
            "  int a = 1;\n"
            "}\n",
            encoding="utf-8",
        )

        hits = evolution_engine.scan_saf_guards()

        self.assertEqual(len(hits), 1)
        hit = hits[0]
        self.assertEqual(hit["file"], "app/src/main/cpp/saf_latent_bridge.cpp")
        self.assertEqual(hit["functions"], ["ivanna_saf_apply_latent"])
        self.assertFalse(hit["has_isfinite_guard"])
        self.assertEqual(hit["guard_source"], "none")
        self.assertIsNone(hit["header_checked"])


if __name__ == "__main__":
    unittest.main()
