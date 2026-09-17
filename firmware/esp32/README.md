# TrainMeet TMBox firmware

Vill du slippa kompilera? Börja med den gemensamma
[installationsguiden med färdig USB-firmware](../../docs/INSTALLATION.md).

Firmwaren gör TMBox v2 till en lokal klient för TrainMeet Server över
protokoll v2. Servern äger alla trafikbeslut. Boxen cachar stationens
konfiguration och aktuella snapshot i RAM, navigerar lokalt och skickar bara
kompletta kommandon. Produktens beteende beskrivs i
[`docs/tmbox.md`](../../docs/tmbox.md).

## Referenshårdvara

Standardmålet `esp32-s3` motsvarar TMBox v2:

- ESP32-S3-DevKitC-1-N8R2
- 20×4 HD44780-display på I2C-adress `0x27` via nivåomvandlare
- passiv 4×4-knappsats direkt på GPIO
- passiv summer och separat statusindikering

Alla pinnar finns i `hardware_profile.h`. Följ
[WIRING.md](WIRING.md) och
[`docs/TMBOX-V2-HARDWARE.md`](../../docs/TMBOX-V2-HARDWARE.md) innan något
ansluts. Kör hårdvarukontrollen i `diagnostics/hardware-check` före första
fullständiga flashningen.

## Bygg och ladda

Med PlatformIO:

```sh
pio run -e esp32-s3
pio run -e esp32-s3 -t upload
```

Miljöerna `esp32-benny` och `esp32-classic-safe` finns kvar som
utvecklingsprofiler för klassiska ESP32-kort. De beskriver inte TMBox v2 och
ska inte användas för den nya referenskonstruktionen.

För Arduino IDE krävs ArduinoJson, ArduinoMqttClient, Keypad,
LiquidCrystal_I2C och WiFiManager. Öppna därefter `TrainMeetTMBox.ino`.

## Drift

Vid start visar boxen sitt permanenta id, ansluter till sparat Wi-Fi, hittar
servern via mDNS och hämtar sin stationstilldelning, konfiguration och
snapshot. Saknas Wi-Fi öppnas nätet `TrainMeet-XXXXXX` för provisionering.
Nät- och serveravbrott är normala tillstånd: boxen återansluter med backoff
och tillåter inga skrivande kommandon innan färsk serverdata har hämtats.

Renderare, navigation, kommandon och uppmärksamhetspolicy ligger i
`lib/tmbox_core/` och verifieras utan hårdvara genom native-tester och
guldfiler. Fysisk verifiering görs med
[`docs/BANKTEST.md`](../../docs/BANKTEST.md).
