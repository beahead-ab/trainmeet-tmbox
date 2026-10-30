#!/usr/bin/env python3
"""Read and verify ESP32 flash. No flash-writing operations are provided."""
from __future__ import annotations

import argparse
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import uuid
import venv

VERSION = "1.0.0"
ESPTOOL_VERSION = "4.11.0"
MIN_PYTHON = (3, 10)
CHIP_TYPES = {"ESP32": "esp32", "ESP32-S3": "esp32s3"}
ALLOWED_OPERATIONS = frozenset({"flash_id", "read_flash", "verify_flash"})


class BackupError(Exception):
    pass


def clean_text(value):
    return "".join(c for c in str(value) if c.isprintable())


def private_text(path, text):
    with path.open("x", encoding="utf-8") as handle:
        handle.write(text)
    if os.name != "nt":
        path.chmod(0o600)


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def flash_size(output):
    match = re.search(r"Detected flash size:\s*(\d+)\s*(KB|MB)\b", output, re.I)
    if not match:
        raise BackupError("Kunde inte fastställa hela flashminnets storlek. Ingen backup godkänns.")
    size = int(match.group(1)) * (1024 if match.group(2).upper() == "KB" else 1024 * 1024)
    if size < 1024 * 1024 or size > 16 * 1024 * 1024 or size & (size - 1):
        raise BackupError("Oväntad minnesstorlek för ESP32. Ingen backup godkänns.")
    return size


class Device:
    def __init__(self, python, port, runner=None):
        self.python, self.port = str(python), str(port)
        self.runner = runner or subprocess.run
        self.log = []
        self.chip = None
        self.identity = None

    def probe(self):
        command = [self.python, str(Path(__file__).with_name("device_probe.py")), "--port", self.port]
        result = self.runner(command, capture_output=True, text=True,
                             encoding="utf-8", errors="replace", timeout=60)
        if result.returncode:
            self.log.append("probe failed\n" + (result.stderr or "") + "\n")
            reason = clean_text(result.stderr or "Okänt fel")[:400]
            raise BackupError("Chip-/säkerhetskontrollen stoppade verktyget: " + reason)
        records = [line[len("TMBOX_DEVICE_JSON:"):] for line in (result.stdout or "").splitlines()
                   if line.startswith("TMBOX_DEVICE_JSON:")]
        try:
            if len(records) != 1:
                raise ValueError()
            info = json.loads(records[0])
            if type(info.get("schema")) is not int or info["schema"] != 1:
                raise ValueError()
            if info.get("chip") not in CHIP_TYPES or info.get("esptoolChip") != CHIP_TYPES[info["chip"]]:
                raise ValueError()
            if not isinstance(info.get("security"), dict) or any(
                    info["security"].get(key) is not False
                    for key in ("flashEncryption", "secureBoot", "secureDownload")):
                raise ValueError()
            if not isinstance(info.get("mac"), str) or not re.fullmatch(
                    r"(?:[0-9a-f]{2}:){5}[0-9a-f]{2}", info["mac"], re.I):
                raise ValueError()
            raw = bytes.fromhex(info["mac"].replace(":", ""))
            if not any(raw) or raw[0] & 1:
                raise ValueError()
            info["mac"] = info["mac"].lower()
        except (ValueError, KeyError, TypeError, AttributeError):
            raise BackupError("Ofullständig eller osäker chipinformation. Ingen flashoperation tillåts.")
        if self.identity and any(info[key] != self.identity[key] for key in ("chip", "esptoolChip", "mac")):
            raise BackupError("Boxen har bytts under körningen. Ingen fortsatt flashoperation tillåts.")
        self.identity, self.chip = info, info["esptoolChip"]
        self.log.append("probe\n" + json.dumps(info) + "\n")
        return info

    def call(self, operation, arguments=(), baud=115200, finish=False):
        if operation not in ALLOWED_OPERATIONS:
            raise BackupError("Otillåten operation i backupverktyget.")
        if self.chip is None:
            raise BackupError("Chip och säkerhetsläge måste kontrolleras först.")
        command = [self.python, "-m", "esptool", "--chip", self.chip,
                   "--port", self.port, "--baud", str(baud), "--after",
                   "hard_reset" if finish else "no_reset", operation, *map(str, arguments)]
        try:
            result = self.runner(command, capture_output=True, text=True,
                                 encoding="utf-8", errors="replace", timeout=3600)
        except subprocess.TimeoutExpired as error:
            self.log.append(operation + ": tidsgränsen överskreds\n")
            raise BackupError("Boxen slutade svara. Kontrollera kabel och port.") from error
        output = (result.stdout or "") + (result.stderr or "")
        self.log.append(operation + "\n" + output + "\n")
        if result.returncode:
            raise BackupError("Läsning eller kontroll misslyckades. Se loggen.")
        return output


def check_flash_identity(output, info):
    match = re.search(r"MAC:\s*((?:[0-9a-f]{2}:){5}[0-9a-f]{2})", output, re.I)
    if not match or match.group(1).lower() != info["mac"]:
        raise BackupError("Flashanslutningens MAC matchar inte chipkontrollen.")


def backup(device, root):
    root = Path(root).expanduser()
    root.mkdir(mode=0o700, parents=True, exist_ok=True)
    folder = root / ("ESP32-" + datetime.now().strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:8])
    folder.mkdir(mode=0o700)
    partial = folder / "OFULLSTANDIG-backup.bin"
    print("\nResultatmapp: " + str(folder))
    succeeded = False
    try:
        print("Kontrollerar att boxen är en ESP32/ESP32-S3 och mäter hela flashminnet...")
        info = device.probe()
        print("Identifierat chip: " + info["chip"] + " · MAC " + info["mac"])
        identification = device.call("flash_id")
        check_flash_identity(identification, info)
        size = flash_size(identification)
        print("Flashminne: " + str(size // 1024) + " KiB.")
        private_text(folder / "flash-info.txt", identification)
        # Keep the firmware stopped until verification. Every command selects
        # the ESP32 bootloader explicitly; no write or erase command exists.
        for attempt, speed in enumerate((460800, 115200)):
            partial.unlink(missing_ok=True)
            partial.touch(mode=0o600, exist_ok=False)
            try:
                print("Läser HELA minnet. Detta kan ta flera minuter — behåll USB-kabeln.")
                device.call("read_flash", ("--no-progress", "0", "ALL", partial), baud=speed)
                if partial.stat().st_size != size:
                    raise BackupError("Backupfilens storlek motsvarar inte hela flashminnet.")
                print("Jämför hela backupfilen med minnet i boxen...")
                # esptool fails with a nonzero status if the device digest and
                # the file differ. Also require its explicit verification result.
                verification = device.call("verify_flash", ("0", partial), baud=speed, finish=False)
                if "-- verify OK (digest matched)" not in verification:
                    raise BackupError("Kontrollen gav inget entydigt godkännande.")
                break
            except BackupError:
                if attempt:
                    raise
                print("Försöket godkändes inte. Läser och kontrollerar på nytt med lägre USB-hastighet...")
        device.probe()  # recheck physical identity/security before approving the backup
        digest = sha256(partial)
        final = folder / "esp32-full-backup.bin"
        partial.rename(final)
        if os.name != "nt":
            final.chmod(0o600)
        metadata = {
            "schema": 1, "toolVersion": VERSION, "esptoolVersion": ESPTOOL_VERSION,
            "createdAt": datetime.now().astimezone().isoformat(),
            "chip": info["chip"], "esptoolChip": info["esptoolChip"], "security": info["security"], "port": device.port,
            "mac": info["mac"], "flashBytes": size,
            "backupFile": final.name, "bytes": final.stat().st_size,
            "sha256": digest, "verifiedAgainstDevice": True,
            "verification": "esptool verify_flash, whole detected flash, device digest matched file",
            "flashWriteOperations": 0,
        }
        private_text(folder / "backup-info.json", json.dumps(metadata, ensure_ascii=False, indent=2) + "\n")
        private_text(folder / "SHA256SUMS.txt", digest + "  " + final.name + "\n")
        private_text(folder / "LAS-MIG.txt",
            "VERIFIERAD FULLSTÄNDIG ESP32-BACKUP\n\n"
            "Spara hela mappen privat. Binärfilen kan innehålla wifi-lösenord.\n"
            "Kopian motsvarade hela boxens flashminne vid kontrollen.\n"
            "Den innehåller program och lokala inställningar, men inte serverns trafikdata.\n"
            "Återställ endast på samma fysiska box, från adress 0 med bibehållna flashparametrar.\n"
            "Backupfilen är INTE avsedd för boxens vanliga Update-sida.\n"
            "Verktyget har ingen skriv-/återställningsfunktion.\n"
            "Tryck RST/RESET på boxen eller koppla ur och anslut USB för normal start.\n")
        succeeded = True
        print("\nKLART — hela säkerhetskopian är verifierad mot boxen.")
        print("Backupfil: " + str(final))
        print("Kontrollsumma: " + digest)
        print("Spara hela resultatmappen privat; filen kan innehålla wifi-lösenord.")
        return folder
    finally:
        try:
            private_text(folder / "backup.log", "".join(device.log))
            if not succeeded:
                private_text(folder / "EJ-GODKAND.txt",
                             "Backupförsöket är INTE godkänt. Använd inte en ofullständig fil för återställning.\n"
                             "Boxens flashminne har inte skrivits av verktyget. Kontrollera backup.log.\n")
        except OSError:
            print("Resultatloggen kunde inte sparas. Kontrollera utrymme och skrivbehörighet i backupmappen.")
        print("Tryck nu RST/RESET på boxen, eller koppla ur och anslut USB, för normal start.")


def runtime_path():
    if sys.platform == "darwin":
        root = Path.home() / "Library" / "Caches"
    elif os.name == "nt":
        root = Path(os.environ.get("LOCALAPPDATA", str(Path.home() / "AppData" / "Local")))
    else:
        root = Path(os.environ.get("XDG_CACHE_HOME", str(Path.home() / ".cache")))
    return root / "TrainMeet" / "ESP32-recovery" / (
        "esptool-" + ESPTOOL_VERSION + "-py" + str(sys.version_info.major) + str(sys.version_info.minor))


def prepare_runtime():
    folder = runtime_path()
    python = folder / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
    check = "import esptool,sys; sys.exit(0 if esptool.__version__ == '" + ESPTOOL_VERSION + "' else 1)"
    ready = False
    if python.is_file():
        ready = subprocess.run([str(python), "-c", check], capture_output=True).returncode == 0
    if not ready:
        print("Förbereder backupverktyget i en egen användarmapp.")
        print("Första gången hämtas esptool " + ESPTOOL_VERSION + " och dess beroenden från Python-paketregistret.")
        print("Det kräver internet. Ingen administratörsbehörighet behövs.")
        venv.EnvBuilder(with_pip=True).create(folder)
        command = [str(python), "-m", "pip", "install", "--disable-pip-version-check",
                   "esptool==" + ESPTOOL_VERSION]
        result = subprocess.run(command, capture_output=True, text=True, timeout=600)
        if result.returncode:
            raise BackupError("Kunde inte hämta esptool. Kontrollera internetanslutningen och försök igen.")
        if subprocess.run([str(python), "-c", check], capture_output=True).returncode:
            raise BackupError("Verktygets version kunde inte kontrolleras.")
    return python


def select_port():
    from serial.tools import list_ports
    while True:
        ports = sorted(list_ports.comports(), key=lambda p: p.device)
        print("\nUSB-/serieportar:")
        for number, port in enumerate(ports, 1):
            print(str(number) + ". " + clean_text(port.device) + " — " + clean_text(port.description))
        if not ports:
            print("Ingen port hittades. Anslut en USB-datakabel och stäng seriella monitorer.")
        choice = input("Välj portnummer, R för ny sökning, M för manuell port eller A för avbryt: ").strip()
        if choice.lower() == "a":
            return None
        if choice.lower() == "r":
            continue
        if choice.lower() == "m":
            port = input("Portnamn (t.ex. COM4 eller /dev/cu.usbserial-...): ").strip()
            if port:
                return port
        if choice.isdigit() and 1 <= int(choice) <= len(ports):
            return ports[int(choice) - 1].device
        print("Välj ett av alternativen i listan.")


def main(argv=None):
    parser = argparse.ArgumentParser(description="Fullständig, verifierad ESP32-backup. Skriver aldrig till flashminnet.")
    parser.add_argument("--port", help="USB-port; annars väljs port i en lista")
    parser.add_argument("--output", help="Backupmapp; annars frågar programmet")
    parser.add_argument("--worker", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--version", action="version", version=VERSION)
    args = parser.parse_args(argv)
    if sys.version_info < MIN_PYTHON:
        print("Python 3.10 eller senare behövs. Se LAS-MIG.txt.")
        return 1
    try:
        if not args.worker:
            python = prepare_runtime()
            forwarded = list(sys.argv[1:] if argv is None else argv)
            return subprocess.run([str(python), str(Path(__file__).resolve()), "--worker", *forwarded]).returncode
        import esptool
        if esptool.__version__ != ESPTOOL_VERSION:
            raise BackupError("Fel esptool-version. Starta med Mac-/Windows-startfilen.")
        print("TrainMeet ESP32-backup " + VERSION)
        print("Boxen pausas under kopieringen. Ingen firmware installeras och flashminnet raderas inte.")
        port = args.port or select_port()
        if not port:
            print("Avbrutet. Ingen backup har tagits.")
            return 0
        default = Path.home() / "TMBox-backuper-ESP32"
        entered = args.output if args.output else input("Spara i mapp [" + str(default) + "]: ").strip().strip('"')
        folder = Path(entered).expanduser() if entered else default
        backup(Device(sys.executable, port), folder)
        return 0
    except (KeyboardInterrupt, EOFError):
        print("\nAvbrutet. Ingen kopia är godkänd om KLART inte visades. Starta om boxen med RST/RESET.")
        return 130
    except (BackupError, OSError, subprocess.SubprocessError) as error:
        print("\nBACKUPEN ÄR INTE GODKÄND: " + clean_text(error))
        print("Behåll boxens befintliga firmware. Tryck RST/RESET eller återanslut USB innan nytt försök.")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
