"""Regression gate: AutoEqManager.kt's hardcoded PROFILES must match what
tools/hpir/extract_hpir_profiles.py actually measures from the real HpIR
SOFA corpus, right now -- not what someone remembered to copy last time.

AutoEqManager.kt documents (2026-09-10) that its Kotlin literal and the
extractor's JSON output had already drifted out of sync once (4 bands/±6dB
clamp vs. the extractor's 5 bands/±8dB clamp) with nothing to catch it
automatically. This test is that missing gate: it re-runs the real
extractor against the real asset corpus and parses the real Kotlin source,
so a future edit to either side that breaks the contract fails CI instead
of shipping silently. See also tests/test_validate_sofa_hdf5.py (structural
HDF5 validity) and scripts/check_safmus_transversal_wiring.py (finite-value
wiring) -- this test covers the third leg: numeric fidelity of the one
pipeline that *does* consume the .sofa corpus at build/profile time.
"""
from __future__ import annotations

import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "hpir"))

KOTLIN_PATH = ROOT / "app/src/main/java/com/ivanna/omega/spatial/AutoEqManager.kt"

_BAND_RE = re.compile(
    r"Band\(\s*([-\d.]+)f\s*,\s*([-\d.]+)f\s*,\s*([-\d.]+)f\s*\)"
)
_PROFILE_HEADER_RE = re.compile(r'"([^"]+)"\s+to\s+listOf\(')


def parse_kotlin_profiles(text: str) -> dict[str, list[tuple[float, float, float]]]:
    """Extract {model: [(freqHz, gainDb, q), ...]} from the PROFILES map
    literal. Deliberately simple/regex-based: if AutoEqManager.kt's syntax
    changes enough to break this, that is itself worth a human looking at
    the diff, not a smarter parser silently papering over it."""
    start = text.index("private val PROFILES")
    depth = 0
    end = start
    for i, ch in enumerate(text[start:], start):
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0:
                end = i + 1
                break
    body = text[start:end]

    profiles: dict[str, list[tuple[float, float, float]]] = {}
    headers = list(_PROFILE_HEADER_RE.finditer(body))
    for idx, header in enumerate(headers):
        model = header.group(1)
        block_end = headers[idx + 1].start() if idx + 1 < len(headers) else len(body)
        block = body[header.end():block_end]
        bands = [
            (float(m.group(1)), float(m.group(2)), float(m.group(3)))
            for m in _BAND_RE.finditer(block)
        ]
        profiles[model] = bands
    return profiles


class AutoEqKotlinMatchesExtractorTest(unittest.TestCase):
    def test_kotlin_profiles_match_freshly_measured_corpus(self):
        import extract_hpir_profiles as extractor  # tools/hpir/extract_hpir_profiles.py

        measured: dict[str, list[tuple[float, float, float]]] = {}
        missing = []
        for filename, model in extractor.MODELS.items():
            sofa_path = Path(extractor.DEFAULT_SOFA_DIR) / filename
            if not sofa_path.exists():
                missing.append(filename)
                continue
            profile = extractor.measure_profile(str(sofa_path), model)
            bands: list[tuple[float, float, float]] = []
            if profile["shelf"] is not None:
                s = profile["shelf"]
                bands.append((s["freq"], s["gainDb"], s["q"]))
            for b in profile["bands"]:
                bands.append((b["freq"], b["gainDb"], b["q"]))
            measured[model] = bands

        self.assertFalse(
            missing,
            f"HpIR SOFA source files referenced by the extractor are missing: {missing} "
            "-- AutoEqManager's measured claim can't be checked without them.",
        )

        kotlin_text = KOTLIN_PATH.read_text(encoding="utf-8")
        kotlin_profiles = parse_kotlin_profiles(kotlin_text)

        self.assertEqual(
            sorted(kotlin_profiles.keys()), sorted(measured.keys()),
            "AutoEqManager.kt PROFILES keys no longer match the extractor's MODELS map "
            "(a headphone was added/removed on one side only).",
        )

        for model in measured:
            with self.subTest(model=model):
                expected = measured[model]
                actual = kotlin_profiles[model]
                self.assertEqual(
                    len(expected), len(actual),
                    f"{model}: Kotlin has {len(actual)} bands, freshly measured corpus "
                    f"has {len(expected)} -- regenerate with extract_hpir_profiles.py, "
                    "do not hand-edit (see AutoEqManager.kt's own 2026-09-10 note).",
                )
                for i, (exp, act) in enumerate(zip(expected, actual)):
                    for label, e, a in zip(("freqHz", "gainDb", "q"), exp, act):
                        self.assertAlmostEqual(
                            e, a, places=1,
                            msg=f"{model} band[{i}].{label}: Kotlin={a} measured={e} "
                                "-- Kotlin PROFILES has drifted from the HpIR corpus.",
                        )


if __name__ == "__main__":
    unittest.main()
