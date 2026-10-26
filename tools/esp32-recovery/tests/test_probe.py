"""Validate security checks against real pinned ROM definitions without USB."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import types
import unittest
from unittest import mock
from esptool.targets.esp32 import ESP32ROM
from esptool.targets.esp32s3 import ESP32S3ROM

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("esp32_probe", ROOT / "device_probe.py")
probe = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(probe)


def rom(cls, registers=None):
    device = cls.__new__(cls)
    values = registers or {}
    device.secure_download_mode = False
    device.read_reg = lambda address: values.get(address, 0)
    device.get_chip_revision = lambda: 300
    device.read_mac = lambda: (0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc)
    return device


class SecurityProbeTests(unittest.TestCase):
    def test_plain_esp32_and_s3_are_identified(self):
        for cls, expected in ((ESP32ROM, "esp32"), (ESP32S3ROM, "esp32s3")):
            with self.subTest(chip=expected):
                info = probe.inspect_device(rom(cls))
                self.assertEqual(info["esptoolChip"], expected)
                self.assertEqual(info["mac"], "12:34:56:78:9a:bc")
                self.assertEqual(info["security"], {"flashEncryption": False, "secureBoot": False, "secureDownload": False})

    def test_actual_encryption_fuse_readers_block_protected_chips(self):
        for cls in (ESP32ROM, ESP32S3ROM):
            bit = cls.EFUSE_SPI_BOOT_CRYPT_CNT_MASK & -cls.EFUSE_SPI_BOOT_CRYPT_CNT_MASK
            with self.subTest(chip=cls.CHIP_NAME), self.assertRaises(ValueError):
                probe.inspect_device(rom(cls, {cls.EFUSE_SPI_BOOT_CRYPT_CNT_REG: bit}))

    def test_actual_secure_boot_fuse_readers_block_protected_chips(self):
        values = {ESP32ROM.EFUSE_RD_ABS_DONE_REG: ESP32ROM.EFUSE_RD_ABS_DONE_0_MASK}
        with self.assertRaises(ValueError):
            probe.inspect_device(rom(ESP32ROM, values))
        values = {ESP32S3ROM.EFUSE_SECURE_BOOT_EN_REG: ESP32S3ROM.EFUSE_SECURE_BOOT_EN_MASK}
        with self.assertRaises(ValueError):
            probe.inspect_device(rom(ESP32S3ROM, values))

    def test_esp32_secure_boot_v2_flag_is_also_blocked(self):
        values = {ESP32ROM.EFUSE_RD_ABS_DONE_REG: ESP32ROM.EFUSE_RD_ABS_DONE_1_MASK}
        with self.assertRaises(ValueError):
            probe.inspect_device(rom(ESP32ROM, values))

    def test_secure_download_or_unknown_status_is_blocked(self):
        for status in (True, None, 0):
            device = rom(ESP32S3ROM)
            device.secure_download_mode = status
            with self.subTest(status=status), self.assertRaises(ValueError):
                probe.inspect_device(device)

    def test_unknown_security_read_values_are_not_treated_as_unprotected(self):
        device = rom(ESP32ROM)
        device.get_flash_encryption_enabled = lambda: None
        with self.assertRaises(ValueError):
            probe.inspect_device(device)
        device = rom(ESP32ROM)
        device.get_secure_boot_enabled = lambda: None
        with self.assertRaises(ValueError):
            probe.inspect_device(device)

    def test_security_register_read_failure_is_not_ignored(self):
        device = rom(ESP32ROM)
        device.read_reg = mock.Mock(side_effect=RuntimeError("register read denied"))
        with self.assertRaises(RuntimeError):
            probe.inspect_device(device)

    def test_other_chip_families_and_bad_mac_are_blocked(self):
        device = rom(ESP32ROM)
        device.CHIP_NAME = "ESP32-C3"
        with self.assertRaises(ValueError):
            probe.inspect_device(device)
        for mac in ((0, 0, 0, 0, 0, 0), (255,)*6, (1, 2, 3, 4, 5, 6), None):
            device = rom(ESP32ROM)
            device.read_mac = lambda: mac
            with self.subTest(mac=mac), self.assertRaises(ValueError):
                probe.inspect_device(device)

    def test_probe_emits_only_curated_json_and_closes_port(self):
        device = rom(ESP32ROM)
        device._port = mock.Mock()
        def detect(**kwargs):
            print("internal connection noise")
            return device
        output = io.StringIO()
        import esptool
        with mock.patch.object(esptool, "detect_chip", side_effect=detect), contextlib.redirect_stdout(output):
            result = probe.main(["--port", "TEST-NO-USB"])
        self.assertEqual(result, 0)
        self.assertNotIn("connection noise", output.getvalue())
        record = json.loads(output.getvalue().strip().removeprefix(probe.MARKER))
        self.assertEqual(record["chip"], "ESP32")
        device._port.close.assert_called_once()

    def test_image_layout_matches_pinned_rom_definitions(self):
        self.assertEqual(ESP32ROM.BOOTLOADER_FLASH_OFFSET, 0x1000)
        self.assertEqual(ESP32S3ROM.BOOTLOADER_FLASH_OFFSET, 0)
        self.assertEqual(ESP32ROM.IMAGE_CHIP_ID, 0)
        self.assertEqual(ESP32S3ROM.IMAGE_CHIP_ID, 9)


if __name__ == "__main__":
    unittest.main()
