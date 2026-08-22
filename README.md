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

### Vad som återstår — och allt hänger på hårdvaran

Ingenting av det som är kvar går att avgöra härifrån:

- **Bennys svar på hårdvarufrågorna.** Kortmodell, knappsatsens GPIO-karta,
  I2C-adress, kablage och elektriska nivåer. Frågorna är formulerade och
  märkta med vilka som är blockerande.
- **Summer och GPIO.** `AttentionController` avgör redan *vad* som förtjänar
  uppmärksamhet. `TMBOX_BUZZER_PIN` är osatt tills fråga 5.2 är besvarad;
  utan den loggar boxen sitt beslut på serieporten och kör vidare. Frekvens-
  och längdtabellen finns och matchar simulatorns toner.
- **Fysisk verifiering.** Ingen firmware har körts på en riktig låda. Fyra
  kända Wi-Fi- och anslutningshärdningar väntar på att kunna provas mot
  hårdvara i stället för mot en fixtur.
- **Å, Ä, Ö.** Kärnan translittererar (`SPAR`, `BEGAR`) tills displayens
  teckenuppsättning är bekräftad.

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
driftsatta MQTT v1-protokollet. Beslutshistoriken bakom `docs/tmbox.md` finns i
[docs/underlag/](docs/underlag/).

[**tmbox-flodesbild.html**](docs/underlag/tmbox-flodesbild.html) är en
interaktiv referens med exakta skärmbilder för varje steg i uppstart,
tåguppslag, spårval, avgång, ankomst, request/reply och linjen-ledig, i tre
kolumner (lokal TMBox / Trainmeet / motstationens TMBox). GitHub visar filen
som källkod i webbläsaren — ladda ner den och öppna lokalt för att köra den
interaktivt.

