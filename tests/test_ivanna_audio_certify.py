import math
import os
import struct
import tempfile
import unittest
import wave

from scripts.ivanna_audio_certify import analyze


def make_tone(path, seconds=0.25, amplitude=0.5):
    rate, hz, channels = 48000, 1000, 2
    with wave.open(path, "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(2)
        w.setframerate(rate)
        data = bytearray()
        for i in range(int(rate * seconds)):
            value = round(amplitude * 32767 * math.sin(2 * math.pi * hz * i / rate))
            data.extend(struct.pack("<hh", value, value))
        w.writeframes(data)


class AudioCertifyTests(unittest.TestCase):
    def test_clean_tone_is_measured(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "tone.wav")
            make_tone(p)
            r = analyze(p, tone_hz=1000)
            self.assertEqual(r["sample_rate_hz"], 48000)
            self.assertEqual(r["channels"], 2)
            self.assertEqual(r["clipped_samples"], 0)
            self.assertGreater(r["channel_metrics"][0]["rms_dbfs"], -12)
            self.assertLess(r["channel_metrics"][0]["rms_dbfs"], -5)
            self.assertLess(r["tone_analysis"][0]["thdn_percent"], 0.1)

    def test_reference_comparison_of_identical_file_has_zero_residual(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "tone.wav")
            make_tone(p)
            r = analyze(p, reference=p)
            self.assertIsNone(r["reference_comparison"]["snr_db_per_channel"][0])

    def test_mismatched_length_is_reported(self):
        with tempfile.TemporaryDirectory() as d:
            a, b = os.path.join(d, "a.wav"), os.path.join(d, "b.wav")
            make_tone(a, seconds=0.25)
            make_tone(b, seconds=0.20)
            r = analyze(a, reference=b)
            self.assertGreater(r["reference_comparison"]["length_delta_frames"], 0)


if __name__ == "__main__":
    unittest.main()
