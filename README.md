# TrainMeet Tambox

Detta repo innehåller programvaran för den fysiska TrainMeet Tamboxen: ESP32 eller NodeMCU/ESP8266, 16×2 LCD och 4×4-tangentbord. Boxen är en tunn och självläkande klient till [TrainMeet Server](https://github.com/beahead-ab/trainmeet-server), oavsett om servern körs på Raspberry Pi, Mac, PC eller Linux.

Repot innehåller inte iPhone-appen. Den utvecklas separat i [trainmeet-iphone](https://github.com/beahead-ab/trainmeet-iphone).

## Grundprincip

Tamboxen fattar inga trafikbeslut. Den skickar tangenttryckningar och visar kompletta, auktoritativa skärmbilder från TrainMeet Server. Om Wi-Fi eller MQTT försvinner väntar boxen, återansluter och hämtar ett nytt fullständigt läge. Gamla tangenttryckningar köas inte. Cloud används inte i träffens runtime.

Varje box har ett permanent id och en kort kod, exempelvis `TBX-A7K2`. Vid start visas koden på displayen. I serverns webbadmin kopplar administratören koden till en station och panel A–D. Klienten behöver inget lösenord.

## Wi-Fi vid första start

1. Boxen försöker ansluta till senast sparade Wi-Fi.
2. Om nätet saknas skapar den tillfälligt nätverket `TrainMeet-XXXX`.
3. Anslut med en telefon och välj träffens Wi-Fi i portalen.
4. Uppgifterna lagras i kortets beständiga minne.
5. Håll `*` i fem sekunder: ESP32 rensar Wi-Fi och startar om; ESP8266 öppnar installationsportalen utan att först radera det sparade nätet.

Servern hittas automatiskt med mDNS/Bonjour. En serveradress kan också anges manuellt i Wi-Fi-portalen.

## Bygg och ladda firmware

### NodeMCU / ESP8266 med I²C-knappsats

Se den kompletta [NodeMCU-guiden](firmware/esp8266/README.md) för koppling,
Arduino IDE, PlatformIO och första uppstart. Profilen kräver en
**PCF8574/PCF8574A-knappsatsmodul**, inte bara valfri modul märkt I²C.

```sh
cd firmware/esp8266
pio run -e nodemcu-hardware-check -t upload
# Kontrollera display och alla 16 tangenter, ladda sedan huvudprogrammet:
pio run -e nodemcu-i2c -t upload
```

Standard: D2/GPIO4 = SDA, D1/GPIO5 = SCL, knappsatsadress `0x20`, LCD `0x27`.
Kontrollera 3,3 V/5 V och nivåomvandling enligt guiden innan anslutning.

### ESP32 / ESP32-S3

[PlatformIO](https://platformio.org/) är den rekommenderade vägen:

```sh
cd firmware/esp32
pio run -e esp32-benny
pio run -e esp32-benny -t upload
```

Det finns tre hårdvaruprofiler:

- `esp32-benny` följer de pin-val som identifierats i Bennys befintliga kod.
- `esp32-classic-safe` undviker boot-strapping-pinnen GPIO12 vid ny kabeldragning.
- `esp32-s3` är profilen för en framtida ESP32-S3-baserad box.

Arduino IDE kan också användas. Instruktioner och bibliotek finns i [firmware/esp32/README.md](firmware/esp32/README.md). Komplett koppling av display, tangentbord, ström och nivåanpassning finns i [WIRING.md](firmware/esp32/WIRING.md).

## Hårdvarustatus

Protokoll, felåterhämtning och boxbeteende är implementerade. Den slutliga produktionsprofilen ska verifieras mot Bennys faktiska komponenter, kortmodell, I2C-adress, kablage och elektriska nivåer innan firmware laddas i de befintliga lådorna.

Det gemensamma protokollet och arkitekturbesluten finns i [docs/architecture.md](docs/architecture.md).
