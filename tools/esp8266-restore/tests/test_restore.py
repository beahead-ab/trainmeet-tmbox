"""The production restore transaction, with simulated flash only. Never accesses USB."""
import contextlib
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT.parent / "esp8266-backup"))
SPEC = importlib.util.spec_from_file_location("restore_tool", ROOT / "restore_esp8266.py")
tool = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(tool)
SIZE = 256 * 1024
MAC = "12:34:56:78:9a:bc"


def payload(value):
    return bytes([0xE9]) + bytes([value]) * (SIZE - 1)


class FakeBox:
    def __init__(self):
        self.data = payload(7)
        self.mac = MAC
        self.calls = []
        self.written = []
        self.fail_write = False
        self.fail_restore_verify = False
        self.fail_rescue_verify = False
        self.missing_verify_message = False
        self.interrupt_write = False
        self.wrong_chip = False

    def __call__(self, command, **kwargs):
        self.calls.append(command)
        assert command[command.index("--chip") + 1] == "esp8266"
        assert command[command.index("--after") + 1] == "no_reset"
        operation = next(x for x in command if x in {"flash_id", "read_flash", "verify_flash", "write_flash"})
        code = 0
        if operation == "flash_id":
            if self.wrong_chip:
                return subprocess.CompletedProcess(command, 2, "", "This chip is ESP32, not ESP8266")
            output = "Chip is ESP8266EX\nMAC: " + self.mac + "\nDetected flash size: " + str(len(self.data) // 1024) + "KB\n"
        elif operation == "read_flash":
            self.assert_whole_read(command)
            Path(command[-1]).write_bytes(self.data)
            output = "Read complete"
        elif operation == "write_flash":
            for flag in ("--flash_mode", "--flash_freq", "--flash_size"):
                assert command[command.index(flag) + 1] == "keep"
            assert "--erase-all" not in command and "--force" not in command
            assert command[-2] == "0"
            data = Path(command[-1]).read_bytes()
            self.written.append(data)
            if self.interrupt_write:
                self.data = data[:4096] + self.data[4096:]
                raise KeyboardInterrupt()
            if self.fail_write:
                self.data = data[:4096] + self.data[4096:]
                return subprocess.CompletedProcess(command, 2, "connection lost", "")
            self.data = data
            output = "Write complete"
        else:
            expected = Path(command[-1]).read_bytes()
            if (expected != self.data or
                    (self.written and self.fail_restore_verify) or
                    (not self.written and self.fail_rescue_verify)):
                code, output = 2, "-- verify FAILED (digest mismatch)"
            else:
                output = "No confirmation" if self.written and self.missing_verify_message else "-- verify OK (digest matched)\n"
        return subprocess.CompletedProcess(command, code, output, "")

    @staticmethod
    def assert_whole_read(command):
        assert command[-3:-1] == ["0", "ALL"]


class RestoreTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="restore-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "source"
        self.source.mkdir()
        self.original = payload(3)
        self.metadata = {
            "schema": 1, "chip": "ESP8266", "backupFile": "esp8266-full-backup.bin",
            "verifiedAgainstDevice": True, "mac": MAC, "flashBytes": SIZE, "bytes": SIZE,
            "sha256": hashlib.sha256(self.original).hexdigest(), "createdAt": "2026-10-26T02:00:00+01:00",
        }
        self.save_source()
        self.box = FakeBox()
        self.output = self.root / "results"

    def save_source(self):
        (self.source / "esp8266-full-backup.bin").write_bytes(self.original)
        (self.source / "backup-info.json").write_text(json.dumps(self.metadata), encoding="utf-8")
        (self.source / "SHA256SUMS.txt").write_text(self.metadata["sha256"] + "  esp8266-full-backup.bin\n")

    def execute(self, confirm=lambda text: "ATERSTALL"):
        image = tool.load_backup(self.source)
        device = tool.common.Device(sys.executable, "TEST-NO-REAL-USB", runner=self.box)
        with contextlib.redirect_stdout(io.StringIO()):
            return tool.restore(image, device, self.output, confirm=confirm)

    def result_file(self):
        return next(self.output.glob("Aterstallningsrapport-*/resultat.json"))

    def test_success_restores_whole_image_and_preserves_rescue_and_source(self):
        previous = self.box.data
        result = self.execute()
        self.assertTrue(result["writeStarted"])
        self.assertTrue(result["verifiedAgainstDevice"])
        self.assertEqual(self.box.data, self.original)
        self.assertEqual(len(self.box.written), 1)
        self.assertEqual((self.source / "esp8266-full-backup.bin").read_bytes(), self.original)
        rescue = Path(result["rescueBackupFolder"])
        self.assertEqual((rescue / "esp8266-full-backup.bin").read_bytes(), previous)
        self.assertTrue(json.loads((rescue / "backup-info.json").read_text())["verifiedAgainstDevice"])
        self.assertTrue(json.loads(self.result_file().read_text())["verifiedAgainstDevice"])

    def test_wrong_box_cannot_write(self):
        self.box.mac = "aa:bb:cc:dd:ee:ff"
        with self.assertRaises(tool.RestoreError) as caught:
            self.execute()
        self.assertFalse(caught.exception.state["writeStarted"])
        self.assertEqual(self.box.written, [])

    def test_wrong_chip_cannot_write(self):
        self.box.wrong_chip = True
        with self.assertRaises(tool.RestoreError):
            self.execute()
        self.assertEqual(self.box.written, [])

    def test_wrong_flash_size_cannot_write(self):
        self.box.data += self.box.data
        with self.assertRaises(tool.RestoreError):
            self.execute()
        self.assertEqual(self.box.written, [])

    def test_cancel_leaves_current_flash_unchanged(self):
        previous = self.box.data
        result = self.execute(confirm=lambda text: "nej")
        self.assertEqual(result["status"], "cancelled_before_write")
        self.assertFalse(result["verifiedAgainstDevice"])
        self.assertEqual(self.box.data, previous)
        self.assertEqual(self.box.written, [])

    def test_failed_rescue_backup_prevents_write_and_confirmation(self):
        self.box.fail_rescue_verify = True
        confirm = mock.Mock(return_value="ATERSTALL")
        with self.assertRaises(tool.RestoreError):
            self.execute(confirm)
        confirm.assert_not_called()
        self.assertEqual(self.box.written, [])

    def test_box_changed_at_confirmation_is_rejected(self):
        def confirm(text):
            self.box.mac = "aa:bb:cc:dd:ee:ff"
            return "ATERSTALL"
        with self.assertRaises(tool.RestoreError):
            self.execute(confirm)
        self.assertEqual(self.box.written, [])

    def test_box_changed_during_rescue_is_rejected(self):
        original_runner = self.box.__call__
        count = 0
        def runner(command, **kwargs):
            nonlocal count
            if "flash_id" in command:
                count += 1
                if count == 2:
                    self.box.mac = "aa:bb:cc:dd:ee:ff"
            return original_runner(command, **kwargs)
        image = tool.load_backup(self.source)
        device = tool.common.Device(sys.executable, "TEST", runner=runner)
        confirm = mock.Mock(return_value="ATERSTALL")
        with contextlib.redirect_stdout(io.StringIO()), self.assertRaises(tool.RestoreError):
            tool.restore(image, device, self.output, confirm)
        confirm.assert_not_called()
        self.assertEqual(self.box.written, [])

    def test_original_file_changed_after_validation_does_not_change_written_bytes(self):
        def confirm(text):
            (self.source / "esp8266-full-backup.bin").write_bytes(payload(99))
            return "ATERSTALL"
        result = self.execute(confirm)
        self.assertTrue(result["verifiedAgainstDevice"])
        self.assertEqual(self.box.written, [self.original])

    def test_failed_write_is_never_approved(self):
        self.box.fail_write = True
        with self.assertRaises(tool.RestoreError) as caught:
            self.execute()
        self.assertTrue(caught.exception.state["writeStarted"])
        self.assertFalse(caught.exception.state["verifiedAgainstDevice"])
        receipt = json.loads(self.result_file().read_text())
        self.assertFalse(receipt["verifiedAgainstDevice"])
        self.assertTrue(Path(receipt["rescueBackupFolder"]).is_dir())
        self.assertEqual(len(self.box.written), 1)  # never retries a write silently

    def test_interrupted_write_keeps_phase_information(self):
        self.box.interrupt_write = True
        with self.assertRaises(tool.RestoreError) as caught:
            self.execute()
        self.assertTrue(caught.exception.state["writeStarted"])
        self.assertFalse(caught.exception.state["verifiedAgainstDevice"])
        self.assertTrue(Path(caught.exception.state["rescueBackupFolder"]).is_dir())

    def test_failed_post_write_verification_is_not_success(self):
        self.box.fail_restore_verify = True
        with self.assertRaises(tool.RestoreError) as caught:
            self.execute()
        self.assertTrue(caught.exception.state["writeStarted"])
        self.assertFalse(caught.exception.state["verifiedAgainstDevice"])

    def test_zero_exit_without_verification_message_is_not_success(self):
        self.box.missing_verify_message = True
        with self.assertRaises(tool.RestoreError):
            self.execute()
        self.assertFalse(json.loads(self.result_file().read_text())["verifiedAgainstDevice"])

    def test_damaged_file_is_rejected(self):
        (self.source / "esp8266-full-backup.bin").write_bytes(payload(88))
        with self.assertRaises(tool.RestoreError):
            tool.load_backup(self.source)

    def test_truncated_file_is_rejected(self):
        (self.source / "esp8266-full-backup.bin").write_bytes(self.original[:-1])
        with self.assertRaises(tool.RestoreError):
            tool.load_backup(self.source)

    def test_invalid_metadata_is_rejected(self):
        cases = [
            {"schema": 2}, {"schema": True}, {"chip": "ESP32"}, {"verifiedAgainstDevice": False},
            {"verifiedAgainstDevice": "true"}, {"mac": None}, {"mac": "wrong"},
            {"flashBytes": True}, {"flashBytes": 0}, {"flashBytes": 3 * 1024 * 1024},
            {"flashBytes": 32 * 1024 * 1024}, {"bytes": SIZE - 1},
            {"backupFile": "../outside.bin"}, {"sha256": "bad"},
        ]
        original = dict(self.metadata)
        for changes in cases:
            with self.subTest(changes=changes):
                self.metadata = dict(original, **changes)
                self.save_source()
                with self.assertRaises(tool.RestoreError):
                    tool.load_backup(self.source)

    def test_invalid_json_and_non_object_metadata_are_rejected(self):
        for content in ("{", "[]", '"string"'):
            with self.subTest(content=content):
                (self.source / "backup-info.json").write_text(content)
                with self.assertRaises(tool.RestoreError):
                    tool.load_backup(self.source)

    def test_sidecar_checksum_must_also_match(self):
        (self.source / "SHA256SUMS.txt").write_text("0" * 64 + "  esp8266-full-backup.bin\n")
        with self.assertRaises(tool.RestoreError):
            tool.load_backup(self.source)

    def test_update_file_or_blank_flash_is_rejected_even_with_matching_hash(self):
        self.original = bytes(SIZE)
        self.metadata["sha256"] = hashlib.sha256(self.original).hexdigest()
        self.save_source()
        with self.assertRaises(tool.RestoreError):
            tool.load_backup(self.source)

    def test_missing_files_are_rejected(self):
        (self.source / "SHA256SUMS.txt").unlink()
        with self.assertRaises(tool.RestoreError):
            tool.load_backup(self.source)

    def test_invalid_backup_is_rejected_before_a_device_is_constructed(self):
        (self.source / "SHA256SUMS.txt").unlink()
        with mock.patch.object(tool.common, "Device") as device, contextlib.redirect_stdout(io.StringIO()):
            result = tool.main(["--worker", "--backup", str(self.source), "--port", "NEVER-OPEN"])
        self.assertEqual(result, 1)
        device.assert_not_called()

    def test_parent_interruption_does_not_claim_no_write_started(self):
        output = io.StringIO()
        with mock.patch.object(tool.common, "prepare_runtime", return_value=Path(sys.executable)), \
             mock.patch.object(tool.subprocess, "run", side_effect=KeyboardInterrupt), \
             contextlib.redirect_stdout(output):
            result = tool.main([])
        self.assertEqual(result, 130)
        self.assertIn("ofullständig", output.getvalue())
        self.assertNotIn("Ingen återställning har startat", output.getvalue())

    def test_loader_does_not_modify_original_files(self):
        before = {p.name: p.read_bytes() for p in self.source.iterdir()}
        tool.load_backup(self.source)
        self.assertEqual(before, {p.name: p.read_bytes() for p in self.source.iterdir()})


class LauncherTests(unittest.TestCase):
    def test_native_launcher_from_directory_with_spaces_and_swedish_letters(self):
        with tempfile.TemporaryDirectory(prefix="TMBox restore å ä spaces-") as directory:
            folder = Path(directory)
            for name in ("restore_esp8266.py", "Aterstall-ESP8266-Mac.command", "Aterstall-ESP8266-Windows.cmd"):
                shutil.copyfile(ROOT / name, folder / name)
            shutil.copyfile(ROOT.parent / "esp8266-backup" / "backup_esp8266.py", folder / "backup_esp8266.py")
            environment = dict(os.environ, TMBOX_BACKUP_NO_PAUSE="1")
            if os.name == "nt":
                launcher = folder / "Aterstall-ESP8266-Windows.cmd"
                command = 'cmd /d /c ""' + str(launcher) + '" --help"'
            else:
                command = ["bash", str(folder / "Aterstall-ESP8266-Mac.command"), "--help"]
            result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8",
                                    errors="replace", env=environment, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("--backup", result.stdout)
            self.assertNotIn("KLART", result.stdout)


class PackageTests(unittest.TestCase):
    def test_download_is_standalone_and_has_integrity_information(self):
        spec = importlib.util.spec_from_file_location("restore_packager", ROOT / "package.py")
        packager = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(packager)
        with tempfile.TemporaryDirectory() as directory:
            archive = packager.package(Path(directory), "1" * 40)
            with zipfile.ZipFile(archive) as z:
                self.assertIsNone(z.testzip())
                prefix = z.namelist()[0].split("/")[0] + "/"
                for name in ("restore_esp8266.py", "backup_esp8266.py", "Aterstall-ESP8266-Mac.command",
                             "Aterstall-ESP8266-Windows.cmd", "LAS-MIG.txt"):
                    self.assertIn(prefix + name, z.namelist())
                self.assertTrue(z.getinfo(prefix + "Aterstall-ESP8266-Mac.command").external_attr >> 16 & stat.S_IXUSR)
                for line in z.read(prefix + "SHA256SUMS.txt").decode().splitlines():
                    digest, relative = line.split("  ", 1)
                    self.assertEqual(hashlib.sha256(z.read(prefix + relative)).hexdigest(), digest)


if __name__ == "__main__":
    unittest.main()
