"""Host tests for Magisk boot-health and ELF header guards."""
import os
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BOOT_HELPER = os.path.join(ROOT, "magisk_module/core/ivanna_boot_guard.sh")
ELF_HELPER = os.path.join(ROOT, "magisk_module/core/ivanna_elf_guard.sh")
POST_FS = os.path.join(ROOT, "magisk_module/post-fs-data.sh")
SERVICE = os.path.join(ROOT, "magisk_module/service.sh")


def run_shell(script, *args):
    return subprocess.run(["sh", "-c", script, "sh", *args],
                          capture_output=True, text=True, timeout=15)


def valid_aarch64_so():
    # Minimal ELF64 little-endian AArch64 ET_DYN header + one 56-byte PHDR.
    b = bytearray(120)
    b[0:4] = bytes.fromhex("7f454c46")
    b[4:7] = bytes.fromhex("020101")
    b[16:20] = bytes.fromhex("0300b700")
    b[20:24] = bytes.fromhex("01000000")
    b[32:40] = bytes.fromhex("4000000000000000")  # e_phoff = 64
    b[52:58] = bytes.fromhex("400038000100")
    return bytes(b)


class BootHealthGuardTest(unittest.TestCase):
    def invoke(self, root, now, window=600):
        count = os.path.join(root, "boot_streak")
        stamp = os.path.join(root, "boot_streak_ts")
        marker = os.path.join(root, "last_boot_ok")
        script = (
            f'. "{BOOT_HELPER}"; '
            f'ivanna_boot_guard_update "{count}" "{stamp}" "{marker}" "{now}" "{window}"; '
            'rc=$?; printf "rc=%s streak=%s\\n" "$rc" "${IVANNA_BOOT_STREAK:-unset}"; exit "$rc"'
        )
        return run_shell(script)

    def test_first_unhealthy_boot_starts_at_one(self):
        with tempfile.TemporaryDirectory() as d:
            r = self.invoke(d, 1000)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertIn("streak=1", r.stdout)

    def test_failed_boots_accumulate_inside_crash_window(self):
        with tempfile.TemporaryDirectory() as d:
            self.invoke(d, 1000)
            r = self.invoke(d, 1200)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertIn("streak=2", r.stdout)

    def test_healthy_marker_resets_streak_and_is_consumed(self):
        with tempfile.TemporaryDirectory() as d:
            self.invoke(d, 1000)
            self.invoke(d, 1200)
            marker = os.path.join(d, "last_boot_ok")
            open(marker, "w").close()
            r = self.invoke(d, 1300)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertIn("streak=1", r.stdout)
            self.assertFalse(os.path.exists(marker))

    def test_old_failure_outside_window_starts_new_streak(self):
        with tempfile.TemporaryDirectory() as d:
            self.invoke(d, 1000)
            r = self.invoke(d, 1701, window=600)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertIn("streak=1", r.stdout)

    def test_corrupt_counter_is_recovered_without_arithmetic_error(self):
        with tempfile.TemporaryDirectory() as d:
            open(os.path.join(d, "boot_streak"), "w").write("broken")
            open(os.path.join(d, "boot_streak_ts"), "w").write("broken")
            r = self.invoke(d, 2000)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertIn("streak=1", r.stdout)


class ElfGuardTest(unittest.TestCase):
    def validate(self, data):
        with tempfile.NamedTemporaryFile() as f:
            f.write(data)
            f.flush()
            script = (
                f'. "{ELF_HELPER}"; ivanna_elf_validate "{f.name}"; '
                'rc=$?; printf "rc=%s reason=%s\\n" "$rc" "$IVANNA_ELF_REASON"; exit "$rc"'
            )
            return run_shell(script)

    def test_accepts_aarch64_elf64_shared_object(self):
        r = self.validate(valid_aarch64_so())
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("ELF64 AArch64 ET_DYN header accepted", r.stdout)

    def test_rejects_non_elf_without_deleting_it(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "libomega_effect.so")
            with open(p, "wb") as f:
                f.write(b"not an elf" + bytes(120))
            script = f'. "{ELF_HELPER}"; ivanna_elf_validate "{p}"; rc=$?; test -f "{p}"; exists=$?; printf "rc=%s exists=%s reason=%s\\n" "$rc" "$exists" "$IVANNA_ELF_REASON"; exit 0'
            r = run_shell(script)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertIn("exists=0", r.stdout)
            self.assertIn("invalid ELF magic", r.stdout)

    def test_rejects_32_bit_elf(self):
        b = bytearray(valid_aarch64_so())
        b[4] = 1
        r = self.validate(bytes(b))
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("not 64-bit", r.stdout)

    def test_rejects_big_endian_elf(self):
        b = bytearray(valid_aarch64_so())
        b[5] = 2
        r = self.validate(bytes(b))
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("not little-endian", r.stdout)

    def test_rejects_wrong_machine(self):
        b = bytearray(valid_aarch64_so())
        b[18:20] = bytes.fromhex("3e00")
        r = self.validate(bytes(b))
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("not AArch64", r.stdout)

    def test_rejects_non_shared_object(self):
        b = bytearray(valid_aarch64_so())
        b[16:18] = bytes.fromhex("0200")
        r = self.validate(bytes(b))
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("not a shared object", r.stdout)

    def test_rejects_truncated_header(self):
        r = self.validate(bytes.fromhex("7f454c46") + bytes(20))
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("truncated", r.stdout)

    def test_post_fs_uses_guards_and_never_deletes_bad_library(self):
        text = open(POST_FS, encoding="utf-8").read()
        self.assertIn("ivanna_boot_guard_update", text)
        self.assertIn("ivanna_elf_validate", text)
        self.assertNotRegex(text, r'rm\s+-f\s+"?\$SO')
        self.assertIn("ivanna_omega_last_boot_ok", text)

    def test_service_marks_health_only_after_daemon_and_socket_are_ready(self):
        text = open(SERVICE, encoding="utf-8").read()
        self.assertIn('touch /data/adb/ivanna_omega_last_boot_ok', text)
        self.assertIn('if kill -0 "$PID"', text)
        self.assertIn('[ "$SOCKET_READY" -eq 1 ]', text)


if __name__ == "__main__":
    unittest.main()
