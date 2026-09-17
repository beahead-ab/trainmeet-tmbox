#!/usr/bin/env python3
"""Assemble a self-contained HTTPS/localhost installer from real CI outputs.

No download, flashing, deploy or hardware claims. Fail closed on missing,
cross-version or damaged binaries. SHA-256 protects against incomplete copies,
not against a malicious host; this is not a signed OTA update mechanism.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent.parent
PROFILES = {"nodemcu-i2c": "ESP8266", "esp32-s3": "ESP32-S3"}


def validate_build(path: Path, profile: str, version: str, commit: str) -> dict:
    metadata = json.loads((path / "build.json").read_text())
    payload = (path / "firmware.bin").read_bytes()
    expected = {"id": profile, "version": version, "chipFamily": PROFILES[profile],
                "offset": 0, "hardwareTested": False, "sourceCommit": commit}
    for key, value in expected.items():
        if metadata.get(key) != value:
            raise ValueError(f"{profile}: mismatched {key}")
    if len(payload) < 1024 or payload[0] != 0xE9:
        raise ValueError(f"{profile}: not a bootable firmware image")
    if metadata.get("bytes") != len(payload) or metadata.get("sha256") != hashlib.sha256(payload).hexdigest():
        raise ValueError(f"{profile}: checksum/size mismatch")
    parts = metadata.get("sourceParts", [])
    if not parts or min(p["offset"] for p in parts) != 0:
        raise ValueError(f"{profile}: missing image at offset zero")
    if profile == "esp32-s3" and len(parts) < 4:
        raise ValueError("ESP32-S3 requires a merged image, not application-only firmware")
    return metadata


def package(output: Path, builds: dict[str, Path], version: str, commit: str) -> dict:
    if not re.fullmatch(r"\d+\.\d+\.\d+", version) or not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("Expected firmware version and full source commit")
    if not (output / "index.html").is_file() or not (output / "web-tools.js").is_file():
        raise ValueError("Build the installer web assets first")
    # Validate everything before replacing catalog or binaries.
    checked = {profile: validate_build(builds[profile], profile, version, commit) for profile in PROFILES}
    catalog = {"schema": 1, "version": version, "sourceCommit": commit, "profiles": []}
    sums = []
    for profile, metadata in checked.items():
        relative = Path("firmware") / profile
        destination = output / relative
        destination.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(builds[profile] / "firmware.bin", destination / "firmware.bin")
        shutil.copyfile(builds[profile] / "build.json", destination / "build.json")
        manifest = {
            "name": f"TrainMeet TMBox {profile}", "version": version,
            "new_install_prompt_erase": True, "new_install_improv_wait_time": 0,
            "builds": [{"chipFamily": metadata["chipFamily"], "improv": False,
                        "parts": [{"path": "firmware.bin", "offset": 0}]}],
        }
        (destination / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        catalog["profiles"].append({
            "id": profile, "chipFamily": metadata["chipFamily"], "hardwareTested": False,
            "image": (relative / "firmware.bin").as_posix(),
            "manifest": (relative / "manifest.json").as_posix(),
            "sha256": metadata["sha256"], "bytes": metadata["bytes"],
        })
        sums.append(f'{metadata["sha256"]}  {(relative / "firmware.bin").as_posix()}')
    (output / "SHA256SUMS").write_text("\n".join(sums) + "\n")
    (output / "catalog.json").write_text(json.dumps(catalog, indent=2) + "\n")
    return catalog


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--builds", type=Path, help="Folder with one subfolder per profile (CI artifacts)")
    parser.add_argument("--output", type=Path, default=ROOT / "dist/installer")
    args = parser.parse_args()
    paths = {name: args.builds / name if args.builds else ROOT / "firmware" /
             ("esp8266" if chip == "ESP8266" else "esp32") / ".pio/build" / name / "webflash"
             for name, chip in PROFILES.items()}
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    package(args.output, paths, (ROOT / "VERSION").read_text().strip(), commit)
    print(f"Installer with both firmware profiles: {args.output}")


if __name__ == "__main__":
    main()
