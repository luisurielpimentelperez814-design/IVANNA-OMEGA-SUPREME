#!/usr/bin/env python3
"""Regression tests for scripts/audit_safmus_corpus.py (stdlib only)."""
import importlib.util
import tempfile
import unittest
import wave
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "audit_safmus_corpus.py"
SPEC = importlib.util.spec_from_file_location("audit_safmus_corpus", SCRIPT)
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)


class CorpusAuditTests(unittest.TestCase):
    def test_sofa_hdf5_signature_at_start(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "valid.sofa"
            path.write_bytes(AUDIT.HDF5_MAGIC + b"\x00" * 32)
            result = AUDIT.inspect_sofa(path)
            self.assertEqual(result["status"], "container-signature-only")
            self.assertEqual(result["hdf5_signature_offset"], 0)

    def test_sofa_hdf5_signature_after_user_block(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "userblock.sofa"
            path.write_bytes(b"\x00" * 512 + AUDIT.HDF5_MAGIC + b"\x00" * 32)
            result = AUDIT.inspect_sofa(path)
            self.assertEqual(result["status"], "container-signature-only")
            self.assertEqual(result["hdf5_signature_offset"], 512)

    def test_invalid_sofa_signature_is_reported(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "not-sofa.sofa"
            path.write_bytes(b"not-hdf5")
            self.assertEqual(AUDIT.inspect_sofa(path)["status"], "invalid-container-signature")

    def test_valid_wav_header(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "impulse.wav"
            with wave.open(str(path), "wb") as wav:
                wav.setnchannels(2)
                wav.setsampwidth(2)
                wav.setframerate(48000)
                wav.writeframes(b"\x00\x00" * 2)
            result = AUDIT.inspect_wav(path)
            self.assertEqual(result["status"], "valid-header")
            self.assertEqual(result["sample_rate_hz"], 48000)
            self.assertEqual(result["channels"], 2)

    def test_invalid_ihr1_header_is_reported(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "broken.ihr1"
            path.write_bytes(b"BAD!" + b"\x00" * 12)
            self.assertEqual(AUDIT.inspect_ihr1(path)["status"], "invalid-header")


if __name__ == "__main__":
    unittest.main()
