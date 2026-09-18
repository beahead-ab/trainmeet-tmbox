# Ladda ner TrainMeet TMBox

Öppna **[GitHub Releases](https://github.com/beahead-ab/trainmeet-tmbox/releases)**
och välj den senaste versionen med paketen nedan under **Assets**.
Välj först kort, sedan utvecklingsmiljö. Ladda inte ner ”Source code (zip)”
om du vill ha det färdiga Arduino-paketet.

| Ditt kort och koppling | Arduino IDE-paket |
|---|---|
| NodeMCU / ESP8266, I²C-knappsats via PCF8574, 16×2 LCD | `trainmeet-tmbox-arduino-nodemcu-i2c.zip` |
| Samma NodeMCU, enbart test av display och knappsats utan Wi-Fi | `trainmeet-tmbox-arduino-nodemcu-hardware-check.zip` |
| TMBox v2: ESP32-S3 N8R2, direktkopplad knappsats, 20×4 LCD | `trainmeet-tmbox-arduino-esp32-s3.zip` |
| Klassisk ESP32 med dokumenterad Benny-pinning | `trainmeet-tmbox-arduino-esp32-benny.zip` |
| Klassisk ESP32 med alternativ safe-pinning | `trainmeet-tmbox-arduino-esp32-classic-safe.zip` |

Namnet **Benny** är en viss kopplingsprofil, inte ett löfte om att den passar
alla hans boxar. En NodeMCU behöver alltid ESP8266-paketet, inte `esp32-benny`.
I²C-knappsats och direktkopplad knappsats är olika hårdvara. Kontrollera
kortets märkning och `hardware_profile.h`.

## Arduino IDE – gör så här

1. Hämta rätt **arduino**-ZIP ovan. Packa upp **hela filen i en ny mapp**.
   Lägg den inte ovanpå en gammal kopia. Använd inte ”Add .ZIP Library” –
   nedladdningen är ett program, inte ett bibliotek.
2. Läs **START-HERE.md** i paketet. Där står exakt kortstöd, biblioteksversioner
   och den enda `.ino`-fil du ska öppna.
3. Installera angivet kortstöd och biblioteken i Arduino IDE:s hanterare.
   Egna TrainMeet-stödfiler följer redan med. Tredjepartsbibliotek laddas ned
   via hanteraren och behöver inte kopieras från andra projekt.
4. Öppna `.ino`-filen i dess mapp. Flytta inte ut den ensam.
   Att den är liten är avsiktligt: Arduino kompilerar automatiskt
   `TrainMeetFirmware.cpp` och övriga `.cpp`-filer **en gång vardera**.
5. Välj kort och USB-port, kontrollera kopplingen och klicka **Verifiera**.
   Klicka därefter **Ladda upp**. Seriell monitor använder **115200 baud**.

Paketet innehåller också en `sketch.yaml` för Arduino CLI, med låsta kortstöd
och bibliotek. Kommandot i START-HERE bygger isolerat från gamla globala
bibliotek. Arduino IDE använder däremot installationerna i steg 3 – den
automatiska CLI-hanteringen ska inte förväxlas med IDE-menyerna.

## PlatformIO

Välj **`trainmeet-tmbox-platformio-esp32.zip`** eller
**`trainmeet-tmbox-platformio-esp8266.zip`**. Packa upp i en ny mapp och öppna
mappen som innehåller `platformio.ini` i Visual Studio Code + PlatformIO.
START-HERE beskriver profilval och bygg-/uppladdningskommandon. Kortstöd och
direkta bibliotek är versionslåsta och hämtas av PlatformIO.

Öppna inte det här paketet i Arduino IDE. PlatformIO:s `src/main.cpp` läser
in originalets `.ino` en gång; den ska inte kopieras in i Arduino-paketet.

## Dubbla bibliotek och ”rundgång”

- `Multiple libraries were found`, `Used`, `Not used`: verktyget har hittat
  flera möjliga bibliotek men väljer bara ett. Kontrollera det använda
  bibliotekets namn, version och sökväg mot START-HERE. Radera inte alla
  bibliotek. Den isolerade CLI-profilen undviker globala dubbletter.
- `multiple definition of setup` eller `loop`: samma program har tagits med
  mer än en gång. Börja i en ny uppackad Arduino-mapp utan `main.cpp` eller
  extra `.ino`-kopior. Arduino-paketet innehåller ingen PlatformIO-startfil.
- `model.h: No such file`: en lös ESP32-`.ino` saknar TrainMeets stödfiler.
  Hämta hela Arduino-paketet; installera inte något slumpmässigt ”model”-bibliotek.

Arduino- och PlatformIO-paketen **genereras från samma källkod och version**.
Ingen separat firmwarevariant underhålls för Arduino IDE. `PACKAGE.json`
anger källrevision och kontrollsummor; `SHA256SUMS.txt` hör till nedladdningarna.
Källorna för ESP8266 respektive ESP32 är fortfarande olika hårdvaruklienter
som talar med samma lokala TrainMeet Server. Paketeringen ändrar inte protokoll.

**LCD-bibliotekets versionsnummer skiljer mellan katalogerna:** Arduino
Library Manager har LiquidCrystal I2C **1.1.2**, medan PlatformIO-paketet heter
LiquidCrystal_I2C **1.1.4**. Den äldre guiden angav felaktigt 1.1.4 även för
Arduino IDE. Nu står respektive systems byggtestade version i START-HERE.

## Efter laddning

Boxen ansluter till träffens lokala nätverk och TrainMeet Server.
**Administratören tilldelar stationen i servern utifrån boxens permanenta ID.**
Boxen väljer inte station själv, och TrainMeet Cloud behövs inte i drift.

Paketen kompileras automatiskt före publicering, både som vanliga
Arduino-byggen och med isolerade CLI-profiler, samt med PlatformIO.
Det är **inte ett fysiskt hårdvarutest**. Kontrollera matning, I²C-nivåomvandling,
display och tangentmatris innan trafikdrift. Laddning ersätter kortets gamla
firmware; spara originalet om du behöver kunna gå tillbaka.

## För utvecklare

```sh
python3 scripts/package_firmware.py --output downloads
python3 -m unittest discover -s tests -p test_firmware_packages.py
```

Använd en ny utdatamapp vid ombyggnad. Generatorn skriver aldrig över ett
tidigare paket. Workflow **Verified firmware downloads** kompilerar de
uppackade ZIP-filerna (inte en annan arbetskopia), och publicerar först när
samtliga profiler är gröna och taggen stämmer med VERSION.

Referenser:
[Arduino sketchstruktur](https://docs.arduino.cc/arduino-cli/sketch-specification),
[isolerade CLI-profiler](https://docs.arduino.cc/arduino-cli/sketch-project-file),
[biblioteksmeddelandet](https://support.arduino.cc/hc/en-us/articles/4406379650578-Error-Multiple-libraries-were-found).
