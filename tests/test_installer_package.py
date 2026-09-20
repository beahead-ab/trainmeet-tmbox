import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('package_installer', ROOT / 'scripts/package_installer.py')
packager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packager)


class InstallerPackageTest(unittest.TestCase):
    def setUp(self):
        self.work = tempfile.TemporaryDirectory()
        self.addCleanup(self.work.cleanup)
        self.root = Path(self.work.name)
        self.output = self.root / 'site'
        self.output.mkdir()
        (self.output / 'index.html').write_text('<!doctype html>')
        (self.output / 'web-tools.js').write_text('// fixture')
        self.commit = 'a' * 40
        self.builds = {}
        for profile, chip in packager.PROFILES.items():
            folder = self.root / profile
            folder.mkdir()
            payload = b'\xe9' + b'\0' * 1023
            (folder / 'firmware.bin').write_bytes(payload)
            parts = [{'offset': 0, 'name': 'firmware.bin'}] if chip == 'ESP8266' else [
                {'offset': offset, 'name': name} for offset, name in
                [(0, 'bootloader.bin'), (32768, 'partitions.bin'), (57344, 'boot_app0.bin'), (65536, 'firmware.bin')]]
            (folder / 'build.json').write_text(json.dumps({
                'id': profile, 'chipFamily': chip, 'hardwareTested': False,
                'offset': 0, 'version': '0.3.2', 'sourceCommit': self.commit,
                'bytes': len(payload), 'sha256': hashlib.sha256(payload).hexdigest(), 'sourceParts': parts,
            }))
            self.builds[profile] = folder

    def package(self):
        return packager.package(self.output, self.builds, '0.3.2', self.commit)

    def alter(self, key, value):
        path = self.builds['esp32-s3'] / 'build.json'
        metadata = json.loads(path.read_text())
        metadata[key] = value
        path.write_text(json.dumps(metadata))

    def test_both_real_images_get_separate_chip_manifests(self):
        catalog = self.package()
        self.assertEqual(2, len(catalog['profiles']))
        for profile in catalog['profiles']:
            manifest = json.loads((self.output / profile['manifest']).read_text())
            self.assertEqual(profile['chipFamily'], manifest['builds'][0]['chipFamily'])
            self.assertEqual(0, manifest['builds'][0]['parts'][0]['offset'])
            self.assertTrue(manifest['new_install_prompt_erase'])
            self.assertFalse(manifest['builds'][0]['improv'])
            self.assertEqual(0, manifest['new_install_improv_wait_time'])
            self.assertTrue((self.output / profile['image']).is_file())
        self.assertEqual(2, len((self.output / 'SHA256SUMS').read_text().splitlines()))

    def test_mismatched_versions_are_not_published(self):
        self.alter('version', '0.3.1')
        with self.assertRaisesRegex(ValueError, 'version'): self.package()
        self.assertFalse((self.output / 'catalog.json').exists())

    def test_stale_source_build_is_not_published(self):
        self.alter('sourceCommit', 'b' * 40)
        with self.assertRaisesRegex(ValueError, 'sourceCommit'): self.package()

    def test_wrong_chip_is_rejected(self):
        self.alter('chipFamily', 'ESP32')
        with self.assertRaisesRegex(ValueError, 'chipFamily'): self.package()

    def test_corrupt_or_incomplete_download_is_rejected(self):
        (self.builds['esp32-s3'] / 'firmware.bin').write_bytes(b'\xe9' + b'\1' * 1023)
        with self.assertRaisesRegex(ValueError, 'checksum'): self.package()

    def test_no_fake_success_when_firmware_is_missing(self):
        (self.builds['nodemcu-i2c'] / 'firmware.bin').unlink()
        with self.assertRaises(FileNotFoundError): self.package()

    def test_application_only_esp32_binary_is_rejected(self):
        self.alter('sourceParts', [{'offset': 65536, 'name': 'firmware.bin'}])
        with self.assertRaisesRegex(ValueError, 'offset zero'): self.package()

    def test_hardware_verification_cannot_be_claimed(self):
        self.alter('hardwareTested', True)
        with self.assertRaisesRegex(ValueError, 'hardwareTested'): self.package()

    def test_no_website_without_local_flasher_bundle(self):
        (self.output / 'web-tools.js').unlink()
        with self.assertRaisesRegex(ValueError, 'web assets'): self.package()

    def test_pins_in_the_guide_match_source_profiles(self):
        model = (ROOT / 'installer/model.js').read_text()
        node = (ROOT / 'firmware/esp8266/TrainMeetTambox8266/hardware_profile.h').read_text()
        self.assertIn('D2 / GPIO4', model)
        self.assertIn('D1 / GPIO5', model)
        self.assertIn('TAMBOX_SDA = 4', node)
        self.assertIn('TAMBOX_SCL = 5', node)


if __name__ == '__main__':
    unittest.main()
