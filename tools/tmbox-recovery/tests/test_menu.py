"""Menu and packaging tests. No serial ports or physical devices are accessed."""
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / filename)
    item = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(item)
    return item

menu = module("recovery_menu", "meny.py")
pack = module("recovery_pack", "package.py")


def minimal_bundle(root):
    for name in menu.REQUIRED:
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"fixture, never executed")
    manifest = {"schema": 1, "files": {
        name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in menu.REQUIRED}}
    (root / "PAKET-INNEHALL.json").write_text(json.dumps(manifest))
    return manifest


def upstream(data=b"print('fixture')", destination="verktyg", mappings=None, version="4.11.0", extra=None):
    mappings = mappings or {"tool.py": "esp8266/backup_esp8266.py"}
    files = {name: data for name in mappings}
    files.update(extra or {})
    files["SOURCE.json"] = json.dumps({
        "sourceCommit": "1"*40, "toolVersion": "1.0.0", "esptoolVersion": version}).encode()
    files["SHA256SUMS.txt"] = "".join(
        hashlib.sha256(value).hexdigest() + "  " + name + "\n" for name, value in sorted(files.items())).encode()
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w") as zipped:
        for name, value in files.items():
            zipped.writestr("fixture/" + name, value)
    data = buffer.getvalue()
    return {"name": "fixture.zip", "prefix": "fixture/", "sha256": hashlib.sha256(data).hexdigest(),
            "commit": "1"*40, "destination": destination, "files": mappings}, data


class MenuTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="TMBox menu å spaces-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.manifest = minimal_bundle(self.root)
        self.calls = []

    def execute(self, choices, result=0, runner=None):
        iterator = iter(choices)
        def read(prompt):
            try:
                return next(iterator)
            except StopIteration:
                raise EOFError()
        def run(command, **kwargs):
            self.calls.append((command, kwargs))
            return subprocess.CompletedProcess(command, result)
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            code = menu.main([], root=self.root, input_fn=read, runner=runner or run)
        return code, output.getvalue()

    def test_four_choices_dispatch_only_the_fixed_backend(self):
        for choice, (_, relative, _) in menu.ACTIONS.items():
            with self.subTest(choice=choice):
                self.calls.clear()
                code, text = self.execute([choice, "", "0"])
                self.assertEqual(code, 0)
                self.assertEqual(len(self.calls), 1)
                command, options = self.calls[0]
                self.assertEqual(command, [sys.executable, str(self.root / relative)])
                self.assertEqual(options, {"cwd": str((self.root / relative).parent)})

    def test_exit_does_not_start_backend(self):
        code, _ = self.execute(["0"])
        self.assertEqual(code, 0)
        self.assertEqual(self.calls, [])

    def test_invalid_input_is_not_a_shell_command(self):
        code, _ = self.execute(["9", "2 && ignored", "../tool.py", "0"])
        self.assertEqual(code, 0)
        self.assertEqual(self.calls, [])

    def test_missing_manifest_blocks_all_actions(self):
        (self.root / "PAKET-INNEHALL.json").unlink()
        code, text = self.execute(["1"])
        self.assertEqual(code, 1)
        self.assertEqual(self.calls, [])
        self.assertIn("HELA", text)

    def test_corrupted_helper_blocks_all_actions(self):
        (self.root / "verktyg/esp32/device_probe.py").write_text("changed")
        code, _ = self.execute(["1"])
        self.assertEqual(code, 1)
        self.assertEqual(self.calls, [])

    def test_file_changed_after_menu_is_rechecked_before_dispatch(self):
        def choices():
            (self.root / menu.ACTIONS["2"][1]).write_text("changed after initial validation")
            yield "2"
        code, _ = self.execute(choices())
        self.assertEqual(code, 1)
        self.assertEqual(self.calls, [])

    def test_manifest_cannot_reference_outside_package(self):
        for path in ("../outside.py", "/tmp/outside.py", "C:\\outside.py"):
            with self.subTest(path=path):
                value = dict(self.manifest)
                value["files"] = dict(self.manifest["files"], **{path: "0"*64})
                (self.root / "PAKET-INNEHALL.json").write_text(json.dumps(value))
                code, _ = self.execute(["1"])
                self.assertEqual(code, 1)
                self.assertEqual(self.calls, [])

    def test_failed_backend_is_not_presented_as_success(self):
        code, text = self.execute(["2", "", "0"], result=1)
        self.assertEqual(code, 0)
        self.assertIn("fel eller avbrott", text)
        self.assertNotIn("KLART", text)

    def test_cancelled_backend_zero_exit_is_not_claimed_as_performed(self):
        code, text = self.execute(["1", "", "0"], result=0)
        self.assertEqual(code, 0)
        self.assertIn("avbrutet val", text)
        self.assertNotIn("KLART", text)

    def test_interrupt_during_restore_warns_about_partial_write(self):
        def interrupt(command, **kwargs):
            raise KeyboardInterrupt()
        code, text = self.execute(["4"], runner=interrupt)
        self.assertEqual(code, 130)
        self.assertIn("ofullständigt program", text)
        self.assertNotIn("oförändrat", text)

    def test_interrupt_during_backup_does_not_approve_copy(self):
        def interrupt(command, **kwargs):
            raise KeyboardInterrupt()
        code, text = self.execute(["1"], runner=interrupt)
        self.assertEqual(code, 130)
        self.assertIn("endast om", text)

    def test_eof_before_selection_is_safe(self):
        code, _ = self.execute([])
        self.assertEqual(code, 0)
        self.assertEqual(self.calls, [])

    def test_restore_choices_explain_the_consequence(self):
        _, text = self.execute(["2", "", "0"])
        self.assertIn("hela boxens program", text)
        self.assertIn("ATERSTALL", text)


class PackageTests(unittest.TestCase):
    def test_upstream_bytes_are_not_modified(self):
        source, archive = upstream()
        contents = pack.source_files(source, archive)
        self.assertEqual(contents["verktyg/esp8266/backup_esp8266.py"], b"print('fixture')")

    def test_wrong_archive_hash_is_rejected(self):
        source, archive = upstream()
        source["sha256"] = "0"*64
        with self.assertRaises(ValueError):
            pack.source_files(source, archive)

    def test_unexpected_source_commit_is_rejected(self):
        source, archive = upstream()
        source["commit"] = "2"*40
        with self.assertRaises(ValueError):
            pack.source_files(source, archive)

    def test_unexpected_dependency_version_is_rejected(self):
        source, archive = upstream(version="5.0.0")
        with self.assertRaises(ValueError):
            pack.source_files(source, archive)

    def test_traversal_member_is_rejected_even_if_outer_hash_matches(self):
        source, archive = upstream(extra={"../outside.py": b"not extracted"})
        with self.assertRaises(ValueError):
            pack.source_files(source, archive)

    def test_conflicting_shared_backend_is_rejected(self):
        a, aa = upstream(data=b"first")
        b, bb = upstream(data=b"second")
        a["name"], b["name"] = "a.zip", "b.zip"
        with tempfile.TemporaryDirectory() as directory, self.assertRaises(ValueError):
            pack.package(Path(directory), "3"*40, sources=(a,b),
                         fetcher=lambda url: aa if url.endswith("a.zip") else bb)

    def test_complete_package_has_valid_manifest_checksums_and_mac_modes(self):
        mappings = {
            "a.py": "esp8266/backup_esp8266.py", "b.py": "esp8266/restore_esp8266.py",
            "c.py": "esp32/backup_esp32.py", "d.py": "esp32/restore_esp32.py",
            "e.py": "esp32/device_probe.py",
        }
        source, archive = upstream(mappings=mappings)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = pack.package(root, "3"*40, sources=(source,), fetcher=lambda url: archive)
            with zipfile.ZipFile(output) as zipped:
                self.assertIsNone(zipped.testzip())
                prefix = zipped.namelist()[0].split("/")[0] + "/"
                for line in zipped.read(prefix+"SHA256SUMS.txt").decode().splitlines():
                    digest, filename = line.split("  ",1)
                    self.assertEqual(hashlib.sha256(zipped.read(prefix+filename)).hexdigest(),digest)
                for name in ("Starta-TMBox-Mac.command",):
                    self.assertTrue(zipped.getinfo(prefix+name).external_attr>>16 & 0o100)
                source_info = json.loads(zipped.read(prefix+"SOURCES.json"))
                self.assertFalse(source_info["backendsModified"])
                unpacked = root / "unpacked"
                zipped.extractall(unpacked)
            menu.validate_package(unpacked / prefix.rstrip("/"))

    def test_existing_download_is_never_overwritten(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            existing = root / "trainmeet-tmbox-backup-restore-1.0.0.zip"
            existing.write_bytes(b"original release")
            with self.assertRaises(FileExistsError):
                pack.package(root, "3"*40, fetcher=lambda url: self.fail("Must not fetch"))
            self.assertEqual(existing.read_bytes(), b"original release")


if __name__ == "__main__":
    unittest.main()
