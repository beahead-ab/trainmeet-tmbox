"""The download, not a second source tree, is the product being tested."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parent.parent
SPEC = importlib.util.spec_from_file_location("package_firmware", ROOT / "scripts/package_firmware.py")
package = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(package)


class FirmwarePackageTests(unittest.TestCase):
    def test_arduino_lcd_uses_a_version_in_its_actual_registry(self):
        for family in package.CORES:
            self.assertIn(('LiquidCrystal I2C', '1.1.2'), package.dependencies(ROOT, family))

    def test_one_arduino_entry_and_no_platformio_wrapper(self):
        for profile in package.PROFILES:
            with self.subTest(profile=profile):
                files = package.arduino_files(ROOT, profile)
                entries = [p for p in files if p.endswith('.ino')]
                self.assertEqual(1, len(entries))
                entry = Path(entries[0])
                self.assertEqual(entry.stem, entry.parent.name)
                self.assertFalse(any(p.endswith('main.cpp') for p in files))
                self.assertFalse(any('/src/' in p or '/lib/' in p or '.pio' in p for p in files))
                for name, content in files.items():
                    if name.endswith(('.ino', '.cpp', '.h')):
                        self.assertNotRegex(content.decode(), r'#include\s+["<].*\.(ino|cpp)[">]')

    def test_firmware_body_and_supporting_code_are_not_a_fork(self):
        for profile, (family, sketch, *_rest) in package.PROFILES.items():
            files = package.arduino_files(ROOT, profile)
            source = package.source_file(ROOT, family)
            self.assertEqual(b'#include "TrainMeetBuild.h"\n' + source.read_bytes(), files[f'{sketch}/TrainMeetFirmware.cpp'])
            for path in source.parent.glob('*.h'):
                self.assertEqual(path.read_bytes(), files[f'{sketch}/{path.name}'])
            if family == 'esp32':
                for path in (ROOT / 'firmware/esp32/lib/tmbox_core').iterdir():
                    if path.suffix in ('.h', '.cpp'):
                        self.assertEqual(path.read_bytes(), files[f'{sketch}/{path.name}'])

    def test_hardware_selection_is_explicit(self):
        self.assertIn('FlashSize=8M,PSRAM=enabled', package.PROFILES['esp32-s3'][3])
        for profile, (family, sketch, _board, _fqbn, number) in package.PROFILES.items():
            files = package.arduino_files(ROOT, profile)
            config = files[f'{sketch}/TrainMeetBuild.h'].decode()
            if number:
                self.assertIn(f'#define TMBOX_HARDWARE_PROFILE {number}\n', config)
            self.assertEqual(profile == 'nodemcu-hardware-check', '#define TAMBOX_HARDWARE_CHECK 1' in config)
            yaml = files[f'{sketch}/sketch.yaml'].decode()
            for name, version in package.dependencies(ROOT, family):
                self.assertIn(f'{name} ({version})', yaml)
            self.assertNotIn('default_profile:', yaml, 'IDE-style builds must not silently select CLI isolation')

    def test_platformio_has_only_one_source_entry_and_all_local_headers(self):
        for family in package.CORES:
            files = package.platformio_files(ROOT, family)
            self.assertEqual(['src/main.cpp'], [p for p in files if p.startswith('src/')])
            self.assertIn('platformio.ini', files)
            self.assertNotIn('.git', files)
            self.assertFalse(any('.pio/' in p or 'node_modules' in p for p in files))
            self.assertEqual(1, len([p for p in files if p.endswith('.ino')]))
            self.assertEqual((ROOT / 'firmware' / family / 'src/main.cpp').read_bytes(), files['src/main.cpp'])

    def test_archives_are_reproducible_and_manifest_covers_every_file(self):
        with tempfile.TemporaryDirectory() as directory:
            first = package.build(ROOT, Path(directory) / 'first', 'test-commit')
            second = package.build(ROOT, Path(directory) / 'second', 'test-commit')
            self.assertEqual(7, len(first))
            for a, b in zip(first, second):
                self.assertEqual(a.read_bytes(), b.read_bytes())
                with zipfile.ZipFile(a) as archive:
                    names = archive.namelist()
                    self.assertEqual(len(names), len(set(names)))
                    contents = {str(Path(n).relative_to(a.stem)): archive.read(n) for n in names}
                manifest = json.loads(contents.pop('PACKAGE.json'))
                self.assertEqual('test-commit', manifest['source_revision'])
                self.assertEqual((ROOT / 'VERSION').read_text().strip(), manifest['version'])
                self.assertIn(f"Version {manifest['version']} · källrevision test-commit", contents['START-HERE.md'].decode())
                self.assertEqual({p: hashlib.sha256(c).hexdigest() for p, c in contents.items()}, manifest['files'])
            with self.assertRaises(FileExistsError):
                package.build(ROOT, Path(directory) / 'first', 'other-commit')


if __name__ == '__main__':
    unittest.main()
