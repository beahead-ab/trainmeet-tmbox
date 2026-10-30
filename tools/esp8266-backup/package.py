#!/usr/bin/env python3
"""Package the backup scripts; includes no USB data or user backup files."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import zipfile

ROOT = Path(__file__).resolve().parent
VERSION = "1.0.0"
FILES = ("backup_esp8266.py", "Backup-ESP8266-Mac.command", "Backup-ESP8266-Windows.cmd", "LAS-MIG.txt")


def package(output, commit):
    if not re.fullmatch("[0-9a-f]{40}", commit):
        raise ValueError("Expected a complete source commit")
    output.mkdir(parents=True, exist_ok=True)
    name = "trainmeet-esp8266-backup-" + VERSION
    contents = {filename: (ROOT / filename).read_bytes() for filename in FILES}
    contents["LICENSE"] = (ROOT.parent.parent / "LICENSE").read_bytes()
    contents["SOURCE.json"] = (json.dumps({
        "toolVersion": VERSION, "sourceCommit": commit, "esptoolVersion": "4.11.0",
        "sourceURL": "https://github.com/beahead-ab/trainmeet-tmbox/tree/" + commit + "/tools/esp8266-backup",
        "hardwareTested": False,
    }, indent=2) + "\n").encode()
    contents["SHA256SUMS.txt"] = "".join(
        hashlib.sha256(data).hexdigest() + "  " + filename + "\n"
        for filename, data in sorted(contents.items())).encode()
    archive = output / (name + ".zip")
    with zipfile.ZipFile(archive, "x", compression=zipfile.ZIP_DEFLATED) as zipped:
        for filename, data in sorted(contents.items()):
            entry = zipfile.ZipInfo(name + "/" + filename, date_time=(2026, 10, 26, 0, 0, 0))
            entry.create_system = 3
            entry.external_attr = (0o100755 if filename.endswith(".command") else 0o100644) << 16
            entry.compress_type = zipfile.ZIP_DEFLATED
            zipped.writestr(entry, data)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    (output / (name + ".zip.sha256")).write_text(digest + "  " + archive.name + "\n")
    return archive


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--source-commit", required=True)
    args = parser.parse_args()
    print(package(args.output, args.source_commit))
