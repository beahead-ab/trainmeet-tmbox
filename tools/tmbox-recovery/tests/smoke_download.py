"""Smoke-test the actual assembled download without starting any USB operation."""
import argparse
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--package-dir", type=Path, required=True)
args = parser.parse_args()
archive = next(args.package_dir.glob("*.zip"))
with tempfile.TemporaryDirectory(prefix="TMBox gemensam å spaces-") as directory:
    with zipfile.ZipFile(archive) as zipped:
        assert zipped.testzip() is None
        for item in zipped.infolist():
            target = (Path(directory) / item.filename).resolve()
            assert target.is_relative_to(Path(directory).resolve())
            zipped.extract(item, directory)
            if os.name != "nt":
                target.chmod(stat.S_IMODE(item.external_attr >> 16))
        root = Path(directory) / zipped.namelist()[0].split("/")[0]
    spec = importlib.util.spec_from_file_location("download_menu", root / "meny.py")
    menu = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(menu)
    menu.validate_package(root)
    assert (root/"verktyg/esp8266/Backup-instruktioner.txt").is_file()
    assert (root/"verktyg/esp8266/Aterstallningsinstruktioner.txt").is_file()
    assert (root/"verktyg/esp32/LAS-MIG.txt").is_file()
    for _, relative, _ in menu.ACTIONS.values():
        result = subprocess.run([sys.executable, str(root/relative), "--help"],
                                capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=30)
        assert result.returncode == 0, result.stdout + result.stderr
    environment = dict(os.environ, TMBOX_MENU_NO_PAUSE="1", PYTHONUTF8="1")
    if os.name == "nt":
        launcher = root / "Starta-TMBox-Windows.cmd"
        command = 'cmd /d /c ""' + str(launcher) + '""'
    else:
        command = ["bash", str(root/"Starta-TMBox-Mac.command")]
    result = subprocess.run(command, input="0\n", capture_output=True, text=True,
                            encoding="utf-8", errors="replace", env=environment, timeout=30)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "TMBOX" in result.stdout
    assert "1." in result.stdout and "4." in result.stdout
    assert not (root/"TMBox-backuper").exists()
    print("Actual download: complete files, unmodified backend imports and native menu startup/exit passed without USB.")
