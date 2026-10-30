"""Exercise the real backup code using fake serial/flash I/O. No USB is accessed."""
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
import types
import unittest
from unittest import mock
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("backup_tool", ROOT / "backup_esp8266.py")
tool = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(tool)


class FakeFlash:
    def __init__(self, size=256 * 1024, bad_reads=0, bad_verifies=0, short=False, wrong_chip=False):
        self.data = bytes(range(256)) * (size // 256)
        self.bad_reads, self.bad_verifies = bad_reads, bad_verifies
        self.short, self.wrong_chip = short, wrong_chip
        self.calls = []

    def __call__(self, command, **kwargs):
        self.calls.append(command)
        assert command[command.index("--chip") + 1] == "esp8266"
        assert command[command.index("--after") + 1] == "no_reset"
        operation = next(x for x in command if x in tool.ALLOWED_OPERATIONS)
        code = 0
        if operation == "flash_id":
            if self.wrong_chip:
                return subprocess.CompletedProcess(command, 2, "", "This chip is ESP32, not ESP8266")
            output = "Chip is ESP8266EX\nMAC: 12:34:56:78:9a:bc\nDetected flash size: " + str(len(self.data) // 1024) + "KB\n"
        elif operation == "read_flash":
            assert command[-3:-1] == ["0", "ALL"]
            Path(command[-1]).write_bytes(self.data[:-1] if self.short else self.data)
            output = "Read complete"
            if self.bad_reads:
                self.bad_reads -= 1
                code = 2
        else:
            actual = Path(command[-1]).read_bytes()
            if self.bad_verifies or actual != self.data:
                self.bad_verifies = max(0, self.bad_verifies - 1)
                code, output = 2, "-- verify FAILED (digest mismatch)"
            else:
                output = "-- verify OK (digest matched)\n"
        return subprocess.CompletedProcess(command, code, output, "")


class BackupTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="backup-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def execute(self, fake):
        device = tool.Device(sys.executable, "TEST-NO-REAL-USB", runner=fake)
        with contextlib.redirect_stdout(io.StringIO()):
            return tool.backup(device, self.root)

    def test_full_four_mb_backup_is_verified_and_has_private_manifest(self):
        fake = FakeFlash(4 * 1024 * 1024)
        folder = self.execute(fake)
        info = json.loads((folder / "backup-info.json").read_text())
        binary = folder / info["backupFile"]
        self.assertEqual(binary.read_bytes(), fake.data)
        self.assertEqual(info["flashBytes"], 4194304)
        self.assertEqual(info["bytes"], 4194304)
        self.assertEqual(info["sha256"], hashlib.sha256(fake.data).hexdigest())
        self.assertTrue(info["verifiedAgainstDevice"])
        self.assertEqual(info["flashWriteOperations"], 0)
        self.assertFalse((folder / "EJ-GODKAND.txt").exists())
        self.assertFalse((folder / "OFULLSTANDIG-backup.bin").exists())
        if os.name != "nt":
            self.assertEqual(stat.S_IMODE(folder.stat().st_mode), 0o700)
            self.assertEqual(stat.S_IMODE(binary.stat().st_mode), 0o600)

    def test_previous_backups_are_never_overwritten(self):
        a = self.execute(FakeFlash())
        original = (a / "esp8266-full-backup.bin").read_bytes()
        b = self.execute(FakeFlash())
        self.assertNotEqual(a, b)
        self.assertEqual((a / "esp8266-full-backup.bin").read_bytes(), original)

    def test_short_read_is_not_marked_successful(self):
        with self.assertRaises(tool.BackupError):
            self.execute(FakeFlash(short=True))
        folder = next(self.root.iterdir())
        self.assertTrue((folder / "EJ-GODKAND.txt").exists())
        self.assertFalse((folder / "backup-info.json").exists())
        self.assertFalse((folder / "esp8266-full-backup.bin").exists())

    def test_digest_mismatch_is_not_marked_successful(self):
        with self.assertRaises(tool.BackupError):
            self.execute(FakeFlash(bad_verifies=2))
        folder = next(self.root.iterdir())
        self.assertTrue((folder / "EJ-GODKAND.txt").exists())
        self.assertFalse((folder / "backup-info.json").exists())
        self.assertFalse((folder / "esp8266-full-backup.bin").exists())

    def test_failed_read_retries_entire_flash_at_lower_speed(self):
        fake = FakeFlash(bad_reads=1)
        folder = self.execute(fake)
        reads = [c for c in fake.calls if "read_flash" in c]
        self.assertEqual(len(reads), 2)
        self.assertEqual([c[c.index("--baud") + 1] for c in reads], ["460800", "115200"])
        self.assertEqual((folder / "esp8266-full-backup.bin").read_bytes(), fake.data)

    def test_failed_verification_also_requires_new_full_read(self):
        fake = FakeFlash(bad_verifies=1)
        self.execute(fake)
        self.assertEqual(sum("read_flash" in c for c in fake.calls), 2)
        self.assertEqual(sum("verify_flash" in c for c in fake.calls), 2)

    def test_wrong_chip_stops_before_flash_read(self):
        fake = FakeFlash(wrong_chip=True)
        with self.assertRaises(tool.BackupError):
            self.execute(fake)
        self.assertEqual(len(fake.calls), 1)

    def test_write_and_erase_operations_are_rejected_before_running(self):
        fake = FakeFlash()
        device = tool.Device(sys.executable, "TEST", runner=fake)
        for command in ("write_flash", "erase_flash", "erase_region", "run"):
            with self.assertRaises(tool.BackupError):
                device.call(command)
        self.assertEqual(fake.calls, [])

    def test_unknown_or_impossible_flash_size_is_rejected(self):
        for text in ("Detected flash size: Unknown", "Detected flash size: 0MB",
                     "Detected flash size: 3MB", "Detected flash size: 32MB", ""):
            with self.subTest(text=text), self.assertRaises(tool.BackupError):
                tool.flash_size(text)

    def test_supported_size_formats(self):
        self.assertEqual(tool.flash_size("Detected flash size: 4 MB"), 4194304)
        self.assertEqual(tool.flash_size("Detected flash size: 512KB"), 524288)

    def test_success_exit_without_explicit_verification_is_not_enough(self):
        fake = FakeFlash()
        def runner(command, **kwargs):
            result = fake(command, **kwargs)
            if "verify_flash" in command:
                result.stdout = "connection closed"
            return result
        with self.assertRaises(tool.BackupError):
            self.execute(runner)
        self.assertFalse(any(self.root.glob("*/backup-info.json")))

    def test_cached_runtime_does_not_install_dependencies(self):
        folder = self.root / "runtime"
        executable = folder / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
        executable.parent.mkdir(parents=True)
        executable.touch()
        with mock.patch.object(tool, "runtime_path", return_value=folder), \
             mock.patch.object(tool.subprocess, "run", return_value=types.SimpleNamespace(returncode=0)) as run, \
             mock.patch.object(tool.venv, "EnvBuilder") as builder:
            self.assertEqual(tool.prepare_runtime(), executable)
            builder.assert_not_called()
            self.assertEqual(run.call_count, 1)

    def test_first_install_targets_only_private_venv_and_pinned_version(self):
        folder = self.root / "runtime"
        with mock.patch.object(tool, "runtime_path", return_value=folder), \
             mock.patch.object(tool.subprocess, "run", return_value=types.SimpleNamespace(returncode=0)) as run, \
             mock.patch.object(tool.venv, "EnvBuilder") as builder, \
             contextlib.redirect_stdout(io.StringIO()):
            executable = tool.prepare_runtime()
            builder.return_value.create.assert_called_once_with(folder)
            install = run.call_args_list[0].args[0]
            self.assertEqual(install[0], str(executable))
            self.assertIn("esptool==4.11.0", install)

    def test_timeout_is_a_failed_read(self):
        def timeout(command, **kwargs):
            raise subprocess.TimeoutExpired(command, 3600)
        device = tool.Device(sys.executable, "TEST", runner=timeout)
        with self.assertRaises(tool.BackupError):
            device.call("flash_id")


class LauncherTests(unittest.TestCase):
    def test_native_launcher_in_directory_with_spaces_and_swedish_letters(self):
        with tempfile.TemporaryDirectory(prefix="TMBox å ä spaces-") as directory:
            folder = Path(directory)
            for filename in ("backup_esp8266.py", "Backup-ESP8266-Mac.command", "Backup-ESP8266-Windows.cmd"):
                shutil.copyfile(ROOT / filename, folder / filename)
            environment = dict(os.environ, TMBOX_BACKUP_NO_PAUSE="1")
            if os.name == "nt":
                launcher = folder / "Backup-ESP8266-Windows.cmd"
                command = 'cmd /d /c ""' + str(launcher) + '" --help"'
            else:
                command = ["bash", str(folder / "Backup-ESP8266-Mac.command"), "--help"]
            result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8",
                                    errors="replace", env=environment, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("--output", result.stdout)


class PackageTests(unittest.TestCase):
    def test_download_contains_complete_launchers_and_integrity_information(self):
        spec = importlib.util.spec_from_file_location("packager", ROOT / "package.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as directory:
            archive = module.package(Path(directory), "1" * 40)
            with zipfile.ZipFile(archive) as zipped:
                self.assertIsNone(zipped.testzip())
                names = zipped.namelist()
                prefix = names[0].split("/")[0] + "/"
                self.assertIn(prefix + "backup_esp8266.py", names)
                self.assertIn(prefix + "Backup-ESP8266-Mac.command", names)
                self.assertIn(prefix + "Backup-ESP8266-Windows.cmd", names)
                self.assertIn(prefix + "LAS-MIG.txt", names)
                mode = zipped.getinfo(prefix + "Backup-ESP8266-Mac.command").external_attr >> 16
                self.assertTrue(mode & stat.S_IXUSR)
                for line in zipped.read(prefix + "SHA256SUMS.txt").decode().splitlines():
                    digest, relative = line.split("  ", 1)
                    self.assertEqual(hashlib.sha256(zipped.read(prefix + relative)).hexdigest(), digest)


if __name__ == "__main__":
    unittest.main()
