import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class TransauralRouteContractTest(unittest.TestCase):
    def read(self, relative: str) -> str:
        return (ROOT / relative).read_text(encoding="utf-8")

    def test_rir_wet_tail_crossmix_is_never_enabled_by_route_or_rt60(self):
        paths = [
            "app/src/main/cpp/omega_effect.cpp",
            "app/src/main/cpp/jni/ivanna_omega_jni.cpp",
        ]
        for path in paths:
            source = self.read(path)
            calls = re.findall(r"setTransauralXtcStrength\(([^)]*)\)", source)
            self.assertTrue(calls, f"expected explicit fail-closed RIR XTC setting in {path}")
            self.assertTrue(
                all(value.strip() == "0.0f" for value in calls),
                f"RIR wet-tail crossmix must stay disabled in production runtime: {path}: {calls}",
            )
        spatial_jni = self.read("app/src/main/cpp/jni/ivanna_spatial_jni.cpp")
        self.assertNotIn("ivanna_set_route_rir_xtc", spatial_jni)

    def test_real_xtc_is_an_independent_route_gated_output_stage(self):
        chain = self.read("app/src/main/cpp/music_intelligence/FourDExcitationChain.hpp")
        surface = self.read("app/src/main/cpp/music_intelligence/FourDControlSurface.cpp")
        engage_start = chain.find("void xbEnabled()")
        engage_end = chain.find("\n    }", engage_start)
        engage_body = chain[engage_start:engage_end]
        stage_start = chain.find("void applyOutputStage(")
        stage_end = chain.find("\n    }", stage_start)
        stage_body = chain[stage_start:stage_end]
        self.assertIn("xtc_.setEngage(xbEnabled_.load(std::memory_order_relaxed));", engage_body)
        self.assertNotIn("enabled_.load", engage_body)
        self.assertIn("if (xbEnabled_.load(std::memory_order_relaxed)) xtc_.processBlock(L, R, n);", stage_body)
        self.assertNotIn("enabled_.load", stage_body)
        for name in ("applyOutputPlanar", "applyOutputInterleaved"):
            start = surface.find(f"void {name}(")
            self.assertGreaterEqual(start, 0, name)
            end = surface.find("\n}", start)
            body = surface[start:end]
            self.assertIn("g_xtcOn.load(std::memory_order_relaxed)", body, name)
            self.assertNotIn("g_enabled.load(std::memory_order_relaxed)", body, name)

    def test_route_policy_fails_closed_outside_declared_usb_aux_speakers(self):
        policy = self.read("app/src/main/java/com/ivanna/omega/audio/ExternalStereoPolicy.kt")
        manager = self.read("app/src/main/java/com/ivanna/omega/audio/AudioRouteManager.kt")
        store = self.read("app/src/main/java/com/ivanna/omega/core/ParameterStore.kt")
        self.assertIn("fun allowTransauralXtc(", policy)
        self.assertIn("syncTransauralRoute(ctx, route)", manager)
        self.assertIn('getBoolean("externalStereoLineOut", false)', store)
        self.assertIn("FourDRouteB.push(ctx, store, route)", manager)


if __name__ == "__main__":
    unittest.main()
