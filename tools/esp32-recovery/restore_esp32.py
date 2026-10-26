#!/usr/bin/env python3
"""Restore a verified full ESP32 backup only to the matching physical box."""
from __future__ import annotations

import argparse
import contextlib
from datetime import datetime
import hashlib
import io
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import uuid

import backup_esp32 as common

VERSION = "1.0.0"
CONFIRMATION = "ATERSTALL"


class RestoreError(common.BackupError):
    def __init__(self, message, state=None):
        super().__init__(message)
        self.state = state or {}


class Image:
    def __init__(self, folder, data, metadata):
        self.folder, self.data, self.metadata = folder, data, metadata
        self.digest = hashlib.sha256(data).hexdigest()
        self.size = len(data)
        self.mac = metadata["mac"].lower()
        self.esptool_chip = metadata["esptoolChip"]


def read_member(folder, filename, maximum=128 * 1024):
    path = folder / filename
    if path.is_symlink() or not path.is_file():
        raise RestoreError("Backupmappen saknar en vanlig fil: " + filename)
    if path.stat().st_size > maximum:
        raise RestoreError("Backupfilen är större än tillåtet för formatet.")
    return path.read_bytes()


def load_backup(folder):
    folder = Path(folder).expanduser().resolve()
    try:
        metadata = json.loads(read_member(folder, "backup-info.json").decode("utf-8"))
    except (ValueError, UnicodeError) as error:
        raise RestoreError("backup-info.json kan inte läsas. Välj hela resultatmappen från backupverktyget.") from error
    if not isinstance(metadata, dict):
        raise RestoreError("Ogiltig backupinformation.")
    if type(metadata.get("schema")) is not int or metadata["schema"] != 1 or metadata.get("chip") not in common.CHIP_TYPES:
        raise RestoreError("Backupen är inte en ESP32-backup i det format som verktyget stöder.")
    if metadata.get("verifiedAgainstDevice") is not True:
        raise RestoreError("Backupen är inte markerad som verifierad mot boxen.")
    if metadata.get("backupFile") != "esp32-full-backup.bin":
        raise RestoreError("Fel backupfil. En lös firmwarefil eller en Update-fil får inte återställas här.")
    if metadata.get("esptoolChip") != common.CHIP_TYPES[metadata["chip"]]:
        raise RestoreError("Chipmodellen i backupinformationen stämmer inte.")
    if not isinstance(metadata.get("security"), dict) or any(
            metadata["security"].get(key) is not False
            for key in ("flashEncryption", "secureBoot", "secureDownload")):
        raise RestoreError("Backupen saknar godkänd status för okrypterat och oskyddat flashminne.")
    size = metadata.get("flashBytes")
    if type(size) is not int or size < 1024 * 1024 or size > 16 * 1024 * 1024 or size & (size - 1):
        raise RestoreError("Ogiltig fullständig minnesstorlek i backupen.")
    if type(metadata.get("bytes")) is not int or metadata["bytes"] != size:
        raise RestoreError("Backupinformationen beskriver inte hela flashminnet.")
    mac = metadata.get("mac")
    if not isinstance(mac, str) or not re.fullmatch(r"(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}", mac):
        raise RestoreError("Backupen saknar en giltig MAC-adress. Samma fysiska box kan inte säkerställas.")
    expected = metadata.get("sha256")
    if not isinstance(expected, str) or not re.fullmatch(r"[0-9a-fA-F]{64}", expected):
        raise RestoreError("Backupen saknar en giltig SHA-256-kontrollsumma.")
    data = read_member(folder, "esp32-full-backup.bin", maximum=size)
    if len(data) != size:
        raise RestoreError("Backupfilens storlek motsvarar inte hela flashminnet.")
    offset, chip_id = (0x1000, 0) if metadata["esptoolChip"] == "esp32" else (0, 9)
    if data[offset] != 0xE9 or int.from_bytes(data[offset + 12:offset + 14], "little") != chip_id:
        raise RestoreError("Bootloaderns placering eller chip-ID matchar inte ESP32-modellen.")
    digest = hashlib.sha256(data).hexdigest()
    if digest != expected.lower():
        raise RestoreError("Backupfilens SHA-256 stämmer inte. Filen kan ha ändrats eller skadats.")
    try:
        lines = [line.strip() for line in read_member(folder, "SHA256SUMS.txt").decode("utf-8").splitlines() if line.strip()]
    except UnicodeError as error:
        raise RestoreError("Kontrollsummefilen kan inte läsas.") from error
    if len(lines) != 1 or not re.fullmatch(
            re.escape(digest) + r"\s+\*?esp32-full-backup\.bin", lines[0], re.I):
        raise RestoreError("SHA256SUMS.txt och backupfilen stämmer inte överens.")
    return Image(folder, data, metadata)


def identity(device, image):
    info = device.probe()
    if info["esptoolChip"] != image.esptool_chip or info["mac"] != image.mac:
        raise RestoreError("Ansluten box har inte samma chipmodell och MAC-adress som backupen.")
    output = device.call("flash_id")
    common.check_flash_identity(output, info)
    if common.flash_size(output) != image.size:
        raise RestoreError("Boxens flashstorlek skiljer sig från backupen. Ingen skrivning tillåts.")


def write_image(device, staged, image):
    # The transaction calls this only after validation, a fresh rescue backup,
    # typed confirmation and a repeated identity check. Never alter boot headers.
    if staged.stat().st_size != image.size or common.sha256(staged) != image.digest:
        raise RestoreError("Arbetskopian har ändrats. Ingen skrivning tillåts.")
    if device.chip != image.esptool_chip or not device.identity or device.identity["mac"] != image.mac:
        raise RestoreError("Rätt fysiska box måste vara identifierad före skrivning.")
    command = [device.python, "-m", "esptool", "--chip", device.chip,
               "--port", device.port, "--baud", "115200", "--after", "no_reset",
               "write_flash", "--flash_mode", "keep", "--flash_freq", "keep",
               "--flash_size", "keep", "0", str(staged)]
    result = device.runner(command, capture_output=True, text=True,
                           encoding="utf-8", errors="replace", timeout=3600)
    output = (result.stdout or "") + (result.stderr or "")
    device.log.append("write_flash\n" + output + "\n")
    if result.returncode:
        raise RestoreError("Skrivningen misslyckades eller avbröts. Återställningen är inte godkänd.")


def restore(image, device, safety_root, confirm=None):
    confirm = confirm or input
    root = Path(safety_root).expanduser()
    root.mkdir(mode=0o700, parents=True, exist_ok=True)
    report = root / ("Aterstallningsrapport-" + datetime.now().strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:8])
    report.mkdir(mode=0o700)
    state = {
        "schema": 1, "toolVersion": VERSION, "esptoolVersion": common.ESPTOOL_VERSION,
        "startedAt": datetime.now().astimezone().isoformat(), "chip": image.metadata["chip"], "esptoolChip": image.esptool_chip,
        "mac": image.mac, "flashBytes": image.size, "sourceFolder": str(image.folder),
        "sourceSha256": image.digest, "status": "checking", "writeStarted": False,
        "verifiedAgainstDevice": False, "reportFolder": str(report),
    }
    try:
        print("\nKontrollerar att rätt fysiska ESP32-box är ansluten...")
        identity(device, image)
        print("Tar först en verifierad säkerhetskopia av boxens NUVARANDE minne.")
        print("Detta kan ta flera minuter. Behåll USB-kabel och ström.")
        captured = io.StringIO()
        with contextlib.redirect_stdout(captured):
            rescue = common.backup(device, root / "Fore-aterstallning")
        state["rescueBackupFolder"] = str(rescue)
        rescue_info = json.loads((rescue / "backup-info.json").read_text(encoding="utf-8"))
        if (rescue_info.get("verifiedAgainstDevice") is not True or
                rescue_info.get("mac", "").lower() != image.mac or
                rescue_info.get("flashBytes") != image.size or rescue_info.get("esptoolChip") != image.esptool_chip):
            raise RestoreError("Säkerhetskopian före återställningen gäller inte för rätt box.")
        print("Säkerhetskopian före återställning är verifierad: " + str(rescue))
        # Snapshot the validated bytes: changing the original file after this
        # point cannot change what will be written.
        with tempfile.TemporaryDirectory(prefix="trainmeet-esp32-restore-", ignore_cleanup_errors=True) as temporary:
            staged = Path(temporary) / "verified-source.bin"
            with staged.open("xb") as handle:
                handle.write(image.data)
            if sys.platform != "win32":
                staged.chmod(0o600)
            print("\nDETTA ERSÄTTER HELA BOXENS PROGRAM OCH SPARADE INSTÄLLNINGAR.")
            print("Chipmodell: " + image.metadata["chip"])
            print("Boxens MAC: " + image.mac)
            print("Vald backup: " + str(image.folder))
            print("Backupens tid: " + common.clean_text(image.metadata.get("createdAt", "okänd")))
            print("Storlek: " + str(image.size) + " byte. Kontrollsumma: " + image.digest)
            answer = confirm("Skriv ATERSTALL och tryck Enter för att fortsätta. Allt annat avbryter: ")
            if answer.strip() != CONFIRMATION:
                state["status"] = "cancelled_before_write"
                print("Avbrutet före skrivning. Flashminnet har inte ändrats av verktyget.")
                return state
            # The user may have reconnected USB while reading the confirmation.
            identity(device, image)
            if staged.stat().st_size != image.size or common.sha256(staged) != image.digest:
                raise RestoreError("Arbetskopian har ändrats. Ingen skrivning tillåts.")
            print("Skriver hela backupen. Bryt INTE strömmen och dra inte ur USB-kabeln.")
            state["writeStarted"] = True
            write_image(device, staged, image)
            print("Skrivningen är klar. Verifierar nu HELA flashminnet mot backupen...")
            result = device.call("verify_flash", (
                "--flash_mode", "keep", "--flash_freq", "keep", "--flash_size", "keep", "0", staged))
            if "-- verify OK (digest matched)" not in result:
                raise RestoreError("Efterkontrollen gav inget entydigt godkännande.")
            state["status"] = "verified"
            state["verifiedAgainstDevice"] = True
            state["finishedAt"] = datetime.now().astimezone().isoformat()
            return state
    except (common.BackupError, OSError, ValueError, subprocess.SubprocessError, KeyboardInterrupt, EOFError) as error:
        state["status"] = "failed_after_write_started" if state["writeStarted"] else "failed_before_write"
        state["error"] = common.clean_text(error) or "Avbrutet av användaren."
        raise RestoreError(state["error"], state) from error
    finally:
        try:
            common.private_text(report / "resultat.json", json.dumps(state, ensure_ascii=False, indent=2) + "\n")
            common.private_text(report / "aterstallning.log", "".join(device.log))
        except OSError:
            print("Rapporten kunde inte sparas. Boxens verifieringsresultat visas i fönstret.")
        print("Rapportmapp: " + str(report))


def select_backup():
    root = Path.home() / "TMBox-backuper-ESP32"
    while True:
        folders = sorted((p for p in root.iterdir() if p.is_dir() and (p / "backup-info.json").is_file()),
                         key=lambda p: p.name, reverse=True) if root.is_dir() else []
        print("\nBackupmappar i " + str(root) + ":")
        for number, folder in enumerate(folders, 1):
            print(str(number) + ". " + common.clean_text(folder.name))
        if not folders:
            print("Ingen backup hittades där.")
        answer = input("Välj nummer, M för annan backupmapp eller A för avbryt: ").strip()
        if answer.lower() == "a":
            return None
        if answer.lower() == "m":
            value = input("Sökväg till HELA backupmappen: ").strip().strip('"')
            if value:
                return Path(value).expanduser()
        if answer.isdigit() and 1 <= int(answer) <= len(folders):
            return folders[int(answer) - 1]
        print("Välj ett av alternativen.")


def main(argv=None):
    parser = argparse.ArgumentParser(description="Återställ en verifierad hel ESP32-backup till samma box.")
    parser.add_argument("--backup", help="Resultatmapp från backupverktyget; annars väljs den i en lista")
    parser.add_argument("--port", help="USB-port; annars väljs den i en lista")
    parser.add_argument("--safety-output", help="Mapp för automatisk säkerhetskopia och rapport")
    parser.add_argument("--worker", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--version", action="version", version=VERSION)
    args = parser.parse_args(argv)
    if sys.version_info < common.MIN_PYTHON:
        print("Python 3.10 eller senare behövs. Läs LAS-MIG.txt.")
        return 1
    try:
        if not args.worker:
            python = common.prepare_runtime()
            forwarded = list(sys.argv[1:] if argv is None else argv)
            try:
                return subprocess.run([str(python), str(Path(__file__).resolve()), "--worker", *forwarded]).returncode
            except KeyboardInterrupt:
                print("\nAvbrutet. Om skrivningen hade startat kan boxen vara ofullständig.")
                print("Behåll backupfilerna och kontrollera återställningsrapporten innan nytt komplett försök.")
                return 130
        import esptool
        if esptool.__version__ != common.ESPTOOL_VERSION:
            raise RestoreError("Fel esptool-version. Starta med Mac-/Windows-startfilen.")
        print("TrainMeet ESP32-återställning " + VERSION)
        print("En verifierad backupmapp från backupverktyget och samma fysiska box krävs.")
        folder = args.backup or select_backup()
        if not folder:
            print("Avbrutet före anslutning.")
            return 0
        image = load_backup(folder)
        print("Backupfilens storlek och SHA-256 är godkända.")
        port = args.port or common.select_port()
        if not port:
            print("Avbrutet före anslutning.")
            return 0
        safety = Path(args.safety_output).expanduser() if args.safety_output else Path.home() / "TMBox-backuper-ESP32" / "Aterstallningsskydd"
        result = restore(image, common.Device(sys.executable, port), safety)
        if not result["verifiedAgainstDevice"]:
            print("Ingen återställning utfördes. Tryck RST/RESET eller återanslut USB för normal start.")
            return 0
        print("\nKLART — hela återställningen är verifierad mot boxens flashminne.")
        print("Gamla programmet och inställningarna finns nu på boxen.")
        print("Tryck RST/RESET på boxen eller koppla ur och anslut USB för normal start.")
        print("Kontrollera därefter att boxen ansluter till förväntat nät och rätt server.")
        return 0
    except RestoreError as error:
        print("\nÅTERSTÄLLNINGEN ÄR INTE GODKÄND: " + common.clean_text(error))
        state = error.state
        if state.get("writeStarted"):
            print("Skrivningen hade startat. Boxen kan ha ett ofullständigt program.")
            print("Behåll källbackupen och säkerhetskopian före återställningen. Gör ett nytt komplett försök.")
        else:
            print("Ingen skrivning till boxens flashminne har startat.")
            print("Tryck RST/RESET eller återanslut USB för normal start.")
        if state.get("rescueBackupFolder"):
            print("Säkerhetskopia före återställning: " + state["rescueBackupFolder"])
        if state.get("reportFolder"):
            print("Se rapport och logg i " + state["reportFolder"])
        return 1
    except (KeyboardInterrupt, EOFError):
        print("\nAvbrutet före skrivning. Ingen återställning har startat.")
        return 130
    except (OSError, subprocess.SubprocessError) as error:
        print("\nVerktyget kunde inte startas: " + common.clean_text(error))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
