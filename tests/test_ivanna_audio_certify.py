import math
import os
import struct
import tempfile
import unittest
import wave

from scripts.ivanna_audio_certify import analyze, load_wav


def make_pcm(path, seconds=0.25, amplitude=0.5, right_amplitude=None, dc=0.0, rate=48000, hz=1000, bits=16):
    channels = 2
    width = bits // 8
    with wave.open(path, "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(width)
        w.setframerate(rate)
        raw = bytearray()
        for i in range(int(rate * seconds)):
            vals = []
            for amp in (amplitude, amplitude if right_amplitude is None else right_amplitude):
                value = round((dc + amp * math.sin(2 * math.pi * hz * i / rate)) * ((1 << (bits - 1)) - 1))
                value = max(-(1 << (bits - 1)), min((1 << (bits - 1)) - 1, value))
                if bits == 16:
                    raw.extend(struct.pack("<h", value))
                elif bits == 24:
                    raw.extend(int(value & 0xFFFFFF).to_bytes(3, "little"))
                elif bits == 32:
                    raw.extend(struct.pack("<i", value))
                elif bits == 8:
                    raw.append(max(0, min(255, value + 128)))
        w.writeframes(raw)


class AudioCertifyAdversarialTests(unittest.TestCase):
    def test_clean_tone_has_no_clipping_and_low_residual(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "tone.wav")
            make_pcm(p)
            r = analyze(p, tone_hz=1000)
            self.assertEqual(r["sample_rate_hz"], 48000)
            self.assertEqual(r["channels"], 2)
            self.assertEqual(r["clipped_samples"], 0)
            self.assertLess(r["tone_analysis"][0]["thdn_percent"], 0.1)
            self.assertEqual(r["diagnostic_status"], "NO_SCREENING_FLAGS")

    def test_full_scale_clipping_is_critical(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "clip.wav")
            make_pcm(p, amplitude=1.0)
            r = analyze(p)
            self.assertGreater(r["clipped_samples"], 0)
            self.assertEqual(r["diagnostic_status"], "RED")
            self.assertIn("PCM_CLIPPING", [f["code"] for f in r["findings"]])

    def test_channel_imbalance_has_specific_adjustment(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "imbalance.wav")
            make_pcm(p, amplitude=0.5, right_amplitude=0.2)
            r = analyze(p)
            self.assertGreater(r["channel_rms_imbalance_db"], 3.0)
            finding = next(f for f in r["findings"] if f["code"] == "CHANNEL_IMBALANCE")
            self.assertEqual(finding["severity"], "CRITICAL")
            self.assertIn("per-channel gain", finding["recommended_adjustment"])

    def test_dc_offset_is_reported(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "dc.wav")
            make_pcm(p, amplitude=0.2, dc=0.02)
            r = analyze(p)
            self.assertIn("DC_OFFSET", [f["code"] for f in r["findings"]])

    def test_reference_mismatch_reports_adjustment(self):
        with tempfile.TemporaryDirectory() as d:
            a, b = os.path.join(d, "after.wav"), os.path.join(d, "before.wav")
            make_pcm(a, seconds=0.25)
            make_pcm(b, seconds=0.20)
            r = analyze(a, reference=b)
            self.assertIn("REFERENCE_LENGTH_MISMATCH", [f["code"] for f in r["findings"]])
            self.assertGreater(r["reference_comparison"]["length_delta_frames"], 0)

    def test_identical_reference_has_zero_residual_not_infinite_json(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "same.wav")
            make_pcm(p)
            r = analyze(p, reference=p)
            self.assertIsNone(r["reference_comparison"]["snr_db_per_channel"][0])
            import json
            json.dumps(r, allow_nan=False)

    def test_24_bit_pcm_sign_extension(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "24bit.wav")
            make_pcm(p, bits=24)
            r = load_wav(p)
            self.assertEqual(r["bits_per_sample"], 24)
            self.assertEqual(r["channels"], 2)
            self.assertEqual(len(r["samples"][0]), r["frames"])

    def test_incompatible_reference_sample_rate_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            a, b = os.path.join(d, "a.wav"), os.path.join(d, "b.wav")
            make_pcm(a, rate=48000)
            make_pcm(b, rate=44100)
            with self.assertRaises(ValueError):
                analyze(a, reference=b)

    def test_invalid_tone_frequency_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "tone.wav")
            make_pcm(p)
            with self.assertRaises(ValueError):
                analyze(p, tone_hz=24000)


if __name__ == "__main__":
    unittest.main()
