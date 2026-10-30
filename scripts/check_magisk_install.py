#!/usr/bin/env python3
"""Exercise the real Magisk XML merge in an isolated synthetic OEM filesystem.

No root, device paths or datasets are touched. BusyBox applets are exercised
when installed; the POSIX host shell always runs. XML assertions use stdlib.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
UUID = '4956414e-4e41-4f4d-4547-415355505245'
SOURCE = (ROOT / 'magisk_module/customize.sh').read_text()
START = '# ── Fusión dinámica de audio_effects.xml'
END = 'ui_print "- Setting permissions on DSP libraries..."'
BLOCK = SOURCE[SOURCE.index(START):SOURCE.index(END)]
CANDIDATES = '/system/vendor/etc/audio_effects.xml /vendor/etc/audio_effects.xml /system/etc/audio_effects.xml'
LIB = '<library name="omega_effect" path="libomega_effect.so"/>'
EFFECT = f'<effect name="omega_effect" library="omega_effect" uuid="{UUID}"/>'


def oem(library='', effect='', postprocess=''):
    return f'''<?xml version="1.0"?>
<audio_effects_conf version="2.0">
<libraries>
<library name="oem" path="liboem.so"/>
{library}
</libraries>
<effects>
<effect name="oem" library="oem" uuid="oem-uuid"/>
{effect}
</effects>
{postprocess}
</audio_effects_conf>
'''


class MagiskMerge(unittest.TestCase):
    def run_merge(self, xml, succeeds=True, repeat=False):
        engines = ['posix']
        if shutil.which('busybox'):
            engines.append('busybox')
        for engine in engines:
            with self.subTest(engine=engine), tempfile.TemporaryDirectory() as directory:
                base = Path(directory)
                fixture = base / 'oem.xml'
                fixture.write_text(xml)
                module = base / 'module'
                environment = dict(os.environ)
                command = ['sh']
                if engine == 'busybox':
                    applets = base / 'applets'
                    applets.mkdir()
                    for applet in ('sh', 'awk', 'sed', 'grep', 'dirname', 'mkdir', 'cp', 'rm', 'mv'):
                        (applets / applet).symlink_to(shutil.which('busybox'))
                    environment['PATH'] = str(applets)
                    command = [str(applets / 'sh')]
                script = '''ui_print() { printf '%s\\n' "$*"; }
abort() { ui_print "$*"; exit 1; }
MODPATH="$1"
''' + BLOCK.replace(CANDIDATES, '"$2"')
                result = subprocess.run(command + ['-c', script, 'test', str(module), str(fixture)],
                                        env=environment, text=True, capture_output=True, timeout=15)
                self.assertEqual(fixture.read_text(), xml, 'OEM source was modified')
                if not succeeds:
                    self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                    continue
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                target = Path(str(module) + str(fixture))
                merged = ET.fromstring(target.read_text())
                libraries = merged.findall('./libraries/library')
                effects = merged.findall('./effects/effect')
                self.assertEqual([e.attrib for e in libraries if e.get('name') == 'omega_effect'],
                                 [{'name': 'omega_effect', 'path': 'libomega_effect.so'}])
                self.assertEqual([e.attrib for e in effects if e.get('name') == 'omega_effect'],
                                 [{'name': 'omega_effect', 'library': 'omega_effect', 'uuid': UUID}])
                self.assertIn({'name': 'oem', 'path': 'liboem.so'}, [e.attrib for e in libraries])
                self.assertIn({'name': 'oem', 'library': 'oem', 'uuid': 'oem-uuid'}, [e.attrib for e in effects])
                for stream in ('music', 'movie', 'game'):
                    applies = merged.findall(f'./postprocess/stream[@type="{stream}"]/apply')
                    self.assertEqual(sum(e.get('effect') == 'omega_effect' for e in applies), 1)
                if 'effect="oem"' in xml:
                    self.assertIsNotNone(merged.find('./postprocess/stream[@type="music"]/apply[@effect="oem"]'))
                if repeat:
                    fixture.write_text(target.read_text())
                    second = subprocess.run(command + ['-c', script, 'test', str(module), str(fixture)],
                                            env=environment, text=True, capture_output=True, timeout=15)
                    self.assertEqual(second.returncode, 0, second.stdout + second.stderr)
                    self.assertEqual(ET.canonicalize(target.read_text(), strip_text=True),
                                     ET.canonicalize(ET.tostring(merged, encoding='unicode'), strip_text=True))

    def test_new_registration_and_idempotence(self):
        self.run_merge(oem(), repeat=True)

    def test_existing_single_line_registration(self):
        self.run_merge(oem(LIB, EFFECT), repeat=True)

    def test_uuid_on_name_line_library_on_next_line(self):
        # Reproduces the exact screenshot failure in v2.5.194.
        self.run_merge(oem(LIB, f'<effect name="omega_effect" uuid="{UUID}"\n library="omega_effect"/>'))

    def test_all_attributes_on_separate_lines(self):
        self.run_merge(oem(LIB.replace(' path=', '\n path='), EFFECT.replace(' library=', '\n library=').replace(' uuid=', '\n uuid=')))

    def test_reordered_attributes(self):
        self.run_merge(oem('<library path="libomega_effect.so"\n name="omega_effect"/>',
                           f'<effect uuid="{UUID}"\n library="omega_effect" name="omega_effect"/>'))

    def test_only_library_exists(self):
        self.run_merge(oem(LIB))

    def test_only_effect_exists(self):
        self.run_merge(oem(effect=EFFECT))

    def test_comments_do_not_register_effects(self):
        self.run_merge(oem('<!-- ' + LIB + ' -->', '<!--\n' + EFFECT + '\n-->'))

    def test_oem_postprocess_is_preserved(self):
        self.run_merge(oem(postprocess='<postprocess>\n<stream type="music"><apply effect="oem"/></stream>\n</postprocess>'), repeat=True)

    def test_attribute_names_are_matched_exactly(self):
        self.run_merge(oem('<library othername="omega_effect" name="other" path="libother.so"/>',
                           f'<effect othername="omega_effect" name="other" library="other" uuid="{UUID}"/>'))

    def test_wrong_uuid_is_rejected(self):
        self.run_merge(oem(LIB, EFFECT.replace(UUID, 'wrong-uuid')), succeeds=False)

    def test_wrong_library_path_is_rejected(self):
        self.run_merge(oem(LIB.replace('libomega_effect.so', 'libwrong.so'), EFFECT), succeeds=False)

    def test_wrong_effect_library_is_rejected(self):
        self.run_merge(oem(LIB, EFFECT.replace('library="omega_effect"', 'library="wrong"')), succeeds=False)

    def test_conflicting_duplicate_is_rejected(self):
        self.run_merge(oem(LIB, EFFECT + '\n' + EFFECT.replace(UUID, 'wrong-uuid')), succeeds=False)

    def test_missing_sections_are_rejected(self):
        self.run_merge('<audio_effects_conf version="2.0"/>', succeeds=False)

    def test_unterminated_comment_is_rejected(self):
        self.run_merge(oem() + '<!--', succeeds=False)


class DaemonArchCheck(unittest.TestCase):
    """The installer reads the ELF header itself (toybox `file` does not print the arch)."""
    A = '        ARCH_OK=0'
    B = '        set_perm "$DAEMON" root root 0755'
    SNIPPET = SOURCE[SOURCE.index(A):SOURCE.index(B)]

    def verdict(self, binary):
        script = 'DAEMON="$1"\n' + self.SNIPPET + 'echo "$ARCH_OK"\n'
        out = subprocess.run(['sh', '-c', script, '_', str(binary)], capture_output=True, text=True, check=True)
        return out.stdout.strip()

    def test_shipped_daemon_is_accepted_as_aarch64(self):
        self.assertEqual(self.verdict(ROOT / 'magisk_module/system/bin/ivanna_daemon'), '1')

    def test_foreign_architecture_is_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            x86 = Path(d) / 'x86'
            x86.write_bytes(b'\x7fELF\x02\x01\x01' + b'\0' * 11 + b'\x3e\x00' + b'\0' * 64)
            self.assertEqual(self.verdict(x86), '0')

    def test_installer_does_not_depend_on_file_utility(self):
        self.assertNotIn('file "$DAEMON"', SOURCE)

    def test_hal_probe_is_shipped_and_exposed_by_the_cli(self):
        self.assertTrue((ROOT / 'magisk_module/system/bin/ivanna_hal_probe').is_file())
        cli = (ROOT / 'magisk_module/ivanna_control.sh').read_text()
        self.assertIn('GET_CLOCK_TELEMETRY', cli)
        self.assertIn('hal-probe)', cli)


if __name__ == '__main__':
    print('Magisk merge engines: POSIX sh' + (' + BusyBox' if shutil.which('busybox') else ' (BusyBox unavailable)'), flush=True)
    unittest.main()
