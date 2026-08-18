# Monsterprompt till Claude: implementera TMBox för Trainmeet

Du ska utveckla **TMBox-funktionaliteten end-to-end** i det repository och den kodbas du har fått tillgång till. Läs först igenom hela repositoryt, befintlig dokumentation, datamodell, API, tester, byggsystem och eventuell befintlig hårdvarukod. Anpassa lösningen till den befintliga stacken i stället för att bygga en parallell prototyp.

Den bifogade filen `tmbox-flodesbild.html` är den aktuella interaktions- och flödesreferensen. Använd den för att förstå skärmbilder, statusar och kommandon. Den här specifikationen är samtidigt normerande för arkitektur, ansvarsfördelning, säkerhet och tillståndsregler.

Arbeta konkret: inspektera, planera, implementera, migrera, testa och dokumentera. Stanna inte vid pseudokod eller en designskiss. Om repositoryt saknar någon nödvändig del ska du skapa den på ett sätt som passar den befintliga strukturen. Gör rimliga antaganden där information saknas, dokumentera antagandena och fråga endast om ett val verkligen blockerar en säker implementation.

## 1. Namnet är TMBox

Det kan finnas äldre eller felaktiga namn som `tMbox`, `T-box`, `TBox`, `Tabbox`, `TaBBox`, `Tambox` eller liknande i kod, GitHub, dokumentation och konfiguration. Standardisera namngivningen enligt följande:

- Produktnamn i löptext och grafiska gränssnitt: **TMBox**.
- Text på teckendisplay när versaler används: **TMBOX**.
- Teknisk slug, repositorynamn, mappar och paket där gemener krävs: **tmbox**.
- Enhets-ID: `TMBOX-<stabilt-hårdvaru-id>`, exempelvis `TMBOX-7A42F1`.
- Trainmeet behåller sitt eget namn och ska inte döpas om.

Gör en repositoryomfattande inventering av gamla namn och skapa en säker migrering. Uppdatera där det är relevant:

- README, docs, skärmar, loggar och felmeddelanden.
- Repositorybeskrivning och föreslaget GitHub-repositorynamn `tmbox`.
- Importer, paketnamn, namespaces, tjänstenamn och byggartefakter.
- CI/CD, releaseflöden, OTA-metadata, Docker/Compose om det finns, installationsskript och sökvägar.
- Konfigurationsnycklar, miljövariabler och lagringsnycklar.
- Dokumenterade klon-URL:er, badges, länkar och git-remotes.

Bryt inte installerade enheter eller sparad data enbart för namnbytets skull. Om äldre nycklar eller identifierare redan används i produktion ska implementationen kunna läsa dem under en övergångsperiod, migrera dem till det nya namnet och skriva det nya formatet. Lägg inte in dubbla permanenta domänmodeller bara för kompatibilitet.

Om du har GitHub-behörighet och uppdraget uttryckligen omfattar en extern namnändring kan repositoryt döpas om till `tmbox`. Annars ska du leverera en exakt checklista för den externa GitHub-ändringen och uppdatera all kod som säkert kan uppdateras lokalt.

## 2. Produktens kärna

TMBox är en fysisk terminal för tågrörelser och klarering mellan trafikplatser i Trainmeet. Den primära användaren är en TKL vid en station. Enheten ska vara snabb, tydlig, robust, svår att göra fel med och begriplig även för ovana användare.

TMBox ska stödja:

- Uppstart, Wi-Fi-installation, serverupptäckt, enhetsidentifiering och stationstilldelning.
- Lokal visning av stationens aktiva tåg och rörelser.
- Uppslag av valfritt tågnummer och serverstyrda tillåtna åtgärder.
- Avgångar som börjar på den lokala stationen.
- Ankomster till den lokala stationen.
- Request/reply-klarering med `KLART` eller `EJ KLART`.
- Ensidigt meddelande `LINJEN ÄR LEDIG` när sträckans arbetssätt använder det.
- Stationsspår med både numeriska och alfanumeriska namn, exempelvis `1A`, `1B`, `2A`, `2B` och `7`.
- Spårbyte före och under ett flöde, med korrekta följdverkningar för redan skickade eller godkända klareringar.
- Offlinevisning av senaste verifierade status utan att skapa falska operativa beslut.
- 16 eller 20 tecken per rad samt 2 eller 4 rader.

TMBox är inte ett ställverk och ska i denna version inte styra växlar eller signaler. LocoNet, JMRI, en generell digital ställverkspanel, förseningstjänster och bred trafikledningsfunktionalitet ligger utanför kärnomfattningen. Arkitekturen får gärna vara utbyggbar, men dessa sidoprojekt får inte komplicera första fungerande TMBox-versionen.

## 3. Övergripande arkitektur

Utgå från följande ansvarsfördelning:

### TMBox-enheten ansvarar för

- Avläsning och debounce av knappsatsen.
- Lokal redigering av tågnummer och lokala menypages.
- Renderering på vald teckendisplay.
- Lokal cache av stationens konfiguration och senaste verifierade snapshot.
- Ljud, lampor och uppmärksamhetslägen.
- Wi-Fi-anslutning, återanslutning, captive portal och serverupptäckt.
- En lokal tillståndsmaskin som återspeglar serverns verifierade status.
- Att skicka kompletta idempotenta kommandon, inte en nätresa per tangenttryckning.
- Prenumeration på händelser för tilldelad station och egen enhet.
- Watchdog, säker omstart och signerad/försiktigt genomförd OTA-uppdatering om kodbasen stöder det.

### Trainmeet-servern ansvarar för

- Sanningen om tåg, trafikplatser, riktning, tidtabell, spår och tillstånd.
- Enhetsregister, stationstilldelning, behörighet och konfigurationsversioner.
- Validering av alla operativa kommandon.
- `allowed_actions` för varje aktuellt tillstånd.
- Klareringslogik, dubblettskydd, idempotens och revisioner.
- Beständig lagring av spårval, statusar, begäranden, svar och händelser.
- Distribution till berörda TMBox-enheter.
- Återsynkronisering efter nätavbrott eller omstart.
- Förslag på spår baserat på tidtabell, trafikmönster och historik.

En tangenttryckning som bara ändrar en lokal markör, bläddrar en sida eller skriver en siffra ska aldrig skickas till servern. TMBox skickar först ett komplett kommando när användaren bekräftar en verklig åtgärd.

Den avsedda fysiska plattformen är en Wi-Fi-kapabel mikrokontroller, i första hand **ESP32-S3 med Arduino-ramverket**, inte en klassisk Arduino Uno och inte en Linux-dator som krav. Anpassa till befintlig hårdvara i repositoryt om den redan är beslutad, men håll implementationen tillräckligt lätt för en mikrokontroller. En Raspberry Pi ska inte behövas för normal funktion.

## 4. Anslutning, installation och identitet

### 4.1 Stabil enhetsidentitet

Vid varje uppstart ska det stabila hårdvaru-ID:t visas tidigt:

```text
TMBOX-7A42F1
STARTAR...
```

ID:t ska vara stabilt över omstarter och programuppdateringar. Det får inte baseras på en flyktig IP-adress. Använd en lämplig hårdvaruidentitet eller en engångsgenererad beständig identitet, beroende på plattform och säkerhetsmodell.

### 4.2 Wi-Fi

TMBox provar lokalt sparade Wi-Fi-profiler i prioriterad ordning. Stöd minst primärt nät och reservnät om den befintliga plattformen medger det.

Om inget nät fungerar ska TMBox erbjuda en lokal installationsportal:

```text
WIFI HITTAS EJ
#=INSTALLATION
```

Efter `#` startas ett lokalt access point, exempelvis `TMBOX-7A42F1`, med captive portal på en lokal adress. En telefon ska kunna ansluta, välja Wi-Fi, ange lösenord och vid behov ange Trainmeet-adress. SSH eller ett textgränssnitt via terminal ska inte krävas för en vanlig användare.

Wi-Fi-lösenord ska lagras på ett plattformsriktigt säkert sätt och aldrig skickas till Trainmeet.

### 4.3 Trainmeet-servern har inte en permanent URL

Serverns URL eller IP kan ändras. Implementera serverupptäckt i denna ordning:

1. Senast verifierade endpoint.
2. Lokal mDNS-annons, exempelvis `_trainmeet._tcp`.
3. Manuellt konfigurerad URL via installationsportalen.

Separera alltid serverns permanenta `server_id` från dess aktuella URL och IP. När en server hittas ska dess identitet verifieras. Vid första anslutningen kan en kontrollerad trust-on-first-use-process användas om det passar befintlig säkerhetsmodell; därefter ska en oväntad identitetsändring ge ett tydligt fel och inte accepteras tyst.

### 4.4 Tilldelning till station

Första gången en enhet ansluter skickar den ett komplett `device.hello` med minst:

- `device_id`
- hårdvaruversion
- firmwareversion
- displaykapacitet
- konfigurationsversion
- protokollversion

Trainmeet-administrationen visar den nya enheten online. Administratören matchar ID:t på servern med ID:t på TMBox-skärmen och tilldelar station, exempelvis `cda`.

TMBox hämtar sedan sin konfiguration, sparar den lokalt och visar tilldelningen:

```text
CDA TILLDELAD
#=BEKRAFTA
```

Användaren ska bekräfta med `#` så att det finns tid att kontrollera på Trainmeet-servern att rätt enhet har dykt upp och fått rätt trafikplats. Därefter öppnas prenumerationen och normalvyn visas.

## 5. Stationskonfiguration och exempelstation

Konfigurationen ska vara datadriven. Hårdkoda inte Charlottendal i den generella implementationen, men använd följande konfiguration som referens och testfixture:

- Lokal station: Charlottendal, signatur `Cda`/`CDA`.
- Vänster: Lekby, signatur `Lek`/`LEK`, dubbelspår.
- Höger: Vagnsta, signatur `Vst`/`VST`, dubbelspår.
- Vänster tredje anslutning: Kungsfors, signatur `Kun`/`KUN`.
- Rad 1 vänster: Lekby linjespår 1.
- Rad 1 höger: Vagnsta linjespår 1.
- Rad 2 vänster: Lekby linjespår 2.
- Rad 2 höger: Vagnsta linjespår 2.
- Rad 3 vänster: Kungsfors.
- Rad 3 höger: anslutning finns inte.
- Rad 4: ingen anslutning i referenskonfigurationen.
- Giltiga stationsspår i det nya alfanumeriska testfallet: `1A`, `1B`, `2A`, `2B`.

En befintlig linje utan aktuellt tåg visas som `TOMT`. En anslutning som inte finns ska vara blank och får inte visas som `TOMT`.

Konfigurationen ska bland annat kunna innehålla:

- Stationens fullständiga namn och signatur.
- Tillåtna stationsspår som text-ID:n och displayetiketter.
- Vänster/höger-topologi och linjespår per displayrad.
- Klareringsläge per relation eller sträcka.
- Displaystorlek och teckenuppsättning.
- Aktiverade funktioner, knappmappning, ljud och lampbeteende.
- Konfigurationsversion.

## 6. Displayregler

Stöd dessa fyra kombinationer:

- 16×2
- 20×2
- 16×4
- 20×4

Logiken ska vara densamma oavsett display. Skillnaden är hur mycket kontext som ryms samtidigt.

### 6.1 Exakt teckenbudget

- Rendera aldrig fler tecken än den valda radbredden.
- Skapa testfixtures som verifierar varje definierad skärmbild mot 16 respektive 20 tecken.
- Använd fullständigt stationsnamn när hela raden ryms. Använd annars den konfigurerade signaturen.
- Trunkera inte ett viktigt ord så att innebörden blir oklar. Välj i stället en godkänd kortetikett.
- LCD-text kan använda versaler och ASCII-translitterering, exempelvis `SPAR`, `BEGAR` och `FORARE`, om den fysiska displayen saknar stabilt stöd för ÅÄÖ. Gör teckentabellen konfigurerbar om hårdvaran stöder svenska tecken.

### 6.2 Inga löpande kommandon

På tvåradersdisplayen ska kommandon inte rulla tecken för tecken. Det blir svårt att läsa och riskerar att knapparnas synliga betydelse flyttar sig medan TKL trycker.

Använd fasta kommandosidor. Exempel:

```text
421 [1A] >VST
A=UPP B=ANDR C>
```

Efter `C`:

```text
421 [1A] >VST
D=INFO *=AVBR C<
```

`C` byter lokal kommandosida utan nättrafik. På en fyraradare kan motsvarande information visas samtidigt.

### 6.3 Trafiköversikt och riktningsgrammatik

Vänster inkommande:

```text
421>[1A]
```

Höger inkommande:

```text
[2A]<428
```

Vänster avgående:

```text
421<[1A]
```

Höger avgående:

```text
[2A]>428
```

På en 16-teckensrad kan båda sidor visas exakt:

```text
421>[1A][2A]<428
```

På 20 tecken används mellanrum för tydligare separation. Vänstersidans rörelse vänsterjusteras och högersidans rörelse högerjusteras.

På tvåradersdisplayen visar sida 1 dubbelspårens rader 1–2 och sida 2 Kungsfors/övriga rader. `C` går till nästa sida. `D` växlar mellan inkommande och utgående vy. Fyraradaren visar samtliga topologirader samtidigt.

## 7. Knappmodell

Knappsatsen består av `0–9`, `A–D`, `*` och `#`.

- `0–9`: tågnummer och annan numerisk lokal inmatning.
- `A`: primär positiv/framåtriktad operativ handling. Exempel: `UPPSTÄLLT`, `FÖRARE PÅ PLATS`, `BEGÄR`, `KLART`, `SKICKA`, `AVGÅTT`, `ANKOMMIT`.
- `B`: sekundär eller negativ operativ handling. Exempel: `EJ KLART`; i ett planerat avgångsläge kan `B=ÄNDRA` öppna spårväljaren.
- `C`: nästa tåg, nästa trafikrad eller nästa fast kommandosida.
- `D`: mer information eller tydligt definierad lokal vyväxling, exempelvis `IN/UT` i trafiköversikten.
- `*`: radera, tillbaka eller initiera avbrott.
- `#`: välj, OK, bekräfta data eller kvittera att information har visats.

Viktiga säkerhetsregler:

- `#` får aldrig lämna operativt `KLART`, `EJ KLART`, `AVGÅTT` eller `ANKOMMIT`.
- `A` och `B` ska användas för de operativa besluten.
- `*` får inte omedelbart radera en pågående serverbegäran. Visa först:

```text
AVBRYT BEGARAN?
#=JA *=NEJ
```

- `#` får däremot bekräfta en dataändring, exempelvis ett valt stationsspår, eftersom det inte är ett klareringsbeslut.
- Visa bara åtgärder som Trainmeet returnerat i `allowed_actions` för aktuell revision.

## 8. Tåguppslag

TKL ska kunna skriva ett valfritt tågnummer och trycka `#`:

```text
CDA
TAGNR: 421_
```

Inmatningen redigeras lokalt. Efter `#` skickas ett komplett `train.lookup`. Trainmeet svarar med minst:

- om tåget finns
- om det är relevant vid den lokala stationen
- tåguppdragets stabila ID och dagens `train_run_id`
- riktning och motstation
- aktuellt tillstånd
- tidtabellsspår, föreslaget spår, tilldelat spår och faktiskt spår
- klareringsläge
- `allowed_actions`
- serverrevision
- tidpunkt för snapshot

TMBox ska sedan visa endast giltiga actions. Om tåget inte hittas, finns vid en annan station eller bara finns i offlinecache får inga operativa actions visas.

## 9. Spårmodell, spårväljare och serverminne

### 9.1 Spår är textidentifierare

Modellera aldrig stationsspår som heltal. Använd en stabil textidentifierare och en separat displayetikett vid behov:

```json
{
  "id": "1A",
  "display_label": "1A",
  "station_id": "cda",
  "active": true,
  "sort_order": 10
}
```

Det ska gå att ha `1A`, `1B`, `2A`, `2B`, `7`, `S1`, `N` eller andra stationsspecifika beteckningar utan kodändring.

### 9.2 Spårväljaren

Använd inte A/B som bokstavsinmatning för spårsuffix. De knapparna har operativ betydelse. När TKL trycker `B=ÄNDRA` visar TMBox stationens serverstyrda lista, ett spår i taget på 16×2:

```text
SPAR 1A  1/4
C=NAST #=VALJ *<
```

`C` går runt i listan `1A → 1B → 2A → 2B → 1A`. `#` väljer. `*` lämnar utan ändring.

Innan serverkommandot skickas visas den kompletta ändringen:

```text
421 1A > 1B
#=SPARA *=AVBR
```

### 9.3 Separata spårvärden

Trainmeet ska lagra minst:

- `scheduled_track_id`: tidtabellens ursprungliga spår.
- `suggested_track_id`: serverns förslag för aktuell körning.
- `assigned_track_id`: det spår TKL valt för dagens körning.
- `actual_track_id`: där tåget faktiskt ställdes upp, avgick eller ankom.
- `origin_track_id` och `destination_track_id` när båda behövs i samma rörelse.

Skriv aldrig över tidtabellsspåret när TKL väljer ett annat spår.

### 9.4 Förslag nästa dag

Om samma trafikmönster ofta använder `1B` ska Trainmeet kunna föreslå `1B` nästa gång. Nyckla inte en sådan preferens enbart på det synliga tågnumret. Använd en stabil tjänste-/tåguppdragsidentitet eller en kombination av trafikmönster, station, riktning, relation och eventuellt tidtabellsperiod.

Förslagsordningen bör vara:

1. Giltigt explicit tilldelat spår för dagens körning.
2. Tidtabellens spår.
3. Relevant historiskt/prefererat spår om det är aktivt och tillåtet.
4. Stationsdefault om sådan finns.

Kontrollera alltid dagens spärrar, beläggning och regler. Historik är ett förslag, aldrig ett automatiskt operativt beslut. TKL bekräftar dagens verkliga spår med `A=UPP` eller väljer ett annat med `B=ÄNDRA`.

### 9.5 Spårbyte under olika tillstånd

- Före klareringsbegäran: validera och uppdatera dagens tilldelning.
- Under väntande begäran: spårbytet får inte vara tyst. Revidera motstationens ärende med ny revision eller makulera och skapa en ny idempotent begäran.
- Efter lämnat `KLART`: ett operativt relevant spårbyte ogiltigförklarar klareringen eller kräver explicit ny bekräftelse enligt serverns regler.
- Efter avgång: avgångsspåret är historik och låses. Mottagande station kan fortfarande ändra ankomstspåret.
- Vid konflikt eller ogiltigt spår: behåll tidigare spår, visa begriplig orsak och returnera aktuell serverrevision.

Använd optimistic concurrency, exempelvis `expected_revision`, så att två TKL-enheter eller en adminändring inte skriver över varandra i tysthet.

## 10. Komplett avgångsflöde: tåg 421 från Charlottendal till Vagnsta

Detta är referensflödet som ska fungera end-to-end.

### 10.1 TKL triggar avgången

Charlottendals TKL anger tåg 421:

```text
AVGANG CDA
TAGNR: 421_
```

Efter `#` returnerar Trainmeet att tåget börjar vid Charlottendal, ska mot Vagnsta och enligt tidtabellen föreslås på spår `1A`.

```text
421 [1A] >VST
A=UPP B=ANDR C>
```

### 10.2 Valfritt spårbyte

TKL kan trycka `B`, välja `1B` i den serverstyrda spårlistan och bekräfta med `#`. Trainmeet validerar och lagrar:

- `scheduled_track_id = 1A`
- `assigned_track_id = 1B`
- `actual_track_id = null`

Efter lyckat svar visas:

```text
421 [1B] >VST
A=UPP B=ANDR C>
```

### 10.3 TKL registrerar uppställt

När tåget verkligen står på spår 1B trycker TKL `A=UPP`. Servern lagrar `positioned` tillsammans med `actual_track_id = 1B` och returnerar nästa tillåtna handling.

```text
421 [1B] UPPST
A=FORARE D=MER
```

### 10.4 TKL registrerar lokförare på plats

Detta är en aktiv TKL-status. När TKL fått besked att lokföraren är på plats trycker TKL `A=FÖRARE`.

```text
421 [1B] UPPST
A=FORARE D=MER
```

Skicka ett komplett `train.crew_ready.set`. En framtida lokförarklient kan också producera samma domänhändelse, men TMBox-flödet får inte vara beroende av den integrationen.

### 10.5 Trainmeet härleder redo

`KLAR ATT BEGÄRA` ska vara härledd serverstatus, inte en tredje manuell dublettbekräftelse. När `positioned && crew_ready && rules_ok` är sant returnerar Trainmeet `request_clearance` i `allowed_actions`:

```text
421 [1B] REDO
A=BEGAR D=MER
```

### 10.6 Begär utfart

Efter `A` visar TMBox en tydlig bekräftelse:

```text
BEGAR UT 421?
A=SKICKA *=NEJ
```

Först efter det andra `A` skickas en komplett idempotent begäran med tåg, avsändare, mottagare, spår, `message_id` och förväntad revision.

Servern bekräftar registrering innan TMBox visar vänteläge:

```text
421 [1B] VANTAR
*=AVBR D=MER
```

### 10.7 Vagnsta svarar

Vagnstas TMBox väcks med ljud/lampa och visar:

```text
421 FRAN CDA
A=KLART B=EJ
```

`A` och `B` är kompletta operativa svar som valideras och lagras på servern.

### 10.8 Charlottendal får svar

Vid godkänt svar:

```text
421 [1B] KLART
#=OK D=MER
```

Meddelandet ligger kvar tills det kvitteras. `#` kvitterar bara visningen.

Vid `EJ KLART` ska tåget vara kvar uppställt. TKL kan visa mer information och senare göra en ny aktiv begäran när servern tillåter det. Ingen orsak behöver skrivas på TMBox om inte verksamhetsreglerna senare kräver det.

### 10.9 Faktisk avgång

Efter KLART och när tåget verkligen lämnat spår 1B:

```text
[1B]>421>VST
A=AVGATT *=TILLB
```

`A=AVGÅTT` skapar en separat domänhändelse från själva klareringen. Servern låser avgångsspåret och sätter tåget `en_route`.

### 10.10 På linjen och avslut

Tåget visas på linjen mot Vagnsta. När Vagnsta registrerar faktisk ankomst avslutas rörelsen och båda stationerna får korrekt snapshot.

## 11. Ankomstflöden till Charlottendal

Använd tåg 428 från Vagnsta till Charlottendal, föreslaget ankomstspår `2A`, som referens.

### 11.1 Normal request/reply-ankomst

1. Trainmeet aviserar tåg 428 från Vagnsta mot Charlottendal spår 2A.
2. Vagnsta skickar en komplett klareringsbegäran.
3. Charlottendals TMBox visar `A=KLART B=EJ`.
4. Efter `A=KLART` får Vagnsta godkänt svar.
5. Vagnsta registrerar avgång; tåget blir `en_route`.
6. Charlottendal visar rörelsen från höger och att tåget närmar sig spår 2A.
7. När tåget fysiskt står inne trycker Charlottendals TKL `A=ANKOMMIT`.
8. Trainmeet sätter tåget `at_station`, avslutar linjerörelsen och frigör relevant linjestatus.

Exempel på två rader:

```text
[2A]<428<VST
ANKOMMER 14:32
```

Vid faktisk ankomst:

```text
428 [2A] INNE?
A=ANKOM D=MER
```

`#` får inte registrera ankomst; `A` gör den operativa statusändringen.

### 11.2 Ensidigt linjen-ledig-läge

Om sträckan är konfigurerad för ensidigt besked får Charlottendal ett `LINJEN ÄR LEDIG`-meddelande i stället för en fråga. `#` kvitterar att meddelandet visats men skickar inget reply. Använd aldrig denna modell på en relation som kräver explicit request/reply.

### 11.3 Spårändring före ankomst

Charlottendals TKL kan ändra ankomstspår från `2A` till `2B` via samma serverstyrda spårväljare. Trainmeet validerar och distribuerar den nya ankomstinformationen. Om klareringens villkor påverkas ska servern kräva revision eller nytt beslut.

### 11.4 Upptaget spår

Om Trainmeet känner till att spår 2A är blockerat ska `A=KLART` inte erbjudas. Visa:

```text
SPAR 2A UPPTAG
B=EJ D=MER
```

### 11.5 Flera inkommande tåg

Servern levererar en ordnad kö, normalt äldsta/aktuellaste ärendet först med tydliga tids- och prioritetsregler. Fyraradaren kan visa flera tåg. Tvåradaren visar ett i taget med `C=NASTA` och position, exempelvis `1/2`. En avbruten lokal tågnummerinmatning ska kunna återställas efter att ett viktigt inkommande ärende hanterats.

## 12. Två klareringslägen

### 12.1 Request/reply

Tillstånd:

- `draft_local`
- `request_submitting`
- `waiting`
- `approved`
- `rejected`
- `cancelled`
- `expired`
- `invalidated_by_revision`

Krav:

- Servern skapar ett stabilt `clearance_id`.
- `message_id` gör retry idempotent.
- Mottagaren svarar endast med A/B.
- Samma verifierade resultat distribueras till båda stationerna.
- Ett aktivt svar eller avbrott använder förväntad revision.
- Ändrat spår, riktning eller tågstatus kan invalidiera ärendet.

### 12.2 Linjen är ledig

Detta är ett ensidigt meddelande, inte en fråga. Avsändaren väljer en servervaliderad action och skickar ett komplett paket. Mottagarens `#` är endast visningskvittens. Servern ska skilja på:

- `server_registered`
- `delivered_to_device`
- `display_acknowledged`

Ingen av dessa betyder att mottagaren lämnat ett operativt reply.

## 13. Protokoll och händelser

Anpassa transporten till befintlig Trainmeet-stack. En rimlig modell är HTTP för bootstrap/snapshot/kommandon och WebSocket för händelseprenumeration, men återanvänd ett befintligt säkert transportlager om repositoryt redan har ett.

Varje operativt kommando bör minst innehålla:

```json
{
  "type": "train.track.change",
  "message_id": "uuid",
  "device_id": "TMBOX-7A42F1",
  "station_id": "cda",
  "train_run_id": "run-2026-421",
  "expected_revision": 7,
  "sent_at": "2026-08-19T12:30:00Z",
  "payload": {
    "from_track_id": "1A",
    "to_track_id": "1B"
  }
}
```

Varje serverhändelse bör minst innehålla:

```json
{
  "type": "train.track_changed",
  "event_id": "evt-uuid",
  "server_id": "TM-SERVER-A91C",
  "station_id": "cda",
  "train_run_id": "run-2026-421",
  "revision": 8,
  "occurred_at": "2026-08-19T12:30:01Z",
  "payload": {
    "scheduled_track_id": "1A",
    "assigned_track_id": "1B"
  }
}
```

Definiera JSON Schema, typed DTO eller motsvarande för alla protokollobjekt. Versionera protokollet. Avvisa okända inkompatibla versioner begripligt.

Minsta kommandon/händelser att stödja eller mappa till befintliga motsvarigheter:

- `device.hello`
- `device.presence`
- `device.config.get`
- `device.config.ack`
- `state.sync`
- `train.lookup`
- `train.position.set`
- `train.crew_ready.set`
- `train.track.change`
- `train.departed`
- `train.arrived`
- `clearance.request`
- `clearance.response`
- `clearance.cancel`
- `clearance.revised`
- `clearance.approved`
- `clearance.rejected`
- `clearance.expired`
- `line.available.publish`
- `event.ack`

Servern ska returnera både resultat och aktuell snapshot/revision vid konflikt så att TMBox kan återställa rätt vy.

## 14. Lokala tillståndsmaskiner

Implementera uttryckliga tillståndsmaskiner och tester, inte spridda booleska flaggor.

### 14.1 Anslutning

```text
boot
→ wifi_connecting
→ wifi_setup_required | server_discovery
→ server_identity_confirmation
→ device_registration
→ waiting_for_station_assignment
→ config_sync
→ ready
→ reconnecting
→ state_resync
→ ready
```

### 14.2 Avgående tåg

```text
planned
→ track_selected
→ positioned
→ crew_ready
→ ready_for_clearance
→ clearance_waiting
→ clearance_approved | clearance_rejected
→ departed
→ en_route
→ arrived_remote
→ completed
```

Spårbyte är en separat validerad transition som kan ske i flera tillstånd och vars effekt bestäms av serverreglerna.

### 14.3 Ankommande tåg

```text
announced
→ clearance_incoming
→ accepted | rejected
→ en_route
→ approaching
→ arrived
→ at_station
```

### 14.4 Displaynavigation

Håll lokal navigation, urval, kommandosida och inputbuffer separerade från domäntillståndet. Ett lokalt sidbyte får inte förändra serverstatus.

## 15. Offline, retry och återstart

TMBox ska skilja tydligt på:

- Wi-Fi saknas.
- Wi-Fi finns men Trainmeet hittas inte.
- Trainmeet hittas men enheten saknar behörighet eller stationstilldelning.
- Aktiv anslutning bröts.

Vid offline:

- Visa senaste verifierade status med tydlig `CACHE`/`INGEN KONTAKT`-markering.
- Tillåt lokal navigation och diagnostik.
- Tillåt inte nya `KLART`, `EJ KLART`, `BEGÄR`, `AVGÅTT` eller `ANKOMMIT` när servern inte kan verifiera förutsättningarna.
- Köa inte gamla operativa tangenttryckningar för blind senare sändning.
- Om ett komplett kommando hann skickas men svaret tappades får samma `message_id` återanvändas efter sync för att fråga efter utfallet; skapa inte ett nytt beslut.
- Efter återanslutning skickar TMBox `last_event_id`, lokal konfigurationsversion och senast kända revision.
- Servern returnerar snapshot plus missade händelser.
- TMBox bygger om vyn från den verifierade serverstatusen, inte från antaganden.

Icke-operativa telemetri- och visningskvittenshändelser kan köas om det är säkert och begränsat, men de får aldrig tränga undan verksamhetskritisk synkronisering.

## 16. Robusthet och säkerhet

- TLS för serverkommunikation.
- Stabil verifierbar serveridentitet.
- Enhetsautentisering med token, certifikat eller befintlig Trainmeet-modell.
- Hemligheter lagras säkert på plattformen och loggas aldrig.
- Begränsa captive portal till installationsläge och stäng den efter lyckad konfiguration.
- Validera alla payloads på både enhet och server.
- Rate limiting och dubblettskydd på servern.
- Idempotens för retry.
- Optimistic concurrency med revisioner.
- Watchdog och återställningsbar lokal lagring.
- Atomisk konfigurationsuppdatering med föregående fungerande version som fallback.
- Signerade OTA-uppdateringar om OTA ingår.
- Auditlogg för operativa kommandon med enhet, station, användarroll/källa, tid, gammalt tillstånd, nytt tillstånd och korrelations-ID.

## 17. Rekommenderad enhetsstruktur

Anpassa namnen till repositoryts språk, men separera minst följande ansvar:

- `DeviceIdentity`
- `WifiManager`
- `SetupPortal`
- `ServerDiscovery`
- `TrainmeetClient`
- `SubscriptionClient`
- `ConfigStore`
- `SnapshotStore`
- `KeypadInput`
- `DisplayRenderer`
- `AttentionController`
- `LocalNavigationState`
- `TrainWorkflowState`
- `CommandBuilder`
- `RetryAndSyncCoordinator`
- `Watchdog/Health`

Undvik att göra en enda stor loop med nätverk, display och domänregler sammanblandade. Renderaren ska kunna testas med rena fixtures utan fysisk display. Protokollklienten ska kunna testas med en fake-server.

## 18. Trainmeet-serverns datamodell

Återanvänd befintliga tabeller och modeller där de passar. Lägg annars till motsvarande begrepp med migreringar:

- `devices`
- `device_assignments`
- `device_config_versions`
- `stations`
- `station_tracks`
- `station_connections`
- `train_services` eller stabilt tåguppdrag
- `train_runs` för dagens konkreta körning
- `train_track_assignments`
- `track_preferences`
- `clearances`
- `clearance_events`
- `line_available_messages`
- `device_event_offsets`
- `audit_events`

Använd främmande nycklar, unika constraints och index som gör dubbletter och motsägelsefulla aktiva klareringar svåra att skapa. Exempel: en aktiv request per relevant tågrörelse/revision, unikt `message_id`, unikt `event_id` och en monotont ökande revision per `train_run` eller aggregate.

## 19. Administration i Trainmeet

Administrationen behöver minst kunna:

- Se online/offline TMBox-enheter och deras hårdvaru-ID.
- Se firmware, konfigurationsversion, senaste kontakt och aktuellt anslutningsfel.
- Tilldela eller byta station.
- Definiera displaystorlek och stationskonfiguration.
- Definiera stationens spår som text-ID:n och sorteringsordning.
- Definiera vänster/höger-topologi och klareringsläge per relation.
- Se aktuell snapshot och väntande klareringsärenden.
- Se auditlogg och korrelations-ID.
- Återkalla en enhet eller tvinga omkonfiguration.

Ett stationsbyte ska kräva tydlig bekräftelse på TMBox och får inte ske tyst mitt i ett aktivt operativt ärende.

## 20. Tester som måste finnas

### 20.1 Enhetstester

- Debounce och tangentsekvenser.
- Lokal tågnummerinmatning utan nättrafik före `#`.
- Fasta kommandosidor med `C`.
- Spårväljare för `1A`, `1B`, `2A`, `2B`.
- `*` radera/tillbaka och säkert avbrottsflöde.
- `#` kan inte lämna operativt KLART.
- Renderingsfixtures för 16×2, 20×2, 16×4 och 20×4.
- Ingen rad överskrider vald bredd.
- Fullständigt stationsnamn används endast när det ryms.
- `TOMT` kontra blank saknad anslutning.

### 20.2 Servertester

- Idempotent `message_id`.
- Revision conflict returnerar aktuell snapshot.
- Spår-ID lagras som text.
- Tidtabellsspår skrivs inte över av tilldelat spår.
- Förslag nästa körning nycklas på rätt trafikmönster, inte bara tågnummer.
- Spårbyte före request.
- Spårbyte under väntande request.
- Spårbyte efter KLART invalidierar eller reviderar enligt regel.
- Avgångsspår kan inte ändras efter avgång.
- Ankomstspår kan ändras av mottagande station före ankomst.
- Request/reply komplett normalflöde.
- EJ KLART-flöde.
- Avbrott, timeout och dubblett.
- Linjen-ledig-meddelande utan falskt reply.
- Flera inkommande ärenden i rätt ordning.
- Enheten får bara data för tilldelad station och tillåtna topics.

### 20.3 Integrations- och återstartstester

- Första uppstart utan Wi-Fi → captive portal → Wi-Fi klart.
- Dynamisk Trainmeet-adress via senast känd endpoint, mDNS och manuell fallback.
- Ny enhet → admin tilldelar Cda → konfigurationssync → lokal bekräftelse.
- Komplett avgång 421 Cda → Vst med spårbyte 1A → 1B.
- TKL sätter uppställt och lokförare på plats; servern härleder redo.
- Komplett ankomst 428 Vst → Cda till 2A.
- Nätavbrott före, under och efter skickat kommando.
- Omstart mitt i väntande klarering återställer samma serververifierade läge.
- Två enheter försöker ändra samma tåg med olika revisioner.
- Gammal programversion får begripligt protokoll-/konfigurationsfel.

## 21. Observability och diagnostik

Logga strukturerat med korrelations-ID men utan hemligheter. Minsta diagnostik:

- boot reason
- device ID
- firmware/protokoll/config-version
- Wi-Fi-state utan lösenord
- server ID och endpoint
- stationstilldelning
- reconnect count och senaste event offset
- kommando-ID och resultat
- state transition från/till
- display-rendering error eller radöverskridning
- watchdog reset och OTA-resultat

Ge användaren korta begripliga fel på displayen och detaljerad diagnostik i admin/logg.

## 22. Implementationsordning

Föreslagen ordning, anpassad efter vad som redan finns:

1. Inventera repository, gamla namn och befintligt protokoll.
2. Skriv en kort faktisk gap-analys mot denna specifikation.
3. Inför canonical `TMBox`/`tmbox` och kompatibel migrering.
4. Färdigställ delad protokollmodell och servervalidering.
5. Implementera datamodell/migrering för enheter, tracks, runs, klarering och revisioner.
6. Implementera enhetens konfiguration, anslutning och sync.
7. Implementera lokal input, displayrenderer och fasta kommandosidor.
8. Implementera spårväljare och alfanumeriska spår.
9. Implementera avgångsflödet.
10. Implementera request/reply och linjen-ledig.
11. Implementera ankomstflöden och trafiköversikt.
12. Implementera offline/resync, uppmärksamhetsläge och watchdog.
13. Lägg till adminstöd, tester, fixtures och dokumentation.
14. Kör full verifiering och åtgärda alla regressioner.

## 23. Definition of done

Arbetet är inte klart förrän:

- Produkten heter TMBox konsekvent och en säker namnbytesplan finns för GitHub och externa beroenden.
- En ny fysisk eller simulerad TMBox kan starta, visa sitt ID, ansluta till Wi-Fi, hitta en Trainmeet-server med föränderlig URL och tilldelas Charlottendal.
- Enheten kör på lokal nedladdad konfiguration och skickar inte varje tangenttryckning till servern.
- 16×2, 20×2, 16×4 och 20×4 renderas utan överfulla rader.
- Spåren `1A`, `1B`, `2A`, `2B` fungerar genom hela stacken.
- TKL kan ändra spår med `B`, `C`, `#` och `*` utan konflikt med A/B som operativa knappar.
- Servern lagrar tidtabellsspår, förslag, tilldelning och faktiskt spår separat.
- Nästa körning kan få ett historikbaserat men servervaliderat spårförslag.
- TKL kan registrera uppställt och lokförare på plats; Trainmeet härleder klar att begära.
- Avgång 421 Cda → Vst fungerar komplett.
- Ankomst 428 Vst → Cda fungerar komplett.
- Både request/reply och linjen-ledig fungerar och blandas aldrig ihop.
- Spårbyte under väntande eller godkänd klarering hanteras säkert med revision/nytt beslut.
- Nätavbrott kan inte skapa ett lokalt falskt KLART eller återspela ett gammalt operativt tangenttryck.
- Alla relevanta tester passerar.
- Dokumentationen beskriver installation, konfiguration, protokoll, tillstånd, felsökning och namnbyte.
- Inga centrala delar lämnas som TODO, mock eller enbart pseudokod.

## 24. Vad du ska leverera tillbaka

När implementationen är färdig ska du redovisa:

1. Kort sammanfattning av vad som faktiskt implementerats.
2. Arkitektur och viktiga designbeslut.
3. Alla ändrade och nya filer grupperade per område.
4. Databasmigreringar och kompatibilitetskonsekvenser.
5. Protokoll-/API-förändringar med exempel.
6. Hur TMBox byggs, flashas, körs och konfigureras.
7. Hur Trainmeet-servern körs och administrerar enheten.
8. Testkommandon och faktiska testresultat.
9. GitHub-/repositorynamnbyteschecklista inklusive nya URL:er/remotes som användaren själv måste ändra om du saknar behörighet.
10. Kvarvarande risker eller verkliga blockerare — inte allmänna framtidsidéer.

Bevara enkelheten: TMBox ska kännas som en fokuserad fysisk terminal. Komplexiteten hör hemma i Trainmeets validerade tillstånd och konfiguration, inte i att TKL måste förstå nätverk, API:er eller interna domänobjekt.
