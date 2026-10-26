#!/usr/bin/env python3
"""One menu for the published, unchanged TMBox backup/restore backends."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import subprocess
import sys

VERSION = "1.0.0"
ACTIONS = {
    "1": ("Backup av ESP8266 / NodeMCU", "verktyg/esp8266/backup_esp8266.py", False),
    "2": ("Återställ ESP8266 / NodeMCU", "verktyg/esp8266/restore_esp8266.py", True),
    "3": ("Backup av ESP32 / ESP32-S3", "verktyg/esp32/backup_esp32.py", False),
    "4": ("Återställ ESP32 / ESP32-S3", "verktyg/esp32/restore_esp32.py", True),
}
REQUIRED = {action[1] for action in ACTIONS.values()} | {
    "verktyg/esp32/device_probe.py", "meny.py", "LAS-MIG.txt",
    "Starta-TMBox-Mac.command", "Starta-TMBox-Windows.cmd",
}


class PackageError(Exception):
    pass


def digest(path):
    value = hashlib.sha256()
    with path.open("rb") as handle:
        for part in iter(lambda: handle.read(1024 * 1024), b""):
            value.update(part)
    return value.hexdigest()


def validate_package(root):
    root = Path(root).resolve()
    manifest = root / "PAKET-INNEHALL.json"
    if manifest.is_symlink() or not manifest.is_file() or manifest.stat().st_size > 128 * 1024:
        raise PackageError("Paketinformationen saknas. Packa upp HELA ZIP-filen i en ny mapp.")
    try:
        data = json.loads(manifest.read_text(encoding="utf-8"))
    except (ValueError, UnicodeError) as error:
        raise PackageError("Paketinformationen går inte att läsa.") from error
    if not isinstance(data, dict) or type(data.get("schema")) is not int or data.get("schema") != 1 or not isinstance(data.get("files"), dict):
        raise PackageError("Ogiltig paketinformation.")
    files = data["files"]
    if not REQUIRED.issubset(files):
        raise PackageError("Paketet saknar nödvändiga start- eller hjälpfiler.")
    for name, expected in files.items():
        if (not isinstance(name, str) or not isinstance(expected, str) or
                len(expected) != 64 or any(c not in "0123456789abcdef" for c in expected) or
                "\\" in name):
            raise PackageError("Ogiltig filpost i paketinformationen.")
        relative = PurePosixPath(name)
        if relative.is_absolute() or ".." in relative.parts or not relative.parts:
            raise PackageError("En filsökväg lämnar paketmappen.")
        path = root.joinpath(*relative.parts)
        if (path.is_symlink() or not path.resolve().is_relative_to(root) or not path.is_file() or
                path.stat().st_size > 5 * 1024 * 1024):
            raise PackageError("En paketfil saknas eller har ersatts med en länk: " + name)
        if digest(path) != expected:
            raise PackageError("En paketfil har ändrats eller skadats: " + name)
    return root


def main(argv=None, root=None, input_fn=None, runner=None):
    parser = argparse.ArgumentParser(description="Gemensam meny för TMBox-backup och återställning på Mac och Windows.")
    parser.add_argument("--version", action="version", version=VERSION)
    parser.parse_args(argv)
    if sys.version_info < (3, 10):
        print("Python 3.10 eller senare behövs. Läs LAS-MIG.txt.")
        return 1
    input_fn, runner = input_fn or input, runner or subprocess.run
    active = None
    try:
        root = validate_package(root or Path(__file__).resolve().parent)
        while True:
            print("\nTMBOX — BACKUP OCH ÅTERSTÄLLNING")
            print("1. Backup av ESP8266 / NodeMCU")
            print("2. Återställ ESP8266 / NodeMCU")
            print("3. Backup av ESP32 / ESP32-S3")
            print("4. Återställ ESP32 / ESP32-S3")
            print("0. Avsluta")
            print("Instruktioner: LAS-MIG.txt i paketets huvudmapp.")
            choice = input_fn("Välj 0–4: ").strip()
            if choice == "0":
                return 0
            if choice not in ACTIONS:
                print("Skriv en siffra mellan 0 och 4.")
                continue
            # Recheck immediately before dispatch, including the imported helper modules.
            validate_package(root)
            active = ACTIONS[choice]
            label, relative, restoring = active
            print("\n" + label)
            if restoring:
                print("Återställning ersätter hela boxens program och lokala inställningar.")
                print("Verktyget tar en ny säkerhetskopia och kräver ATERSTALL före skrivningen.")
            else:
                print("Verktyget läser hela flashminnet och verifierar kopian mot boxen.")
            script = root / relative
            result = runner([sys.executable, str(script)], cwd=str(script.parent))
            active = None
            if result.returncode:
                print("\nVerktyget avslutades med fel eller avbrott. Läs resultatet och eventuell rapport ovan.")
                print("Ett fel betyder inte att en återställning är godkänd.")
            else:
                print("\nÅtgärden är avslutad. Läs verktygets resultat ovan; ett avbrutet val är inte en utförd åtgärd.")
            input_fn("Tryck Enter för huvudmenyn.")
    except PackageError as error:
        print("\nPAKETET KAN INTE STARTAS: " + str(error))
        print("Hämta paketet igen och packa upp hela ZIP-filen i en ny mapp.")
        return 1
    except (KeyboardInterrupt, EOFError):
        if active:
            if active[2]:
                print("\nAvbrutet. Om skrivningen hade startat kan boxen ha ett ofullständigt program.")
                print("Behåll backupfilerna och läs återställningsrapporten innan ett nytt komplett försök.")
            else:
                print("\nBackupen avbröts. En kopia är godkänd endast om backupverktyget visade KLART.")
            return 130
        print("\nMenyn stängs.")
        return 0
    except (OSError, subprocess.SubprocessError) as error:
        print("\nVerktyget kunde inte slutföras: " + str(error))
        print("Kontrollera fönstrets resultat och eventuell rapport. Ingen åtgärd godkänns av menyn.")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
