# TrainMeet Tambox

Detta repo innehåller programvaran för den fysiska TrainMeet Tamboxen: ESP32, 16×2 LCD och 4×4-tangentbord. Boxen är en tunn och självläkande klient till [TrainMeet Server](https://github.com/beahead-ab/trainmeet-server) på Raspberry Pi.

Repot innehåller inte iPhone-appen. Den utvecklas separat i [trainmeet-iphone](https://github.com/beahead-ab/trainmeet-iphone).

## Grundprincip

Tamboxen fattar inga trafikbeslut. Den skickar tangenttryckningar och visar kompletta, auktoritativa skärmbilder från Raspberry Pi:n. Om Wi-Fi eller MQTT försvinner väntar boxen, återansluter och hämtar ett nytt fullständigt läge. Gamla tangenttryckningar köas inte.

Varje box har ett permanent id och en kort kod, exempelvis `TBX-A7K2`. Vid start visas koden på displayen. I serverns webbadmin kopplar administratören koden till en station och panel A–D. Klienten behöver inget lösenord.

## Wi-Fi vid första start

1. Boxen försöker ansluta till senast sparade Wi-Fi.
2. Om nätet saknas skapar den tillfälligt nätverket `TrainMeet-XXXX`.
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

Det gemensamma protokollet och arkitekturbesluten finns i [docs/architecture.md](docs/architecture.md).

