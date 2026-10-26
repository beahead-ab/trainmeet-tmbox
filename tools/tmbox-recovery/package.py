#!/usr/bin/env python3
"""Assemble a menu bundle from immutable, checksum-pinned published tools."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import stat
import subprocess
import zipfile
import io

ROOT = Path(__file__).resolve().parent
VERSION = "1.0.0"
BASE = "https://github.com/beahead-ab/trainmeet-tmbox/releases/download/v0.9.0-wifi-test.1/"
SOURCES = (
    {"name": "trainmeet-esp8266-backup-1.0.0.zip",
     "prefix": "trainmeet-esp8266-backup-1.0.0/",
     "sha256": "4fcb121543b884a3ae9b52735f4c82a1d9aeb3a81e29e2ebd18788978f07d608",
     "commit": "a07c4c24a65ff0d1c2a094f51c75a6f8f3f38033",
     "destination": "verktyg/esp8266",
     "files": {"backup_esp8266.py": "backup_esp8266.py",
               "Backup-ESP8266-Mac.command": "Backup-ESP8266-Mac.command",
               "Backup-ESP8266-Windows.cmd": "Backup-ESP8266-Windows.cmd",
               "LAS-MIG.txt": "Backup-instruktioner.txt"}},
    {"name": "trainmeet-esp8266-restore-1.0.0.zip",
     "prefix": "trainmeet-esp8266-restore-1.0.0/",
     "sha256": "de597999b9e95cbdcd06e3f20bafeddb8abe1d11a96ffdab182a1295556f80d7",
     "commit": "a90f4d9776981f051fe1f70d25dfa900dd7a8078",
     "destination": "verktyg/esp8266",
     "files": {"backup_esp8266.py": "backup_esp8266.py", "restore_esp8266.py": "restore_esp8266.py",
               "Aterstall-ESP8266-Mac.command": "Aterstall-ESP8266-Mac.command",
               "Aterstall-ESP8266-Windows.cmd": "Aterstall-ESP8266-Windows.cmd",
               "LAS-MIG.txt": "Aterstallningsinstruktioner.txt"}},
    {"name": "trainmeet-esp32-backup-restore-1.0.0.zip",
     "prefix": "trainmeet-esp32-backup-restore-1.0.0/",
     "sha256": "a26d479ebf03a634e171c9db0a3aeeeb4dc123770f577778bac5a7d202694789",
     "commit": "8d897cc78825ecb9cab347f44353526f23b70c43",
     "destination": "verktyg/esp32",
     "files": {name: name for name in (
         "backup_esp32.py", "restore_esp32.py", "device_probe.py",
         "Backup-ESP32-Mac.command", "Backup-ESP32-Windows.cmd",
         "Aterstall-ESP32-Mac.command", "Aterstall-ESP32-Windows.cmd", "LAS-MIG.txt")}},
)
LOCAL_FILES = ("meny.py", "Starta-TMBox-Mac.command", "Starta-TMBox-Windows.cmd", "LAS-MIG.txt")


def hash_bytes(data):
    return hashlib.sha256(data).hexdigest()


def fetch(url):
    return subprocess.run(
        ["curl", "--fail", "--location", "--silent", "--show-error", "--retry", "3",
         "--proto", "=https", "--proto-redir", "=https", "--max-filesize", "2097152", url],
        capture_output=True, check=True, timeout=120).stdout


def source_files(source, archive):
    if hash_bytes(archive) != source["sha256"]:
        raise ValueError("Published archive checksum mismatch: " + source["name"])
    prefix = source["prefix"]
    with zipfile.ZipFile(io.BytesIO(archive)) as zipped:
        names = zipped.namelist()
        if len(names) != len(set(names)) or zipped.testzip() is not None:
            raise ValueError("Duplicate or corrupted archive members")
        for info in zipped.infolist():
            relative = PurePosixPath(info.filename)
            if ("\\" in info.filename or relative.is_absolute() or ".." in relative.parts or
                    not info.filename.startswith(prefix) or stat.S_ISLNK(info.external_attr >> 16) or
                    info.file_size > 1024 * 1024):
                raise ValueError("Invalid archive member")
        actual = {name[len(prefix):] for name in names}
        sums = {}
        for line in zipped.read(prefix + "SHA256SUMS.txt").decode().splitlines():
            digest, filename = line.split("  ", 1)
            if filename in sums:
                raise ValueError("Duplicate checksum entry")
            sums[filename] = digest
        if set(sums) != actual - {"SHA256SUMS.txt"}:
            raise ValueError("Incomplete upstream checksums")
        for filename, digest in sums.items():
            if hash_bytes(zipped.read(prefix + filename)) != digest:
                raise ValueError("Upstream file checksum mismatch")
        info = json.loads(zipped.read(prefix + "SOURCE.json"))
        if (info.get("sourceCommit") != source["commit"] or info.get("toolVersion") != "1.0.0" or
                info.get("esptoolVersion") != "4.11.0"):
            raise ValueError("Upstream source/version mismatch")
        return {source["destination"] + "/" + destination: zipped.read(prefix + original)
                for original, destination in source["files"].items()}


def package(output, commit, fetcher=None, sources=None):
    if not re.fullmatch("[0-9a-f]{40}", commit):
        raise ValueError("Expected full source commit")
    fetcher, sources = fetcher or fetch, SOURCES if sources is None else sources
    output.mkdir(parents=True, exist_ok=True)
    name = "trainmeet-tmbox-backup-restore-" + VERSION
    archive = output / (name + ".zip")
    if archive.exists():
        raise FileExistsError(archive)
    contents = {name: (ROOT / name).read_bytes() for name in LOCAL_FILES}
    contents["LICENSE"] = (ROOT.parent.parent / "LICENSE").read_bytes()
    for source in sources:
        incoming = source_files(source, fetcher(BASE + source["name"]))
        for filename, data in incoming.items():
            if filename in contents and contents[filename] != data:
                raise ValueError("Conflicting shared backend: " + filename)
            contents[filename] = data
    contents["verktyg/esp8266/LAS-MIG.txt"] = (
        "Starta normalt den gemensamma menyn i paketets huvudmapp.\n"
        "För ESP8266-backup: läs Backup-instruktioner.txt.\n"
        "För ESP8266-återställning: läs Aterstallningsinstruktioner.txt.\n").encode()
    contents["SOURCES.json"] = (json.dumps({
        "menuVersion": VERSION, "sourceCommit": commit, "hardwareTested": False,
        "backends": [{key: source[key] for key in ("name", "sha256", "commit")} for source in sources],
        "backendsModified": False,
    }, indent=2) + "\n").encode()
    manifest = {"schema": 1, "menuVersion": VERSION,
                "files": {filename: hash_bytes(data) for filename, data in sorted(contents.items())}}
    contents["PAKET-INNEHALL.json"] = (json.dumps(manifest, indent=2) + "\n").encode()
    contents["SHA256SUMS.txt"] = "".join(
        hash_bytes(data) + "  " + filename + "\n" for filename, data in sorted(contents.items())).encode()
    with zipfile.ZipFile(archive, "x", compression=zipfile.ZIP_DEFLATED) as zipped:
        for filename, data in sorted(contents.items()):
            item = zipfile.ZipInfo(name + "/" + filename, date_time=(2026, 10, 26, 0, 0, 0))
            item.create_system = 3
            item.external_attr = (0o100755 if filename.endswith(".command") else 0o100644) << 16
            item.compress_type = zipfile.ZIP_DEFLATED
            zipped.writestr(item, data)
    (output / (name + ".zip.sha256")).write_text(hash_bytes(archive.read_bytes()) + "  " + archive.name + "\n")
    return archive


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--source-commit", required=True)
    args = parser.parse_args()
    print(package(args.output, args.source_commit))
