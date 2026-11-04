"""Los literales del perfil de sala MAGISTRAL no pueden divergir entre sus copias.

RoomGeometryConfig.hpp es la fuente; omega_control_bus.h (header autocontenido), command_server.h (estado del
daemon) y WfsCalibrationManager.kt (UI) llevan copias literales. Si una difiere, el efecto, el daemon y la app
colocarian los altavoces en salas distintas sin ningun error visible.
"""
import os
import re
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CPP = os.path.join(ROOT, "app", "src", "main", "cpp")
KT = os.path.join(ROOT, "app", "src", "main", "java", "com", "ivanna", "omega", "spatial", "WfsCalibrationManager.kt")


def read(p):
    with open(p, encoding="utf-8") as f:
        return f.read()


def floats(text):
    return [round(float(x), 4) for x in re.findall(r"-?\d+\.\d+", text)]


def arrays_cpp(text, names):
    out = []
    for n in names:
        m = re.search(n + r"[^{]*\{\{?([^}]*)\}", text)
        assert m, n
        out.append(floats(m.group(1)))
    return out


EXPECTED = (
    [0.565, 3.265, 0.0, 3.83, 0.25, 3.58, 3.0],
    [1.5, 1.5, 1.8, 1.8, 2.83, 2.83, 0.0],
    [0.0, 0.0, 3.5, 3.5, 3.5, 3.5, 3.0],
)


class MagistralDefaultsTest(unittest.TestCase):
    def test_room_geometry_config_is_the_source(self):
        t = read(os.path.join(CPP, "spatial", "RoomGeometryConfig.hpp"))
        got = tuple(arrays_cpp(t, ["speakerX", "speakerY", "speakerZ"]))
        self.assertEqual(got, tuple(EXPECTED))

    def test_control_bus_default_snapshot(self):
        t = read(os.path.join(CPP, "include", "omega_control_bus.h"))
        got = tuple(arrays_cpp(t, [r"kX\[OMEGA_CTRL_WFS_SPEAKERS\]", r"kY\[OMEGA_CTRL_WFS_SPEAKERS\]", r"kZ\[OMEGA_CTRL_WFS_SPEAKERS\]"]))
        self.assertEqual(got, tuple(EXPECTED))

    def test_daemon_default_state(self):
        t = read(os.path.join(CPP, "daemon", "control", "command_server.h"))
        got = tuple(arrays_cpp(t, [r"wfs_speaker_x\[7\]", r"wfs_speaker_y\[7\]", r"wfs_speaker_z\[7\]"]))
        self.assertEqual(got, tuple(EXPECTED))

    def test_kotlin_calibration_defaults(self):
        t = read(KT)
        got = []
        for n in ("DEFAULT_X", "DEFAULT_Y", "DEFAULT_Z"):
            m = re.search(r"val " + n + r"\s*=\s*floatArrayOf\(([^)]*)\)", t)
            self.assertIsNotNone(m, n)
            got.append(floats(m.group(1)))
        self.assertEqual(tuple(got), tuple(EXPECTED))

    def test_listener_and_room_size_agree(self):
        cfg = read(os.path.join(CPP, "spatial", "RoomGeometryConfig.hpp"))
        kt = read(KT)
        self.assertIn("listenerX = 1.915f", cfg)
        self.assertRegex(cfg, r"roomWidthM\s*=\s*3\.83f")
        self.assertRegex(cfg, r"roomHeightM\s*=\s*2\.83f")
        self.assertRegex(kt, r"NATIVE_LISTENER_X\s*=\s*1\.915f")
        self.assertRegex(kt, r"ROOM_WIDTH_M\s*=\s*3\.83f")
        self.assertRegex(kt, r"ROOM_HEIGHT_M\s*=\s*2\.83f")
        self.assertNotIn("1.75f", kt)   # el oyente nativo antiguo no debe sobrevivir en la UI

    def test_xtc_geometry_is_wired_in_both_real_route_cancellers(self):
        core = read(os.path.join(CPP, "IvannaFusionCore.cpp"))
        eff = read(os.path.join(CPP, "omega_effect.cpp"))
        self.assertIn("crosstalkPlacement", core)
        self.assertIn("m_xtc.setGeometry", core)
        self.assertIn("crosstalkPlacement", eff)
        self.assertIn("fourd::setSpeakerGeometry", eff)


if __name__ == "__main__":
    unittest.main()
