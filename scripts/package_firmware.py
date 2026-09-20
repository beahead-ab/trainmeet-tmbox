#!/usr/bin/env python3
"""Build IDE-specific downloads from canonical firmware; never maintain a fork.

The Arduino download compiles the firmware once as C++, exactly as PlatformIO
does. Its .ino is an intentionally empty entry point, not an include of a .cpp
or .ino. Own supporting code is local to the sketch. Third-party dependencies
are pinned (not vendored); CLI profiles additionally isolate library discovery.
"""
from __future__ import annotations

import argparse
import configparser
import hashlib
import json
from pathlib import Path
import re
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parent.parent
PROFILES = {
    "esp32-s3": ("esp32", "TrainMeetTMBox_S3", "ESP32S3 Dev Module", "esp32:esp32:esp32s3:FlashSize=8M,PSRAM=enabled", 2),
    "esp32-benny": ("esp32", "TrainMeetTMBox_Benny", "ESP32 Dev Module", "esp32:esp32:esp32", 1),
    "esp32-classic-safe": ("esp32", "TrainMeetTMBox_Classic", "ESP32 Dev Module", "esp32:esp32:esp32", 3),
    "nodemcu-i2c": ("esp8266", "TrainMeetTambox8266", "NodeMCU 1.0 (ESP-12E Module)", "esp8266:esp8266:nodemcuv2", None),
    "nodemcu-hardware-check": ("esp8266", "TrainMeetTambox8266_Test", "NodeMCU 1.0 (ESP-12E Module)", "esp8266:esp8266:nodemcuv2", None),
}
CORES = {
    "esp32": ("esp32:esp32", "2.0.17", "https://espressif.github.io/arduino-esp32/package_esp32_index.json"),
    "esp8266": ("esp8266:esp8266", "3.1.2", "https://arduino.esp8266.com/stable/package_esp8266com_index.json"),
}
# The registries have different LCD release catalogs. Arduino's official
# library_index has 1.1.2, NOT PlatformIO's 1.1.4. Both builds are tested in CI;
# do not silently translate a future PlatformIO update to this older version.
ARDUINO_OVERRIDES = {("LiquidCrystal_I2C", "1.1.4"): ("LiquidCrystal I2C", "1.1.2")}


def dependencies(root: Path, family: str) -> list[tuple[str, str]]:
    config = configparser.ConfigParser(interpolation=None)
    config.read(root / "firmware" / family / "platformio.ini")
    result = []
    for line in config["env"]["lib_deps"].splitlines():
        if not line.strip():
            continue
        match = re.fullmatch(r"[^/]+/([\w-]+)@(\d+\.\d+\.\d+)", line.strip())
        if not match:
            raise ValueError(f"Dependency must use an exact version: {line}")
        name, version = match.groups()
        if name == "LiquidCrystal_I2C" and (name, version) not in ARDUINO_OVERRIDES:
            raise ValueError("LCD version needs an explicitly tested Arduino counterpart")
        result.append(ARDUINO_OVERRIDES.get((name, version), (name, version)))
    return result


def source_file(root: Path, family: str) -> Path:
    if family == "esp32":
        return root / "firmware/esp32/TrainMeetTMBox.ino"
    return root / "firmware/esp8266/TrainMeetTambox8266/TrainMeetTambox8266.ino"


def esp8266_debug_instructions(hardware_file: str) -> bytes:
    return f"""
## Frivilligt debugläge för ESP8266

Debug är av som standard. I **Arduino IDE**, välj **Verktyg → Debug port →
Serial** för mer felsökningsinformation. **Debug Level** kan stå kvar på
**None**; det valet gäller ESP8266-kortstödets egna loggar. Du behöver inte
ändra källkoden. Välj **Debug port → Disabled** för att stänga av igen.
I Arduino CLI motsvaras menyvalet av `--board-options dbg=Serial` på
kommandot `arduino-cli compile`.

I **PlatformIO**, eller för att använda den tidigare manuella inställningen,
öppna `{hardware_file}` och ta bort `//` framför:

```cpp
#define TAMBOX_DEBUG_ENABLED 1
```

Kompilera och ladda upp samma profil igen. Öppna seriell monitor med
**115200 baud**. Debugrader anger funktion, källkodens radnummer och antal
millisekunder sedan start (`millis`). Inget extra bibliotek eller debugpaket
behövs. En uttrycklig `TAMBOX_DEBUG_ENABLED` (`0` eller `1`) går före
Arduino-menyn för TMBox-loggarna. Kommentera bort flaggan för att följa
menyvalet igen. I PlatformIO är debug av utan flaggan; byggflaggan
`-DTAMBOX_DEBUG_ENABLED=1` fungerar också. Behåll profilens övriga byggflaggor.

Normala statusrader finns kvar även när debug är av. Webbpanelen kräver ingen kod.
Hårdvarutestprogrammet har ingen webbpanel. Flaggan ändrar inte
nätinställningar, stationstilldelning eller MQTT-protokoll.
""".encode()


def arduino_files(root: Path, profile: str) -> dict[str, bytes]:
    family, sketch, board, fqbn, number = PROFILES[profile]
    source = source_file(root, family)
    config = '// Generated build selection; choose another download to change hardware.\n#pragma once\n#include <Arduino.h>\n'
    if number is not None:
        config += f"#define TMBOX_HARDWARE_PROFILE {number}\n"
    if profile == "nodemcu-hardware-check":
        config += "#define TAMBOX_HARDWARE_CHECK 1\n"
    guard = "ESP32" if family == "esp32" else "ESP8266"
    config += f'#ifndef {guard}\n#error "Wrong board: select {board}"\n#endif\n'
    if family == "esp32":
        chip = 'ESP32S3' if number == 2 else 'ESP32'
        config += f'#ifndef CONFIG_IDF_TARGET_{chip}\n#error "Wrong ESP32 chip: select {board}"\n#endif\n'
    files = {
        f"{sketch}/{sketch}.ino": (
            "// Open this file in Arduino IDE. The firmware is compiled ONCE\n"
            "// from TrainMeetFirmware.cpp and the supporting .cpp files.\n"
            "// Do not add a main.cpp or include a .cpp/.ino file here.\n"
            '#include <Arduino.h>\n'
        ).encode(),
        f"{sketch}/TrainMeetBuild.h": config.encode(),
        f"{sketch}/TrainMeetFirmware.cpp": b'#include "TrainMeetBuild.h"\n' + source.read_bytes().replace(b'../../common/server_terminal.h', b'server_terminal.h').replace(b'../common/server_terminal.h', b'server_terminal.h'),
        f"{sketch}/server_terminal.h": (root / "firmware/common/server_terminal.h").read_bytes(),
    }
    supporting = list(source.parent.glob("*.h"))
    if family == "esp32":
        supporting += list((root / "firmware/esp32/lib/tmbox_core").glob("*.h"))
        supporting += list((root / "firmware/esp32/lib/tmbox_core").glob("*.cpp"))
    for path in sorted(supporting):
        name = f"{sketch}/{path.name}"
        if name in files:
            raise ValueError(f"Duplicate sketch file: {name}")
        files[name] = path.read_bytes()
    core, version, index = CORES[family]
    libraries = dependencies(root, family)
    yaml = (
        "# CLI-only isolated build. Arduino IDE users follow START-HERE.md.\n"
        f"profiles:\n  build:\n    fqbn: {fqbn}\n    platforms:\n"
        f"      - platform: {core} ({version})\n        platform_index_url: {index}\n"
        "    libraries:\n" + "".join(f"      - {name} ({v})\n" for name, v in libraries)
    )
    files[f"{sketch}/sketch.yaml"] = yaml.encode()
    library_list = "\n".join(f"   - **{name} {v}**" for name, v in libraries)
    hardware = ("ESP32-S3, 20×4 LCD och direktkopplad knappsats" if profile == "esp32-s3"
                else "klassisk ESP32, 16×2 LCD och direktkopplad knappsats" if family == "esp32"
                else "NodeMCU ESP8266, 16×2 LCD och separat PCF8574-knappsats via I²C")
    warning = ("Detta är ENDAST hårdvarutestet. Det ansluter inte till servern. Installera nodemcu-i2c efter bänktestet."
               if profile == "nodemcu-hardware-check" else "Detta är huvudprogrammet för anslutning till den lokala TrainMeet Server.")
    python_note = ('''På Linux behöver ESP32-kortstödets verktyg även Python-paketen för esptool.
Om `No module named serial` visas, skapa en isolerad verktygsmiljö:

```sh
python3 -m venv .trainmeet-tools
. .trainmeet-tools/bin/activate
python -m pip install esptool==4.5.1
```

Kör sedan Arduino CLI-kommandot i samma terminal. Det ändrar inte dina
Arduino-bibliotek. Detta är ett verktygsfel, inte dubbel firmware.
''' if family == "esp32" else "")
    web_test_note = """
## Testa NodeMCU med telefon, utan display och knappsats

Huvudprogrammet nodemcu-i2c har en lokal webbpanel med display och alla
16 tangenter (0–9, A–D, * och #). Läs boxens webbadress i
seriell monitor (115200 baud) efter Wi-Fi-anslutningen och öppna den på
telefonen. Ingen webbtestkod, serveradress, port eller anslutningskod behövs.

Boxen upptäcker TrainMeet Server automatiskt på det lokala nätet.
Administratören tilldelar boxens enhetskod en station på servern.
Aktivera webbtest när serverns panel visas. Tågnumrets siffror stannar
i telefonen tills # bekräftar hela numret; * avbryter utan siffrorna.
Knapparna påverkar den anslutna träffen på riktigt efter tilldelning:
använd en separat testträff. Hårdvarutestprogrammet har ingen webbpanel.
""" if profile == "nodemcu-i2c" else ""
    files["START-HERE.md"] = f"""# TrainMeet TMBox – Arduino IDE

**Vald profil: {profile}** · {hardware}

{warning}

## Steg för steg

1. Packa upp HELA ZIP-filen i en ny mapp. Blanda inte med en tidigare nedladdning.
   Detta är ett program, inte ett Arduino-bibliotek: använd inte ”Add .ZIP Library”.
2. I Arduino IDE: Inställningar → Ytterligare kort-URL:er, lägg till:
   `{index}`
3. Installera kortstödet **{core} {version}** i Boards Manager.
4. Installera exakt dessa bibliotek i Library Manager:
{library_list}
   Wi-Fi, Wire och övriga kortbibliotek ingår i kortstödet; installera inte
   separata gamla kopior. TrainMeets egna stödfiler finns redan i paketet.
5. Öppna **{sketch}/{sketch}.ino**. Alla filer i samma mapp måste följa med.
   Filen är avsiktligt liten: Arduino kompilerar automatiskt
   `TrainMeetFirmware.cpp` och stödfilerna, en gång vardera.
6. Välj **{board}** och rätt USB-port. För S3: 8 MB flash, QSPI PSRAM
   (referenskort N8R2). För NodeMCU: 80 MHz, 4 MB flash.
7. Kontrollera `hardware_profile.h` och kopplingen innan du laddar upp.
   `TrainMeetBuild.h` väljer profil; profilnamnet Benny innebär inte att
   varje kort hos Benny har denna koppling. Byt paket om hårdvaran skiljer sig.
8. Klicka **Verifiera**, sedan **Ladda upp**. Öppna seriell monitor, **115200 baud**.

**Ingen fysisk box har verifierats av detta byggtest.** Kontrollera matning,
I²C-nivåomvandling och knappsatsens koppling. Laddning ersätter kortets
befintliga firmware; behåll originalet om du behöver kunna återgå.

## Kompilera med ett kommando (Arduino CLI)

Med Arduino CLI 1.3.1 eller senare installerat, kör i den uppackade mappen:

```sh
arduino-cli compile --profile build {sketch}
```

Profilen hämtar angivna kortstöd/bibliotek och bygger isolerat från gamla
globala bibliotek. Arduino IDE använder däremot installationerna från steg 3–4;
`sketch.yaml` är inte ett löfte om automatisk bibliotekshantering i IDE:n.

{python_note}

## Första starten och stationen

Anslut till boxens tillfälliga TrainMeet-Wi-Fi och välj träffens lokala nät.
{("Ange den lokala servern om automatisk upptäckt inte fungerar." if family == "esp32" else "Servern hittas automatiskt; inga serverfält eller koder används.")} Den lokala
administratören kopplar sedan boxens permanenta ID till stationen i
TrainMeet Server. Boxen väljer aldrig station själv. Cloud används inte i drift.
Hårdvarutestet behöver inget Wi-Fi och gör ingen stationstilldelning.

{web_test_note}

## Om ”Multiple libraries were found” visas

Läs raderna **Used** och **Not used**: bara det förstnämnda biblioteket används.
Kontrollera namn/version enligt listan ovan. Radera inte alla bibliotek.
Använd den isolerade CLI-kommandoraden om gamla installationer konkurrerar.
Vid `multiple definition of setup/loop`: börja i en ny uppackad mapp; kopiera
inte in PlatformIO:s `src/main.cpp` eller en extra .ino från GitHub.

Kopplingsguide och serveranslutning:
https://github.com/beahead-ab/trainmeet-tmbox/blob/main/firmware/{family}/README.md
""".encode()
    if family == "esp8266":
        files["START-HERE.md"] += esp8266_debug_instructions(f"{sketch}/hardware_profile.h")
    return files


def platformio_files(root: Path, family: str) -> dict[str, bytes]:
    folder = root / "firmware" / family
    # Explicit source allowlist; never ship .pio caches or somebody's libraries.
    paths = [folder / "platformio.ini", source_file(root, family), folder / "src/main.cpp"]
    paths += list(source_file(root, family).parent.glob("*.h"))
    if family == "esp32":
        paths += list((folder / "lib/tmbox_core").glob("*.h"))
        paths += list((folder / "lib/tmbox_core").glob("*.cpp"))
    files = {p.relative_to(folder).as_posix(): p.read_bytes() for p in sorted(set(paths))}
    # Standalone bundles must not reach outside their extraction directory.
    name = source_file(root, family).relative_to(folder).as_posix()
    files[name] = files[name].replace(b'../../common/server_terminal.h', b'server_terminal.h').replace(b'../common/server_terminal.h', b'server_terminal.h')
    header = source_file(root, family).parent.relative_to(folder) / "server_terminal.h"
    files[header.as_posix()] = (root / "firmware/common/server_terminal.h").read_bytes()
    profiles = [p for p, value in PROFILES.items() if value[0] == family]
    example = profiles[0]
    files["START-HERE.md"] = f"""# TrainMeet TMBox – PlatformIO ({family})

1. Packa upp hela paketet i en ny mapp. Öppna mappen med `platformio.ini`
   i Visual Studio Code + PlatformIO. Detta paket är INTE för Arduino IDE.
2. Välj hårdvaruprofil: **{', '.join(profiles)}**.
3. Kör i PlatformIO-terminalen (byt profil om ditt kort är ett annat):

```sh
pio run -e {example}
pio run -e {example} -t upload
pio device monitor -b 115200
```

PlatformIO hämtar versionslåsta kortstöd och bibliotek. `src/main.cpp` läser
in den gemensamma .ino-källan en gång. Kompilera projektet, inte filerna var för
sig. Kopiera inte hit bibliotek från Arduino IDE och ändra inte `src_dir`.
I ESP8266-paketet finns också `nodemcu-hardware-check` för bänktest utan Wi-Fi.

Kontrollera pinning, matning och I²C-nivåomvandling innan laddning. Detta är
byggtestat, inte fysiskt hårdvaruverifierat. Laddning ersätter tidigare firmware.
Boxen ansluter till lokal TrainMeet Server; administratören tilldelar stationen
utifrån boxens permanenta ID. Boxen väljer inte station och använder inte Cloud.

Kopplingsguide:
https://github.com/beahead-ab/trainmeet-tmbox/blob/main/firmware/{family}/README.md
""".encode()
    if family == "esp8266":
        files["START-HERE.md"] += esp8266_debug_instructions("TrainMeetTambox8266/hardware_profile.h")
    return files


def write_package(output: Path, name: str, files: dict[str, bytes], version: str, revision: str) -> Path:
    files = dict(files)
    title, _, body = files['START-HERE.md'].decode().partition('\n')
    files['START-HERE.md'] = f'{title}\n\n**Version {version} · källrevision {revision[:12]}**\n{body}'.encode()
    files["PACKAGE.json"] = (json.dumps({
        "format": 1, "version": version, "source_revision": revision,
        "files": {p: hashlib.sha256(data).hexdigest() for p, data in sorted(files.items())},
    }, indent=2) + "\n").encode()
    path = output / f"{name}.zip"
    # Refuse to replace an existing package, so unreviewed reruns cannot silently
    # change a delivered archive under the same filename.
    with zipfile.ZipFile(path, "x", compression=zipfile.ZIP_DEFLATED) as archive:
        for filename, data in sorted(files.items()):
            info = zipfile.ZipInfo(f"{name}/{filename}", date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, data)
    return path


def build(root: Path, output: Path, revision: str) -> list[Path]:
    version = (root / "VERSION").read_text().strip()
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise ValueError("Invalid VERSION")
    output.mkdir(parents=True, exist_ok=True)
    packages = []
    for profile in PROFILES:
        packages.append(write_package(output, f"trainmeet-tmbox-arduino-{profile}", arduino_files(root, profile), version, revision))
    for family in CORES:
        packages.append(write_package(output, f"trainmeet-tmbox-platformio-{family}", platformio_files(root, family), version, revision))
    checksums = "".join(f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n" for p in packages)
    with (output / "SHA256SUMS.txt").open("x") as stream:
        stream.write(checksums)
    return packages


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    for package in build(ROOT, args.output, revision):
        print(package)
