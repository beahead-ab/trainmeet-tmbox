# TrainMeet TMBox firmware

Vill du slippa kompilera? Börja med den gemensamma
[installationsguiden med färdig USB-firmware](../../docs/INSTALLATION.md).

Från firmware 0.7.0 används samma serverstyrda 16×2-profil som på ESP8266.
Server 1.10.0 eller senare krävs och ska uppdateras först. Servern äger
trafikbeslut, skärmar, språk och tangentfunktioner. Boxen hanterar hårdvara,
transport och lokal sifferbuffring. [Aktuell användning](../../README.md).
Den äldre lokala kärnan och dess golden-filer är kompatibilitetsunderlag.

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

För Arduino IDE: hämta det särskilda
[Arduino-paketet för din profil](../../docs/FIRMWARE-DOWNLOADS.md) och följ
START-HERE.md. Paketet innehåller alla egna stödfiler och rätt biblioteksversioner
anges där. Öppna inte en lös `TrainMeetTMBox.ino` ur detta repo: den behöver
`lib/tmbox_core`, och PlatformIO:s `src/main.cpp` får inte följa med i en
Arduino-sketch. De nedladdningsbara paketen byggs från exakt samma källkod.

## Drift

Vid start visar boxen sitt permanenta id, ansluter till sparat Wi-Fi, hittar
servern via mDNS och hämtar sin stationstilldelning, konfiguration och
snapshot. Saknas Wi-Fi öppnas nätet `TrainMeet-XXXXXX` för provisionering.
Nät- och serveravbrott är normala tillstånd: boxen återansluter med backoff
och tillåter inga skrivande kommandon innan färsk serverdata har hämtats.

Ingen serveradress eller port matas in. ESP32 använder samma serverval som
ESP8266: en ensam upptäckt server före första tilldelningen, därefter sparat
server-ID oavsett IP-adress. Flera servrar är ett tydligt vänteläge, inte ett
slumpmässigt val. Håll `*` fem sekunder för Wi-Fi-inställningar utan radering.
Avsiktligt serverbyte görs med kryssrutan i portalen; se installationsguiden.

Renderare, navigation, kommandon och uppmärksamhetspolicy ligger i
`lib/tmbox_core/` och verifieras utan hårdvara genom native-tester och
guldfiler. Fysisk verifiering görs med
[`docs/BANKTEST.md`](../../docs/BANKTEST.md).

## Träffomfattning vid config- och trafikdagsbyte

När Server skickar `meet_generation` och `publication_id` kräver boxen att
tilldelning, stationsconfig och trafiksnapshot har samma värden och samma
station innan knappar kan skicka trafikkommandon. Kommandot tar med den
generation operatören såg, inte en nyhämtad generation vid sändning.

En ny generation eller station tömmer gamla val, skärmdata och väntande
kommandoidentifierare. Försenade svar kan därför inte återställa ett gammalt
val. Blandade eller felaktiga MQTT-data spärrar inmatning medan boxen begär
färska data från Server. Äldre Server utan dessa fält stöds fortfarande,
men gamla och nya protokollfält blandas aldrig under samma MQTT-anslutning.

Detta ändrar inte stationstilldelningens ägare: administratören på Server
bestämmer stationen. Boxen får ingen Cloud-koppling eller egen trafiklogik.
Hosttester verifierar tillståndsmaskinen och alla sex ankomstordningar för
MQTT-data. Funktion på fysisk hårdvara behöver också provköras före release.
