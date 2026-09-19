# TrainMeet TMBox

Detta repo innehåller programvaran för den fysiska TrainMeet TMBoxen: ESP32-S3,
20×4 LCD och 4×4-tangentbord. Boxen är en tunn och självläkande klient till
[TrainMeet Server](https://github.com/beahead-ab/trainmeet-server), och talar
protokoll v2 (`tmbox/v2/...`).

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

Servern hittas automatiskt med mDNS/Bonjour. ESP8266 har inga manuella serverfält eller anslutningskoder; administratören tilldelar station på servern. ESP32:s befintliga installationsflöde är oförändrat.

## Bygg och ladda firmware

**Färdiga källkodspaket:** öppna
[Nedladdningar / Releases](https://github.com/beahead-ab/trainmeet-tmbox/releases)
och välj ett **Arduino IDE-** eller **PlatformIO-paket** under Assets.
[Välj rätt paket och följ steg-för-steg-guiden](docs/FIRMWARE-DOWNLOADS.md).
Arduino-paketet innehåller alla egna stödfiler och ingen PlatformIO-startfil
som kan ge dubbelkompilering. Välj inte ”Source code (zip)” för detta flöde.

[PlatformIO](https://platformio.org/) är den rekommenderade vägen:

```sh
cd firmware/esp32
pio run -e esp32-s3
pio run -e esp32-s3 -t upload
```

Det finns tre hårdvaruprofiler:

- **`esp32-s3` är TMBox v2** och byggmålet som gäller. ESP32-S3-DevKitC-1-N8R2,
  20×4-display på `0x27` bakom en nivåomvandlare, passiv 4×4-matris direkt på
  GPIO, summer och statuslysdiod. Fullständig specifikation i
  [docs/TMBOX-V2-HARDWARE.md](docs/TMBOX-V2-HARDWARE.md).
- `esp32-benny` och `esp32-classic-safe` beskriver klassisk ESP32 och behålls
  för den som ska få igång ett kort som råkar finnas. De är inte produkten.

> **TMBox v1 Legacy.** Den tidigare generationens boxar är ESP8266
> (ESP-12F) nodeMCU V3 med knappsatsen på ett PCF8574 över I2C. De behåller
> sin befintliga firmware; ESP32-S3:s v2-kod portas inte till dem.
> Hårdvaran är dokumenterad i
> [docs/TMBOX-V1-LEGACY.md](docs/TMBOX-V1-LEGACY.md).

Arduino IDE använder det särskilda [Arduino-paketet](docs/FIRMWARE-DOWNLOADS.md),
inte en lös `.ino` ur repot. Komplett koppling av display, tangentbord, ström
och nivåanpassning finns i [WIRING.md](firmware/esp32/WIRING.md).

## Valfri NodeMCU / ESP8266-variant (MQTT v1)

För ett separat NodeMCU-kort finns nu en **egen v1-klient** med PCF8574-knappsats
på I²C och 16×2 LCD. Den använder TrainMeet Servers äldre, driftsatta
`tambox/v1`-protokoll, inte v2:s lokala navigering eller v2-simulator.
Befintliga boxar med `mqttTamBox` uppdateras inte automatiskt.

Se [NodeMCU-guiden](firmware/esp8266/README.md) för koppling, Arduino IDE,
PlatformIO och ett separat hårdvarutest. Ingen fysisk box är verifierad ännu.

```sh
cd firmware/esp8266
pio run -e nodemcu-hardware-check
pio run -e nodemcu-i2c
```

D2/GPIO4 är SDA, D1/GPIO5 är SCL. Standardadresserna är `0x20` för knappsatsen
och `0x27` för LCD:n. Kontrollera matrisordning och nivåomvandling innan laddning.
Detta ändrar inte v2-profilen eller dess protokoll.

## Status

Firmwaren pratar protokoll v2 på riktigt: stabil enhetsidentitet,
mDNS-upptäckt, stationstilldelning och RAM-cachad config/snapshot.

**Kommandosidan är komplett.** Kärnan i
[`firmware/esp32/lib/tmbox_core/`](firmware/esp32/lib/tmbox_core/) bär hela
den lokala logiken, testad i CI utan hårdvara:

| Del | Vad den gör |
|---|---|
| Navigation | 19 skärmar, bläddring i stationsöversikt och rörelsedetalj, §5 inmatningslås på 500 ms |
| Tåguppslag | fyra siffror knappas in, servern svarar med träffar, träffarna bläddras |
| Spårväljare | välj spår ur stationens katalog; servern avgör om det är ledigt |
| Anslutningsväljare | klareringsbegäran namnger sin sträcka — boxen gissar aldrig |
| Klarering | inkorg, godkänn på `A`, neka på `B`, aldrig på `#` |
| Linjen ledig | inkorg och kvittering |
| Uppmärksamhet | vad som förtjänar en signal, och framför allt vad som inte gör det |

Tre guldfiler binder varje annan implementation till den här:
`golden_frames.txt` (60 rutor i alla fyra geometrier), `golden_traces.txt`
(12 tangentsekvenser) och `golden_attention.txt` (tre händelseförlopp).
Simulatorn under server.trainmeet.app speglar alla tre, och serverns testsvit
faller om de skiljer sig.

### Vad som återstår

Referenskonstruktionen är fastställd och firmwaren är testad utan hårdvara.
Nästa steg är att bygga den första v2-prototypen och verifiera inkoppling,
display, knappsats, summer, Wi-Fi, antennläge och kapsling i bänktest. Å, Ä och
Ö translittereras tills de egendefinierade displaytecknen har verifierats.

Bänktestlistan för det som kräver en människa och fysisk utrustning finns i
[docs/BANKTEST.md](docs/BANKTEST.md). Se [docs/tmbox.md](docs/tmbox.md) för
fullständig definition of done.

## Versionsnummer

`VERSION` i rotens enda auktoritativa fil, och den sätts automatiskt vid varje
merge till main. `FIRMWARE_VERSION` i skissen härleds ur den, så numret en box
rapporterar i sitt `hello` och numret i repot är samma sak — det var två
oberoende påståenden förut, vilket är precis den sorts glidning som gör att
man inte kan avgöra vilken firmware som faktiskt ligger i lådan.

Firmwaren står kvar under 1.0 med flit. Ingen version av den har körts på
riktig hårdvara. Den dagen en box gör det och fungerar är 1.0.0 rimligt; att
kalla den 1.0 innan dess vore att påstå något vi inte vet.

## Ändringar ska synas i simulatorn

Varje funktionell ändring i TMBox ska slå igenom i simulatorn på
TrainMeet Server, under **TMBox v2**. Det är där funktionerna testas: hela
kommandosidan går att köra där, i vilken som helst av de fyra geometrierna,
utan en enda box.

Det är inte en hederssak utan mekaniskt tvingat. Skärmarna och
tillståndsmaskinen bor i [`firmware/esp32/lib/tmbox_core/`](firmware/esp32/lib/tmbox_core/)
och publicerar tre filer:

| Fil | Vad den håller fast |
|---|---|
| `golden_frames.txt` | varje skärm, tecken för tecken, i 16×2, 20×2, 16×4 och 20×4 |
| `golden_traces.txt` | vad varje tangentsekvens gör: skärmbyten och kommandon |
| `golden_attention.txt` | vad som förtjänar en signal — och framför allt vad som inte gör det |

Simulatorns `tmbox-render.js`, `tmbox-nav.js` och `tmbox-attention.js` i
trainmeet-server måste reproducera alla tre exakt. Gör de inte det faller
serverns testsvit.

**Arbetsgången när en skärm eller en tangent ändras:**

1. Ändra i `lib/tmbox_core/` och kör `make -C firmware/esp32/test_native test`
2. `make -C firmware/esp32/test_native golden` skriver om de gyllene filerna
3. Kopiera de gyllene filerna till `tests/` i trainmeet-server
4. Spegla ändringen i `tmbox-render.js`, `tmbox-nav.js` eller `tmbox-attention.js`
5. Kör serverns svit — den säger vilken ruta, spår eller signal som flyttade sig

Ordningen är inte godtycklig: firmwaren är originalet, simulatorn speglar.

## Dokumentation

Den fullständiga produktbeskrivningen — namngivning, arkitektur, protokoll,
skärmflöden, tester och definition of done — finns i [docs/tmbox.md](docs/tmbox.md).
[docs/architecture.md](docs/architecture.md) beskriver det äldre, fortfarande
driftsatta MQTT v1-protokollet. En samlad dokumentationsöversikt finns i
[docs/README.md](docs/README.md).
