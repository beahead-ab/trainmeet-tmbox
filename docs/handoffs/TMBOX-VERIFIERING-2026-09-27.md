# TMBox: verifiering av serveröverlämningen

Datum: 2026-09-27. Gäller de fysiska ESP8266/ESP32-enheternas gemensamma
`server-16x2`-adapter, inte en ombyggnad av Cloud eller Server.

## Underlag och avgränsning

- Ursprunglig verifiering: firmwarekällkod `10852ae`, VERSION `0.7.0`.
- Inför publicering: senaste `main`, `1f50aa8`, VERSION `0.7.1`.
  Den gemensamma terminaladaptern är oförändrad mellan dessa revisioner.
- Överlämning: Server-repots `docs/handoffs/TMBOX-FIRMWARE-2026-09-27.md`.
- Serverns kontraktsprov kördes mot den lokala arbetskopian
  `trainmeet-server-connection-simplify`, bas `5c716ea` **med pågående lokala
  ändringar**. Resultatet gäller inte automatiskt driftsatt Server.
- Bennys installerade firmwareversion har **inte** kunnat avläsas.

## Genomfört

Den faktiska `firmware/common/server_terminal.h` kompileras nu i ett direkt
beteendeprov. ArduinoJson 7.4.2 är samma låsta JSON-beroende som i firmwaren.
Testdubblar ersätter bara tid, Arduino-String samt MQTT- och LCD-I/O.

18 scenarier provar bland annat:

- Siffror förblir lokala; `#` skickar hela tågnumret med bildtoken och kontext.
- `B` suddar och `*` tömmer lokalt. `A` skickar funktionen utan ofärdigt nummer.
- Ändrad `view_revision` bevarar siffror vid samma `entry.context` och använder
  den nya bildtoken vid bekräftelse; äldre revisioner rullar inte tillbaka bilden.
- Ny station, träff eller återställning tömmer gammal inmatning.
- Accepterade/dubblerade kvittenser, nekade kommandon, dubbeltrycksspärr,
  fel boot-ID och retained-meddelanden.
- Transportfel/timeout och återanslutning utan återspelat kommando.
- Närvarokvitto är inte ny stationstilldelning.
- Serverns råa LCD-cellkoder och upp till åtta glyfer renderas; språkbildens
  återanvända glyfslot uppdateras; sifferinmatning lämnar klockcellerna orörda.
- En större fysisk LCD får tomma celler utanför den gemensamma 16×2-ytan.

CI har fått ett obligatoriskt adapterprov efter firmwarebygget när JSON-
beroendet finns tillgängligt. GitHub-resultatet redovisas i ändringens
PR-kontroller. Den fysiska bänktestguiden är uppdaterad till det
aktuella kontraktet; gamla uppgifter om `A` som godkänn och passiv `#` är borttagna.

## Lokala provresultat före publicering

| Prov | Resultat |
|---|---|
| Firmwareprojektets Python-svit | 84 tester godkända, inget överhoppat |
| Nytt direktprov av gemensam adapter | 18 scenarier godkända, ingår som ett av de 84 testerna |
| Äldre C++-kompatibilitetsprov | 584 kontroller godkända; samtliga golden-jämförelser oförändrade |
| Serverns terminal-/LCD-kontrakt | 93 tester godkända |
| Versionskontroll och diffkontroll | Godkända |
| Lokal korskompilering för ESP-korten | Inte körd; GitHubs byggkontroll kör den separat |
| Fysisk ESP8266/ESP32, LCD, Wi-Fi och minne | Inte genomförda |

Serverproven kördes utan att ändra Server-källkod eller ansluta till skarp
trafik. Urval: `test_terminal16_runtime`, `test_terminal16_glyphs`,
`test_terminal16_active`, `test_terminal16_pilot.Terminal16Tests`.

## Det som återstår på riktiga boxar

Ingen USB-ansluten TMBox syntes på datorn. Den tidigare angivna lokala
serveradressen kunde inte nås vid kontrollen. Det säger inte att Bennys box
saknas eller kör en viss version, bara att den inte kunde verifieras här.

1. Gör respektive box tillgänglig och läs av ID, faktisk firmwareversion,
   kort-/kopplingsprofil och om möjligt installerat pakets källrevision.
2. Kör de [sju fysiska acceptansproven](../BANKTEST.md) på båda kortfamiljerna.
3. Dokumentera LCD/tecken, knappar, återanslutning, hela trafikförloppet och
   minnes-/watchdogbeteendet med faktiskt utfall, inte antaganden från webbprov.

## Slutsats

Ingen ny placerings-, destinations- eller trafiklogik behövs i firmwaren för
vänster/höger-ändringen. Den granskade gemensamma adaptern har stödet för
serverstyrd presentation. En äldre installerad firmware kan ändå behöva
uppgraderas; en configuppdatering kan inte tillföra saknad protokollkod.

Detta arbete ändrar tester, CI och dokumentation, **inte firmwarekoden eller
versionsnumret**. Inga fysiska enheter har flashats. Publicering av dessa
kontroller och dokument till `main` innebär inte automatisk uppdatering av
redan installerade boxar och ersätter inte hårdvaruproven.
