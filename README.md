# TrainMeet TMBox

Detta repo innehåller programvaran för den fysiska TrainMeet TMBoxen: ESP32, 16×2 LCD och 4×4-tangentbord. Boxen är en tunn och självläkande klient till [TrainMeet Server](https://github.com/beahead-ab/trainmeet-server), och talar protokoll v2 (`tmbox/v2/...`).

Repot innehåller inte iPhone-appen. Den utvecklas separat i [trainmeet-iphone](https://github.com/beahead-ab/trainmeet-iphone).

## Grundprincip

TMBoxen fattar inga trafikbeslut. Den cachar sin tilldelade stations konfiguration och aktuella läge lokalt i RAM, bläddrar i den cachen direkt utan nätverksfördröjning, och pratar bara på tråden när den skickar ett komplett kommando (inga tangenttryckningar en och en). Om Wi-Fi eller MQTT försvinner väntar boxen, återansluter och hämtar ett nytt fullständigt läge.

Varje box har ett permanent id och en kort kod, exempelvis `TMBOX-A7K2C3`. Vid start visas koden på displayen. I serverns webbadmin kopplar administratören koden till en station. Klienten behöver inget lösenord.

## Wi-Fi vid första start

1. Boxen försöker ansluta till senast sparade Wi-Fi.
2. Om nätet saknas skapar den tillfälligt nätverket `TrainMeet-XXXXXX`.
3. Anslut med en telefon och välj träffens Wi-Fi i portalen.
4. Uppgifterna lagras i ESP32:ans beständiga minne.
5. Håll `*` i fem sekunder för att rensa Wi-Fi och börja om.

Servern hittas automatiskt med mDNS/Bonjour. En serveradress kan också anges manuellt i Wi-Fi-portalen.

## Bygg och ladda firmware

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

## Dokumentation

Den fullständiga produktbeskrivningen — namngivning, arkitektur, protokoll,
skärmflöden, tester och definition of done — finns i [docs/tmbox.md](docs/tmbox.md).
[docs/architecture.md](docs/architecture.md) beskriver det äldre, fortfarande
driftsatta MQTT v1-protokollet. Beslutshistoriken bakom `docs/tmbox.md` finns i
[docs/underlag/](docs/underlag/).

