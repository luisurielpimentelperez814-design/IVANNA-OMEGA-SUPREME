"""Contrato del canal ASRC/reloj HAL del modulo Magisk (asrc_link.sh).

Causa raiz protegida: sin /data/vendor/ivanna_audio el daemon no puede publicar
asrc_state ni abrir clock_snapshot.v2, y con los archivos en 0660 root:root la
app no puede leerlos. Estas pruebas ejecutan el helper real contra un directorio
temporal (IVANNA_CLOCK_DIR); no prueban SELinux ni el dispositivo.
"""
import os
import re
import stat
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MOD = os.path.join(ROOT, "magisk_module")
HELPER = os.path.join(MOD, "asrc_link.sh")


def run(clock_dir, body):
    env = dict(os.environ, IVANNA_CLOCK_DIR=clock_dir)
    return subprocess.run(["sh", "-c", f'. "{HELPER}"; {body}'], env=env,
                          capture_output=True, text=True, timeout=30)


def mode(path):
    return stat.S_IMODE(os.stat(path).st_mode)


class AsrcLinkTest(unittest.TestCase):
    def test_prepare_creates_missing_directory(self):
        with tempfile.TemporaryDirectory() as t:
            d = os.path.join(t, "vendor", "ivanna_audio")
            self.assertFalse(os.path.exists(d))
            r = run(d, "ivanna_asrc_link_prepare")
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertTrue(os.path.isdir(d))
            self.assertEqual(mode(d), 0o775)

    def test_prepare_fails_when_directory_cannot_be_created(self):
        with tempfile.TemporaryDirectory() as t:
            blocker = os.path.join(t, "file")
            open(blocker, "w").close()
            r = run(os.path.join(blocker, "sub"), "ivanna_asrc_link_prepare")
            self.assertNotEqual(r.returncode, 0)

    def test_fixperms_opens_daemon_files_for_reading(self):
        with tempfile.TemporaryDirectory() as t:
            d = os.path.join(t, "ia")
            os.makedirs(d)
            for n in ("asrc_state", "clock_snapshot.v2"):
                p = os.path.join(d, n)
                open(p, "w").close()
                os.chmod(p, 0o660)  # lo que deja fchmod(fd, 0660) del daemon
            r = run(d, "ivanna_asrc_link_fixperms")
            self.assertEqual(r.stdout.strip(), "2", r.stderr)
            for n in ("asrc_state", "clock_snapshot.v2"):
                self.assertEqual(mode(os.path.join(d, n)), 0o664)

    def test_fixperms_ignores_missing_files_and_is_idempotent(self):
        with tempfile.TemporaryDirectory() as t:
            self.assertEqual(run(t, "ivanna_asrc_link_fixperms").stdout.strip(), "0")
            self.assertEqual(run(t, "ivanna_asrc_link_fixperms").stdout.strip(), "0")

    def test_service_sh_prepares_dir_before_launching_daemon(self):
        s = open(os.path.join(MOD, "service.sh"), encoding="utf-8").read()
        i_prep = s.index("ivanna_asrc_link_prepare")
        i_launch = s.index('"$DAEMON" --realtime')
        self.assertLess(i_prep, i_launch)
        self.assertIn("ivanna_asrc_link_watch", s[i_launch:])

    def test_paths_match_native_constants(self):
        inc = os.path.join(ROOT, "app", "src", "main", "cpp", "include")
        asrc = open(os.path.join(inc, "asrc_state_bridge.hpp"), encoding="utf-8").read()
        clk = open(os.path.join(inc, "audio_hardware_clock_bridge.hpp"), encoding="utf-8").read()
        native = {os.path.dirname(m) for m in re.findall(r'"(/data/vendor/ivanna_audio/[^"]+)"', asrc + clk)}
        self.assertEqual(native, {"/data/vendor/ivanna_audio"})
        helper = open(HELPER, encoding="utf-8").read()
        self.assertIn("/data/vendor/ivanna_audio", helper)
        for name in ("asrc_state", "clock_snapshot.v2"):
            self.assertIn(name, helper)

    def test_sepolicy_lets_app_traverse_vendor_data(self):
        s = open(os.path.join(MOD, "sepolicy.rule"), encoding="utf-8").read()
        self.assertRegex(s, r"allow untrusted_app vendor_data_file dir \{ search getattr \}")


if __name__ == "__main__":
    unittest.main()
