# Bänktest: serverstyrd TMBox på ESP8266 och ESP32

Aktuellt operatörskontrakt: `server-16x2`, från firmware 0.7.0 och Server
1.10.0. Servern äger trafikbeslut, presentation, språk och tangentbetydelser.
Testa med en server som också innehåller ändringen för lokal vänster/höger-
placering från serveröverlämningen 2026-09-27. Versionsgränsen 1.10.0 ensam
bevisar inte att just den ändringen finns installerad.

Den här guiden ersätter det äldre operatörsprovet för den lokala ESP32-kärnan.
Dess `golden_frames.txt` är historiska regressioner, inte facit för dagens
serverstyrda knappflöde. `#` är primär bekräftelse; A–D är aldrig destinationer.

## Förberedelser och identifiering

Kör på en testträff, utan fysisk trafik. Ingen flashning eller radering av
inställningar ingår automatiskt i detta prov. Kontrollera först vad som är
installerat; uppgradera en äldre box som ett separat, uttryckligen beslutat steg.

Notera för varje prov:

| Uppgift | ESP8266 | ESP32 |
|---|---|---|
| Datum och testare | Ej provat | Ej provat |
| Boxens permanenta ID/kod | Ej avläst | Ej avläst |
| Kort och kopplingsprofil | Ej avläst | Ej avläst |
| Fysisk LCD: storlek och I²C-adress | Ej avläst | Ej avläst |
| Rapporterad firmwareversion | Ej avläst | Ej avläst |
| Installerat paket/källrevision om känt | Ej verifierat | Ej verifierat |
| Serverversion och källrevision | Ej verifierat | Ej verifierat |
| Träff och tilldelad station | Ej tilldelat | Ej tilldelat |

Versionen kan läsas i serverns enhetsuppgifter (`firmware_version` från boxens
`hello`). ESP8266:s befintliga webbpanel rapporterar också versionen. En
nedladdad ZIP eller ett lokalt repo bevisar **inte** vad enheten kör. Enbart
versionsnumret skiljer inte heller två lokalt byggda varianter med samma
version; spara paketets `PACKAGE.json`/källrevision när den är känd.

Använd rätt kopplingsguide för kortet:

- [NodeMCU ESP8266](../firmware/esp8266/README.md).
- [ESP32-profiler](../firmware/esp32/README.md) och
  [ESP32-S3 referenshårdvara](TMBOX-V2-HARDWARE.md).

20×4 är en möjlig fysisk display, inte en ny trafikprofil. Den aktiva ytan
ska tills vidare vara 16×2 och övriga celler tomma. Detta är inte ett påstående
att alla ESP8266-kopplingar redan stöder en 20×4-display.

## Sju acceptansprov — kör på båda kortfamiljerna

### 1. Anslutning, identitet och tilldelning

- [ ] Alla sexton tangenter ger rätt tecken utan dubbla utslag.
- [ ] Boxen ansluter till träffens Wi-Fi och upptäcker servern automatiskt.
- [ ] Operatören behöver inte ange server-IP/port eller välja station/roll.
- [ ] Före admin-tilldelning kan boxen inte påverka trafik.
- [ ] Admin tilldelar station; serverstyrd 16×2 visas, klockan till höger.
- [ ] Kall omstart behåller enhetsidentiteten och återhämtar färskt läge.
- [ ] Närvarokvitton ger inte nya återkommande stationstilldelningar.

### 2. Lokal inmatning under presentationsändring

- [ ] Skriv `93` utan `#`; inga tågkommandon publiceras.
- [ ] Admin ändrar vänster/höger för stationen; siffrorna finns kvar.
- [ ] Kontrollera samma `entry.context`, högre `view_revision` och ny
      `view_token`, även om trafikens `revision` är oförändrad.
- [ ] `B` suddar ett tecken, `*` tömmer inmatningen, utan trafikkommando.
- [ ] `#` skickar det färdiga numret en gång, bundet till den nya bilden.

### 3. Aktiv begäran och placering

- [ ] Välj tåg och begär med den åtgärd som servern visar.
- [ ] Admin ändrar sida under begäran: stationskod, nummer, `?`/pil och
      justering ändras utan att begäran eller destinationen byts/tappas.
- [ ] Upprepade snabba bekräftelser utför inte nästa trafiksteg av misstag.

### 4. Hela trafikförloppet

- [ ] Begär → ge klart → faktisk avgång → ankomst fungerar mellan boxarna.
- [ ] Klart registreras inte som faktisk avgång.
- [ ] Återtag fungerar före avgång men inte efter att tåget lämnat.
- [ ] Neka och skicka utan krav på klart fungerar när servern erbjuder det.
- [ ] Ankomst fungerar både på planerat och valt avvikande spår.
- [ ] Avsändaren får ett kort mottagningsbesked; därefter försvinner tåget
      automatiskt från den avslutade rörelsen utan extra kvittering.
- [ ] Ingen aktiv rörelse ger tom översta rad. Klockan syns enligt serverbilden.

### 5. Förfrågningskö och minimal inmatning

- [ ] En begäran öppnas när boxen är ledig, utan att mottagaren slår tågnumret.
- [ ] Två väntande förfrågningar ger en tydlig köindikering.
- [ ] `A` hittar tillbaka och `C`/`D` bläddrar enligt serverns visade funktioner.
- [ ] Ny begäran förstör inte pågående sifferinmatning.
- [ ] Från inmatning lämnar `A` den lokala inmatningen för kön när servern
      erbjuder snabbvägen; den skickar inte det ofärdiga tågnumret.

### 6. Språk, LCD och CGRAM

- [ ] Prova svenska ÅÄÖ/åäö, danska/norska ÆØ/æø, tyska Ü/ü/ß och båda pilarna
      över flera serverbilder; inte alla tecken behöver finnas samtidigt.
- [ ] Operatörens språkbyte och admin-sänt språk fungerar.
- [ ] LCD visar serverns cellkoder/pixelmönster, inte UTF-8 som enstaka ASCII-byte.
- [ ] Högst åtta samtidiga CGRAM-glyfer; återanvända slotar visar rätt nya tecken.
- [ ] Språk-/sidbyte lämnar inga gamla synliga glyfer eller celler kvar.
- [ ] Kontrast, bakgrundsbelysning och läsbarhet provas i mötesmiljö.

### 7. Avbrott, gammal kontext och återanslutning

- [ ] Bryt nätet under inmatning; trafikknappar spärras och gamla siffror
      återanvänds inte efter återanslutning.
- [ ] Bryt nätet vid `#`, inklusive när servern kan ha hunnit godkänna men
      kvittot inte nått boxen. Kontrollera serverns trafikhistorik: högst en åtgärd.
- [ ] Återanslutning ger färskt läge utan återspelade trafikkommandon.
- [ ] Ny station, ny träff och återställd isolerad provbänk ger ny kontext;
      gamla inmatningar/kommandon kan inte användas där.
- [ ] Kör upprepade språkbyten/återanslutningar och dokumentera minne,
      omstarter och eventuella watchdog-fel på varje faktisk kortvariant.

## Automatiska prov och deras gräns

`tests/server_terminal_test.cpp` kompilerar den riktiga gemensamma adaptern
mot samma låsta ArduinoJson 7.4.2 som firmwaren. Endast Arduino-primitiver,
MQTT-I/O, tid och LCD-I/O ersätts av testdubblar. 18 scenarier täcker lokal
inmatning, bildrevisioner, kontextbyten, kvittenser, föråldrade bilder,
återanslutning, transportfel och råa LCD-cellkoder.

```sh
# Efter ett vanligt firmwarebygge finns ArduinoJson i PlatformIO-cachen.
TMBOX_REQUIRE_TERMINAL_TESTS=1 python3 -m unittest discover -s tests -p test_server_terminal.py
python3 -m unittest discover -s tests
make -C firmware/esp32/test_native test
```

`ARDUINOJSON_INCLUDE` kan ange en redan installerad `ArduinoJson/src`-katalog.
Utan beroendet hoppar ett vanligt källkodsprov över terminalprovet med tydlig
orsak. CI kör det efter firmwarebygget med krav på att det verkligen genomförs.

Dessa prov verifierar **inte** faktisk Wi-Fi/MQTT-transport, LCD-elektronik,
tecknens läsbarhet, tangentstuds, minnesutrymme eller hela trafikmotorn på Server.
Serverns egna kontraktsprov och de sju fysiska proven behövs också.

## Protokoll och godkännande

Anteckna utfall, avvikelser och bevis per prov och box i testprotokollet.
Ej utfört betyder **ej verifierat**, inte godkänt. För hårdvarans separata
säkerhets-/mekanikprov gäller fortfarande respektive kopplingsguide: kontrollera
spänningsnivåer, nivåomvandlare, kablar och infästningar innan ström ansluts.
Ett godkänt bygge räcker inte för att godkänna en fysisk box eller deklarera 1.0.

## Separata hårdvaruprov för ESP32-S3-referenslådan

Följande elektronik- och mekanikpunkter behålls från den tidigare checklistan.
De avser referenslådans komponenter, inte en separat operatörslogik.

### Elektronik och display

- [ ] Kontrollera kortvariant och alla anslutningar mot pinntabellen innan ström ansluts.
- [ ] 5 V når aldrig ESP32-S3:s GPIO; nivåomvandlaren sitter på I2C-bussen.
- [ ] Displayen svarar på `0x27`; separat hårdvarudiagnostik provar alla 20×4 celler.
- [ ] Kontrast och bakgrundsbelysning fungerar i tänkt mötesmiljö.
- [ ] Den aktiva ytan följer Serverns 16×2-bild; övriga celler är tomma.
- [ ] Setup-portalen kan öppnas utan att sparat Wi-Fi raderas av misstag.
- [ ] Antennens räckvidd provas med vald låda och frontpanel.

### Ljud, ljus och mekanik

- [ ] Summer och statuslysdiod provas separat där hårdvara/diagnostik finns.
      Detta bevisar inte ljud- eller ljusstöd i `server-16x2`-kontraktet.
- [ ] Om ljudstöd införs ska gamla händelser inte spelas efter återanslutning.
- [ ] Kort, display och knappsats sitter med mekaniska infästningar utan lim.
- [ ] USB-kabelns kraft tas upp av paneluttaget och når inte utvecklingskortet.
- [ ] Boxen klarar upprepade kabelanslutningar, omstarter och normal hantering.
