"""Contrato del diagnostico LD_PRELOAD del modulo Magisk (preload_link.sh).

El modulo empaqueta libivanna_pal_presence_probe.so, pero la unica evidencia
valida de que este cargada es el mapa de memoria del proceso del HAL. Estas
pruebas ejecutan el helper real contra un arbol /proc falso (IVANNA_PROC_DIR) y
fijan tambien el limite de honestidad del modulo: ningun script puede exportar
LD_PRELOAD, reiniciar el HAL ni tocar SELinux. No prueban el dispositivo.
"""
import os
import re
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MOD = os.path.join(ROOT, "magisk_module")
HELPER = os.path.join(MOD, "preload_link.sh")
LIB = "libivanna_pal_presence_probe.so"
HAL_CMD = "/vendor/bin/hw/android.hardware.audio.service_64"


def write(path, data, binary=False):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb" if binary else "w") as f:
        f.write(data)


class Sandbox:
    """Arbol /proc falso + directorio de modulo falso, todo en un tmpdir."""

    def __init__(self, root):
        self.root = root
        self.proc = os.path.join(root, "proc")
        self.moddir = os.path.join(root, "module")
        self.vendor = os.path.join(root, "vendor", "lib64", LIB)
        self.report = os.path.join(root, "report.txt")
        os.makedirs(self.proc)

    def install_lib(self):
        write(os.path.join(self.moddir, "system", "vendor", "lib64", LIB), "ELF-fake")

    def process(self, pid, cmdline, maps="", environ=None):
        d = os.path.join(self.proc, str(pid))
        write(os.path.join(d, "cmdline"), cmdline.replace(" ", "\0") + "\0", binary=False)
        write(os.path.join(d, "maps"), maps)
        if environ is not None:
            write(os.path.join(d, "environ"), "\0".join(environ) + "\0")

    def unreadable_maps(self, pid):
        # un directorio hace que grep falle con rc=2, como EACCES sin root
        d = os.path.join(self.proc, str(pid), "maps")
        os.remove(d)
        os.makedirs(d)

    def run(self, body="ivanna_preload_status"):
        env = dict(
            os.environ,
            IVANNA_PROC_DIR=self.proc,
            IVANNA_MODDIR=self.moddir,
            IVANNA_VENDOR_LIB=self.vendor,
            IVANNA_PRELOAD_REPORT=self.report,
        )
        return subprocess.run(["sh", "-c", f'. "{HELPER}"; {body}'], env=env,
                              capture_output=True, text=True, timeout=30)


def fields(stdout):
    out = {}
    for line in stdout.splitlines():
        m = re.match(r"^(verdict|note)=(.*)$", line)
        if m:
            out[m.group(1)] = m.group(2)
    return out


MAPPED = f"7f00-7f01 r-xp 00000000 fd:00 1 /vendor/lib64/{LIB}\n"
UNRELATED = "7f00-7f01 r-xp 00000000 fd:00 1 /vendor/lib64/libc.so\n"


class PreloadStatusTest(unittest.TestCase):
    def sb(self):
        t = tempfile.TemporaryDirectory()
        self.addCleanup(t.cleanup)
        return Sandbox(t.name)

    def test_lib_not_installed(self):
        s = self.sb()
        s.process(100, HAL_CMD, UNRELATED)
        r = s.run()
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(fields(r.stdout)["verdict"], "LIB_NOT_INSTALLED", r.stdout)

    def test_hal_not_running(self):
        s = self.sb()
        s.install_lib()
        s.process(200, "/system/bin/surfaceflinger", UNRELATED)
        r = s.run()
        self.assertEqual(fields(r.stdout)["verdict"], "HAL_NOT_RUNNING", r.stdout)
        self.assertIn("hal_processes=0", r.stdout)

    def test_hal_running_without_library_is_not_loaded(self):
        s = self.sb()
        s.install_lib()
        s.process(100, HAL_CMD, UNRELATED, environ=["PATH=/vendor/bin"])
        r = s.run()
        f = fields(r.stdout)
        self.assertEqual(f["verdict"], "NOT_LOADED", r.stdout)
        self.assertIn("hal_pid=100 mapped=no", r.stdout)
        self.assertIn("early-init", f["note"])

    def test_library_mapped_in_hal_is_loaded(self):
        s = self.sb()
        s.install_lib()
        s.process(100, HAL_CMD, UNRELATED + MAPPED)
        r = s.run()
        self.assertEqual(fields(r.stdout)["verdict"], "LOADED", r.stdout)
        self.assertIn("hal_pid=100 mapped=yes", r.stdout)
        # honestidad: LOADED no se vende como ASRC/PCM activo
        self.assertIn("no prueba PCM", fields(r.stdout)["note"])

    def test_library_in_another_process_does_not_count(self):
        # Regresion de alcance: solo el proceso del HAL decide el veredicto.
        s = self.sb()
        s.install_lib()
        s.process(100, HAL_CMD, UNRELATED)
        s.process(300, "/system/bin/audioserver", MAPPED)
        r = s.run()
        self.assertEqual(fields(r.stdout)["verdict"], "NOT_LOADED", r.stdout)
        self.assertNotIn("hal_pid=300", r.stdout)

    def test_unreadable_maps_asks_for_root_instead_of_guessing(self):
        s = self.sb()
        s.install_lib()
        s.process(100, HAL_CMD, UNRELATED)
        s.unreadable_maps(100)
        r = s.run()
        self.assertEqual(fields(r.stdout)["verdict"], "NEEDS_ROOT", r.stdout)
        self.assertIn("mapped=unreadable", r.stdout)

    def test_one_loaded_pid_among_several_hal_processes(self):
        s = self.sb()
        s.install_lib()
        s.process(100, HAL_CMD, UNRELATED)
        s.process(101, HAL_CMD, MAPPED)
        r = s.run()
        self.assertEqual(fields(r.stdout)["verdict"], "LOADED", r.stdout)
        self.assertIn("hal_processes=2", r.stdout)

    def test_ld_preload_environment_is_reported_and_existing_entries_visible(self):
        s = self.sb()
        s.install_lib()
        both = f"LD_PRELOAD=/vendor/lib64/other.so:/vendor/lib64/{LIB}"
        s.process(100, HAL_CMD, MAPPED, environ=["PATH=/x", both])
        r = s.run()
        self.assertIn(f"ld_preload_env=yes {both}", r.stdout)

    def test_foreign_ld_preload_is_not_credited_to_the_probe(self):
        s = self.sb()
        s.install_lib()
        s.process(100, HAL_CMD, UNRELATED,
                  environ=["LD_PRELOAD=/vendor/lib64/other.so"])
        r = s.run()
        self.assertIn("ld_preload_env=no LD_PRELOAD=/vendor/lib64/other.so", r.stdout)

    def test_missing_environ_is_unreadable_not_no(self):
        s = self.sb()
        s.install_lib()
        s.process(100, HAL_CMD, UNRELATED)  # sin archivo environ
        r = s.run()
        self.assertIn("ld_preload_env=unreadable", r.stdout)

    def test_report_file_presence_is_reported(self):
        s = self.sb()
        s.install_lib()
        s.process(100, HAL_CMD, UNRELATED)
        self.assertIn("report_file=absent", s.run().stdout)
        write(s.report, "=== IVANNA PAL PRELOAD PRESENCE v1 ===\n")
        self.assertIn("report_file=" + s.report, s.run().stdout)
        self.assertIn("PRESENCE v1", s.run("ivanna_preload_report").stdout)

    def test_report_without_file_says_so(self):
        s = self.sb()
        r = s.run("ivanna_preload_report")
        self.assertEqual(r.returncode, 0)
        self.assertIn("sin reporte", r.stdout)

    def test_module_lib_hash_is_published_when_installed(self):
        s = self.sb()
        s.install_lib()
        r = s.run()
        self.assertRegex(r.stdout, r"module_lib_sha256=[0-9a-f]{64}")


class ModuleHonestyContractTest(unittest.TestCase):
    """El modulo no puede fingir inyeccion: init ya creo el HAL antes que service.sh."""

    FORBIDDEN = [
        r"^\s*(export\s+)?LD_PRELOAD=",       # asignar/exportar LD_PRELOAD
        r"setenv\s+LD_PRELOAD",               # stanza de init
        r"setprop\s+ctl\.",                   # reiniciar servicios
        r"\bptrace\b",
        r"process_vm_writev",
        r"setenforce",
    ]

    def module_scripts(self):
        for dirpath, _, files in os.walk(MOD):
            for n in files:
                if n.endswith(".sh"):
                    yield os.path.join(dirpath, n)

    def test_no_module_script_injects_or_restarts_the_hal(self):
        for p in self.module_scripts():
            text = open(p, encoding="utf-8", errors="replace").read()
            for line in text.splitlines():
                if line.lstrip().startswith("#"):
                    continue
                for pat in self.FORBIDDEN:
                    self.assertIsNone(re.search(pat, line),
                                      f"{os.path.relpath(p, ROOT)}: {line.strip()!r}")

    def test_preload_helper_is_read_only(self):
        text = open(HELPER, encoding="utf-8").read()
        code = "\n".join(l for l in text.splitlines() if not l.lstrip().startswith("#"))
        for token in ("setprop", "chmod", "chown", "mkdir", "rm -", "kill", "stop ", "start "):
            self.assertNotIn(token, code, f"preload_link.sh no debe usar {token!r}")
        # escribe en stdout solamente: ninguna redireccion a archivo salvo /dev/null.
        # Se ignoran las cadenas entrecomilladas ("<pid>" en un mensaje no es redireccion).
        unquoted = re.sub(r'"[^"]*"', '""', code)
        for m in re.finditer(r">\s*([^\s&|)]+)", unquoted):
            target = m.group(1)
            self.assertIn(target, ("/dev/null", "&2"), f"redireccion inesperada: {target}")

    def test_control_cli_exposes_preload_command(self):
        text = open(os.path.join(MOD, "ivanna_control.sh"), encoding="utf-8").read()
        self.assertRegex(text, r"(?m)^\s*preload\)")
        self.assertIn("preload_link.sh", text)
        self.assertIn("preload [status|report]", text)


if __name__ == "__main__":
    unittest.main()
