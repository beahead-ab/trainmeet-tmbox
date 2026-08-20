# Monsterprompt v2: TMBox — nuläge, beslut och väg till första releasen

Detta ersätter `tmbox-monsterprompt-claude.md` (2026-08-19). Skillnaden är att den
ursprungliga prompten skrevs **utan kännedom om befintlig kod**. Denna version är
skriven **efter** en fullständig genomgång av `trainmeet-tambox`,
`trainmeet-server` och `trainmeet-cloud`, och efter att en första
implementationsvåg faktiskt byggts och testats.

Läs §0 först. Den beskriver vad som verkligen finns idag — resten av dokumentet
är meningslöst utan den utgångspunkten.

---

## 0. Nuläge (verifierat 2026-08-20)

### 0.1 ESP32-firmwaren — vad boxen faktiskt gör idag

`firmware/esp32/TrainMeetTambox.ino`, 497 rader, orörd sedan första committen.
**Ingenting av protokoll v2-arbetet finns på hårdvaran.**

Boxen är idag en **ren tunn klient**. Den har ingen kunskap om tåg, spår,
klarering eller flöden. Den gör exakt fyra saker:

| Funktion | Implementation |
|---|---|
| Visa två rader à 16 tecken | Skriver ut `display.line1` / `line2` från snapshoten ordagrant |
| Skicka tangenttryck | Ett MQTT-kommando per tryck, om tangenten finns i `allowed_keys` |
| Anslutning | Wi-Fi (WiFiManager captive portal), mDNS-tjänst `tambox`, MQTT utan lösenord |
| Identitet | `TBX-XXXX` härlett ur efuse-MAC |

Firmwaren har alltså **inga** lokala tillståndsmaskiner, ingen lokal
inmatningsbuffert, ingen spårväljare, inga kommandosidor, inget ljud, inga
lampor och inget stöd för 4 rader eller 20 tecken. Servern renderar allt.

### 0.2 Vilka flöden stöder Arduino idag — fullständig genomgång

Eftersom boxen bara ritar det servern skickar, är frågan i praktiken: *vad
implementerar `engine.py` (protokoll v1)?* Svaret är en betydligt enklare modell
än monsterprompten beskriver.

**Interaktionslägen som finns (`InteractionMode` i `models.py`):**

```text
IDLE               → välj riktning med A/B/C/D (en slot per sträcka)
ENTER_TRAIN        → siffror, # skickar, * avbryter
AWAITING_PERMISSION→ * avbryter begäran
INCOMING_REQUEST   → # accepterar, * avslår
READY_DEPARTURE    → samma slot-tangent bekräftar, * avbryter
CONFIRM_DEPARTURE  → # ja, * nej
CONFIRM_CANCEL     → # ja, * nej
INCOMING_ARRIVAL   → # bekräftar ankomst, * lämnar
```

**Flöden som fungerar end-to-end idag:**

1. Begär klarering: `A` (välj sträcka) → siffror → `#`
2. Ta emot begäran: `A` → `#` (klart) eller `*` (ej klart)
3. Avgång: `A` → `A` → `#`
4. Ankomst: `A` → `#`
5. Avbryt begäran/reservation: `*` → `#`
6. Direktläge (`direct`): första stationen reserverar utan att fråga

**Flöden i monsterprompten som saknas helt på hårdvaran idag:**

- Tåguppslag mot tidtabell (`train.lookup`) — v1 tar bara ett fritextnummer
- Spårval och spårkatalog — v1-motorn känner inte till spår över huvud taget
- `UPPSTÄLLT` och `FÖRARE PÅ PLATS`
- `LINJEN ÄR LEDIG` som eget flöde (direktläge är inte samma sak)
- Trafiköversikt med dubbelspårsrader
- 20 tecken och 4 rader
- Ljud, lampor, uppmärksamhetsläge
- Kö vid flera inkommande ärenden

**Viktig avvikelse att åtgärda:** v1-motorn använder `#` för att lämna operativa
beslut — `INCOMING_REQUEST` (`#` = KLART), `CONFIRM_DEPARTURE` (`#` = avgått) och
`INCOMING_ARRIVAL` (`#` = ankommit). Detta bryter mot säkerhetsregeln i §7.
Regeln kan därför inte införas i firmwaren isolerat; den måste rullas ut
synkront i motorn, webbsimulatorn, Swift-klienten och TKL-terminalen.

### 0.3 Servern — vad som byggts och testats

Branch `protocol-v2` i `trainmeet-server` ([PR #1](https://github.com/beahead-ab/trainmeet-server/pull/1)),
9 commits, 119/119 tester gröna, verifierat i webbläsare och mot en riktig
Mosquitto-broker.

| Byggt | Var |
|---|---|
| Spårkatalog med stabila text-id:n, servervaliderad | `models.py`, `runtime.py` |
| `crew_ready` som TKL:s egen kolumn | `operations.py` |
| `assigned_track_id` med beläggningskontroll | `operations.py` |
| Stationsbaserad enhetstilldelning | `identity.py` |
| Klareringsaggregat med riktade dubbelspårskanaler | `operations.py` |
| Linjen-ledig som tvåstegsmeddelande | `operations.py` |
| Per-rörelse-revision | `operations.py` |
| HTTP-yta `/v1/tmbox-v2/*` | `http_server.py` |
| Webbsimulator "TMBox v2" | `web/` |
| **MQTT v2-adapter** (`tmbox/v2/...`, hello/config/snapshot/command/ack) | `mqtt_adapter_v2.py` |

**Hårdvaran kan nu nå v2-lagret.** MQTT-adaptern är ren transport ovanpå de
redan testade `v2_*`-metoderna — ingen affärslogik duplicerad. Verifierat
end-to-end mot en riktig broker, inklusive en fullständig
tvåstations-klareringsväxling med upptagen-kanal-avvisning. Det som saknas är
firmware som faktiskt ansluter och pratar protokollet.

### 0.4 Vad som därmed återstår för en första riktig release

Se §22 för ordningen. Kort: namnbyte i firmware, firmware v2 enligt §3.4a, och
frågan om TKL-pass som §22 flaggar (en fysisk box kan idag inte agera utan att
någon redan startat ett pass via webbsimulatorn eller TKL-terminalen).

---

## 1. Namnet är TMBox

Oförändrat från originalet, med ett förtydligande:

- Produktnamn i löptext och GUI: **TMBox**
- Teckendisplay: **TMBOX**
- Slug/mappar/paket: **tmbox**
- Enhets-ID: `TMBOX-<hårdvaru-id>`, exempelvis `TMBOX-7A42F1`
- **Repositoryt döps till `trainmeet-tmbox`**, inte `tmbox` — så repo-familjen
  förblir konsekvent (`trainmeet-server`, `trainmeet-cloud`, `trainmeet-iphone`,
  `trainmeet-tkl`). Detta är ett fattat beslut (B6).

Noll fysiska boxar är driftsatta. **Gör namnbytet tidigt och utan
kompatibilitetslager** — inklusive enhets-id-formatet `TBX-XXXX` → `TMBOX-XXXXXX`
och MQTT-prefixet `tambox/v1` → `tmbox/v2`. Ingen migrering behövs.

## 2. Produktens kärna

Oförändrad. TMBox är en fysisk terminal för tågrörelser och klarering mellan
trafikplatser. Inte ett ställverk. LocoNet, JMRI, digital ställverkspanel och
förseningstjänster ligger utanför.

Ett tillägg som saknades i originalet: **all tid som visas är träffklockan**,
aldrig väggklockan. Träffklockan kan gå i annan hastighet och kan stoppas
(`operations.py` implementerar den redan). En skärm som visar `ANKOMMER 14:32`
menar modelltid.

## 3. Övergripande arkitektur

### 3.1 Ansvarsfördelning

Oförändrad i sak. Trainmeet-servern äger sanningen; TMBox skickar kompletta
idempotenta kommandon och renderar verifierade tillstånd. En tangenttryckning som
bara flyttar en markör eller skriver en siffra skickas aldrig till servern.

### 3.2 Transport: MQTT, inte HTTP/WebSocket

**Beslut B1.** Originalets HTTP+WebSocket-förslag utgår. Protokoll v2 körs över
befintlig Mosquitto-stack med QoS 1 och retained snapshots — samma infrastruktur
som v1, nytt topic-prefix `tmbox/v2/...`.

Skälet är att retained-mekanismen redan löser det svåraste problemet: en box som
återansluter får ett komplett läge utan att fråga, oavsett i vilken ordning
meddelandena kommer.

### 3.3 Enheten tilldelas en station, inte en panel

**Den enskilt största arkitekturändringen.** V1 tilldelar en enhet en eller flera
`panel_id` med fasta A–D-slots mot sträckor. V2 tilldelar enheten **en station**.
Trafiköversikten byggs då av en topologikonfiguration i `device.config`, inte av
fasta slots.

Konsekvens: v1:s `TrafficEngine`/`ConnectionRuntime`/`PanelRuntime` byggs **inte**
vidare ut för TMBox. De var byggda för tangent-för-tangent-A–D-protokollet. V2:s
motsvarighet är det movement-/klareringslager som redan finns i `operations.py`.

### 3.4 Beslut: boxen är tyst tills den har något komplett att säga

**Fattat 2026-08-20.** Boxen håller en levande lokal kopia av sin stations data
i RAM — dagens tidtabell och aktuell status — och pratar bara på nätet för att
skicka ett komplett operativt kommando. Ingen rundresa per tangenttryck, ingen
rundresa för att slå upp ett tåg eller bläddra ett spår.

Den avgörande principen som gör detta säkert är en skarp gräns mellan tre
lager, som **aldrig får blandas ihop**:

1. **Data** (tidtabell, spårkatalog, levande rörelsestatus) — cachas
   aggressivt i RAM, uppdateras automatiskt via retained MQTT-topics. Detta är
   fritt fram, och är precis vad som gör boxen snabb.
2. **Formatering** (teckenbudget, pilgrammatik, `TOMT`-logik, fasta
   kommandosidor — §6) — mekaniska regler, byggda in i firmware. Ändras
   sällan, kräver omflashning när de ändras, men är stabila nog för det.
3. **Affärslogik och ordalydelse** (vilka `allowed_actions` som gäller, exakt
   skärmtext, klareringsregler) — **stannar hos servern, alltid**. Skickas
   som data (lager 1), inte som kod. En ändrad regel eller ett ändrat ordval
   ska nå en ansluten box direkt via samma retained config-kanal — ingen
   omflashning, och alla fyra klienter (box, webb, Swift, TKL) förblir
   synkade eftersom ingen av dem har hårdkodat sin egen version av sanningen.

Det som gjorde tangentinmatning trögt var aldrig MQTT i sig (10–30 ms på ett
lokalt nät) — det var att varje knapptryck betalade en egen rundresa. Ett
komplett kommando tar bort det. Att sedan hålla stationens snapshot lokalt tar
bort väntan även för uppslag och navigering, eftersom svaret redan finns i RAM
när frågan ställs.

Kapacitet är inte begränsningen — en ESP32-S3 klarar detta med bred marginal.
Se §3.4a för den konkreta datan boxen håller.

### 3.4a Vad boxen håller i RAM

Två retained MQTT-topics fyller RAM-kopian. De uppdateras oberoende av
varandra och boxen slår aldrig ihop dem manuellt — varje mottagen payload
ersätter föregående i sin helhet. Ingen delta-logik i firmware; enklast och
säkrast.

**`tmbox/v2/device/{id}/config`** — ändras bara vid ny driftpaket-aktivering.

```json
{
  "config_version": 12,
  "station": { "id": "st-cda", "code": "CDA", "name": "Charlottendal" },
  "tracks": [
    { "id": "track-a-1a", "display_label": "1A", "sort_order": 10 },
    { "id": "track-a-1b", "display_label": "1B", "sort_order": 20 }
  ],
  "connections": [
    { "connection_id": "connection-a-b", "other_station_code": "VST",
      "track_type": "single", "dispatch_mode": "clearance", "display_row": 1 }
  ],
  "display": { "rows": 4, "cols": 20, "charset": "ascii" }
}
```

**`tmbox/v2/device/{id}/snapshot`** — ändras vid varje operativ händelse som
rör stationen. Detta är samma form som `v2_station_snapshot` redan returnerar
till webbsimulatorn i `trainmeet-server` — servern behöver inte bygga något
nytt, bara publicera det befintliga svaret på topicet i stället för att vänta
på en HTTP-fråga.

```json
{
  "revision": {
    "config_version": 12,
    "movements": { "movement-101-a": 4 },
    "cases": { "clr-88f2": 2 }
  },
  "movements": [
    {
      "id": "movement-101-a", "train_number": "101",
      "arrival_time": null, "departure_time": "09:20",
      "departure": "positioned", "arrival": "none",
      "assignedTrackId": "track-a-1a", "actualTrack": null,
      "crewReady": true
    }
  ],
  "active_clearances": [
    { "clearance_id": "clr-88f2", "movement_id": "movement-101-a",
      "connection_id": "connection-a-b", "status": "waiting",
      "from_station_id": "st-cda", "to_station_id": "st-vst" }
  ],
  "line_messages": [],
  "clock": { "time": "09:23", "running": true }
}
```

Tåguppslag blir en lokal sökning i `movements[]` mot `train_number` — inget
skickas på nätet. Spårväljaren bläddrar `config.tracks[]` lokalt. Vilka knappar
som är giltiga (`A=BEGAR`, `B=EJ` osv.) härleds av firmware ur `movements[].
departure`/`arrival`/`crewReady` med **exakt samma mekaniska regler** servern
själv använder för att bygga `allowed_actions` idag — reglerna är stabila nog
att duplicera (lager 2), men de faktiska besluten (godkänt/nekat, tilldelat
spår) kommer aldrig från boxen.

**Vad som uttryckligen inte cachas:** operativa beslut fattas aldrig lokalt.
En knapp som ser tillåten ut enligt cachead data men vars kommando avvisas av
servern (stale revision, spår upptaget under tiden) visar avslagsorsaken och
väntar på nästa snapshot — den agerar aldrig på den gamla datan igen.
Lokal navigation (vald kommandosida, markörläge, `AVBRYT BEGARAN?`-frågan) är
ren UI-state och rör aldrig detta lager, enligt §14.4.

### 3.5 Plattform

**ESP32-S3 med Arduino-ramverket.** Ingen Raspberry Pi krävs för boxen.
TrainMeet Server körs separat (Pi, Mac, PC eller VPS) och är alltid den lokala
auktoriteten under en träff. Cloud är byggkedja, aldrig runtime-part.

## 4. Anslutning, installation och identitet

### 4.1 Stabil enhetsidentitet

Oförändrad princip. Formatet byts till `TMBOX-7A42F1`.

```text
TMBOX-7A42F1
STARTAR...
```

### 4.2 Wi-Fi

Oförändrad. Ett förtydligande: WiFiManager lagrar **en** profil. Fleranätsstöd
(primärt + reserv) är en egen senare slice, inte ett krav för första releasen.

Fyra granskningsfynd i befintlig firmware ska åtgärdas i samma veva:

1. Installationsportalen har ingen timeout och stoppar STA-återförsök — en
   router som startar om i tre minuter låser boxen i portalläge tills någon
   bryter strömmen.
2. Ett nyfabrikerat kort väntar ~45 s innan portalen öppnas, i stället för att
   upptäcka att inga sparade uppgifter finns.
3. Ingen MQTT last will — en box som tappar strömmen står kvar som `online`.
4. Tidsjämförelser av typen `now >= nextX` är inte wraparound-säkra.

### 4.3 Serverupptäckt

Ordning: senast verifierade endpoint → mDNS → manuell adress.

**Att åtgärda:** firmwaren annonserar/söker tjänsten `tambox`, specen säger
`_trainmeet._tcp`. Standardisera till ett namn och lägg `server_id` i TXT-posten.
Använd dessutom `MDNS.IP(0)`, inte `MDNS.hostname(0)` — ett mDNS-värdnamn går
ofta inte att slå upp via vanlig DNS, vilket ger en box som "hittar" servern men
aldrig kan ansluta.

### 4.4 Tilldelning till station

Oförändrad. `device.hello` innehåller `device_id`, hårdvaruversion,
firmwareversion, displaykapacitet, konfigurationsversion, protokollversion.

```text
CDA TILLDELAD
#=BEKRAFTA
```

## 5. Stationskonfiguration och referensdata

**Beslut B5.** Två referenskonfigurationer, med olika syften:

- **Charlottendal (verklig)** — station med driftplatserna `C` och `RBG`, finns
  redan i `trainmeet-cloud` och byggs av PDF-importen. Detta är
  **integrationsfixturen**: allt som ska bevisas fungera mot publicerbar data
  körs mot den.
- **Cda / Lek / Vst / Kun (fiktiv)** — topologin i originalprompten (dubbelspår
  vänster och höger, Kungsfors som tredje anslutning, spåren `1A`/`1B`/`2A`/`2B`).
  Behålls som **enhetstestfixture** där topologin måste vara konstruerad, t.ex.
  för dubbelspårskanalerna.

En befintlig linje utan aktuellt tåg visas `TOMT`. En anslutning som inte finns
lämnas blank.

## 6. Displayregler

Stöd 16×2, 20×2, 16×4, 20×4. Logiken identisk; skillnaden är hur mycket kontext
som ryms.

Oförändrat: exakt teckenbudget, fasta kommandosidor (inga rullande kommandon),
riktningsgrammatik (`421>[1A]`, `[2A]<428`).

**Att avgöra:** teckenuppsättning. HD44780 saknar ÅÄÖ. Antingen
ASCII-translitterering (`SPAR`, `BEGAR`, `FORARE`) eller ÅÄÖ via CGRAM (8
platser räcker för `ÅÄÖåäö`). `hello` bör annonsera `display: {rows, cols,
charset}` så servern vet vad boxen klarar.

## 7. Knappmodell

Oförändrad i sak:

- `0–9` numerisk inmatning
- `A` primär operativ handling (`UPPSTÄLLT`, `FÖRARE`, `BEGÄR`, `KLART`, `AVGÅTT`, `ANKOMMIT`)
- `B` sekundär/negativ (`EJ KLART`, `ÄNDRA`)
- `C` nästa tåg / rad / kommandosida
- `D` mer information / vyväxling
- `*` radera, tillbaka, initiera avbrott
- `#` välj, OK, bekräfta data, kvittera visning

**Säkerhetsregeln står fast:** `#` får aldrig lämna operativt `KLART`,
`EJ KLART`, `AVGÅTT` eller `ANKOMMIT`. `A`/`B` bär de operativa besluten.

Men se §0.2: **v1-motorn bryter mot detta på tre ställen idag.** Införandet är
därför inte en firmware-ändring utan en samordnad utrullning i motor + alla fyra
klienter. Planera det som ett eget steg (§22).

Etikettregel: avslagsknappen heter **`B=EJ`** överallt. Originalets `B=NEJ` i två
undantagsskärmar är ett fel som ska rättas i flödesbilden.

Ett tillägg: **inmatningsspärr ~500 ms efter varje skärmbyte**, så ett tryck
avsett för föregående skärm inte tolkas mot den nya.

## 8. Tåguppslag

Oförändrat i grunden. Ett tillägg som saknades:

**Tvetydiga tågnummer.** Samma tågnummer kan ha flera rörelser vid stationen
samma dag (t.ex. en ankomst och en senare avgång). Servern väljer i första hand
via tidsfönster mot träffklockan. Om det ändå är tvetydigt returneras en lista
och boxen bläddrar med `C`:

```text
421 2 TRAFFAR
C=NASTA #=VALJ
```

Denna skärm saknas i flödesbilden och ska läggas till.

## 9. Spårmodell och spårväljare

### 9.1 Spår är textidentifierare — **byggt**

Implementerat som `TrackConfig` i `models.py` och en toppnivålista `tracks[]` i
driftpaketet. Fält: `id`, `display_label`, `station_id`, `operating_point_id`,
`active`, `sort_order`.

**Avvikelse från planen värd att känna till:** detta lades till *additivt inom
`RUNTIME_SCHEMA_VERSION 2`*, inte som en versionsbump till 3. Att bumpa
schemakonstanten är en hård, repo-omfattande övergång som hade brutit varje
befintlig fixture i hela servern — inte bara de TMBox-nära delarna.
`operating_points` lades till på exakt samma additiva sätt tidigare.

### 9.2 Spårväljaren

Oförändrad. `B=ÄNDRA` öppnar serverstyrd lista, `C` bläddrar, `#` väljer, `*`
lämnar. A/B används aldrig som bokstavsinmatning.

### 9.3 Separata spårvärden

`scheduled_track_id` (tidtabellens), `suggested_track_id` (serverns förslag),
`assigned_track_id` (TKL:s val — **byggt**), `actual_track_id` (verkligt utfall —
**fanns redan**). Tidtabellsspåret skrivs aldrig över.

**Byggt:** ett spår kan bara vara tilldelat en icke-avgången rörelse per
station/dag. Kollision ger `track_occupied`:

```text
SPAR 1A UPPTAGET
B=ANDRA D=MER
```

### 9.4 Förslag nästa körning

Inte byggt. `track_preferences` nycklat på trafikmönster (inte bara tågnummer).
Förslagsordning: tilldelat → tidtabell → historik → stationsdefault. Historik är
alltid ett förslag, aldrig ett automatiskt beslut.

### 9.5 Spårbyte under olika tillstånd

Oförändrade regler. `invalidate_clearance()` finns i `operations.py` som krok —
men **ingen anropar den ännu**. Att koppla spårbyte till invalidering av väntande
klarering är kvarvarande arbete.

## 10. Avgångsflöde: tåg 421 Cda → Vst

Oförändrat i sak, med **en rättelse**:

### 10.4 Lokförare på plats — rättad skärm

Originalet visade **identisk skärm före och efter** `A=FÖRARE`, vilket inbjuder
till dubbeltryck. Rätt sekvens:

Före:
```text
421 [1B] UPPST
A=FORARE D=MER
```

Efter (ny skärm):
```text
421 [1B] FORARE
VANTAR REDO...
```

Sedan, när servern härlett redo (`positioned && crew_ready && rules_ok`):
```text
421 [1B] REDO
A=BEGAR D=MER
```

**Beslut B2:** `UPPSTÄLLT` och `FÖRARE PÅ PLATS` är TKL:s egen lägeskontroll och
**registreras beständigt på servern** — samma restart-garanti som
`departure_status`. En omstart får aldrig tappa dem. De är uttryckligen skilda
från rangerarnas `train_readiness`-flöde; rangerarna får en egen framtida panel.

## 11. Ankomstflöden till Charlottendal

Oförändrat, med ett tillägg: **`approaching` som egen TKL-åtgärd** (`D=NÄRMAR
SIG`), som låter TKL bekräfta observation innan tåget fysiskt är inne. Kolumnen
fanns redan i databasens CHECK-villkor men saknade anropare — nu byggd.

Regeln står fast: `A=ANKOMMIT` gör statusändringen, aldrig `#`.

## 12. Två klareringslägen

### 12.1 Request/reply — **byggt**

Tillstånd: `waiting → approved | rejected | cancelled | expired |
invalidated_by_revision`. Stabilt `clearance_id`, `message_id` för idempotens,
TTL med lat utgång (kontrolleras vid varje request/response, så korrektheten
aldrig beror på att en bakgrundsjobb hunnit köra).

**Byggt (B7): riktade kanaler.** En enkelspårsförbindelse har en delad kanal; en
dubbelspårsförbindelse har en oberoende kanal per riktning. Motriktade rörelser
blockerar aldrig varandra.

### 12.2 Linjen är ledig — **byggt**

Ensidigt meddelande, aldrig en fråga. **Två tillstånd, inte tre:**
`delivered_to_device → display_acknowledged`. Originalets `server_registered` är
inte ett enhetssynligt tillstånd. Meddelandet beläggningskontrolleras aldrig mot
en klareringsbegäran — det är inget beslut.

## 13. Protokoll och händelser

### 13.1 Topics

```text
box → server   tmbox/v2/device/{id}/hello           QoS1
server → box   tmbox/v2/device/{id}/assignment      QoS1 retained
server → box   tmbox/v2/device/{id}/config          QoS1 retained
box → server   tmbox/v2/device/{id}/config/ack      QoS1
box → server   tmbox/v2/device/{id}/presence        QoS1 retained
box → server   tmbox/v2/device/{id}/command         QoS1
server → box   tmbox/v2/device/{id}/ack             QoS1
server → box   tmbox/v2/device/{id}/snapshot        QoS1 retained
server → alla  tmbox/v2/gateway/{id}/status         QoS1 retained
```

En snapshot-topic per **enhet**, inte per panel. `config` skiljer statisk
stationskonfiguration från dynamisk trafikstatus.

### 13.2 Revision — **beslut B4**

Tre skilda revisionsutrymmen i stället för v1:s enda globala räknare:

| Scope | Nyckel | Ökar vid |
|---|---|---|
| `movement` | `movement_id` | uppställt, förare, spårbyte, avgång, ankomst |
| `case` | `clearance_id` / meddelande-id | begär, svara, avbryt, revidera |
| `config` | station | ny driftpaket-aktivering |

Ett kommando anger exakt ett scope. Global revision skalar inte — en orelaterad
händelse vid en annan station skulle annars ge falska `stale_revision`-avslag.

### 13.3 Inget event-replay

`last_event_id`, `device_event_offsets` och missade-händelser-uppspelning i
originalets §13/§15 **utgår**. Retained assignment + config + snapshot vid
återanslutning **är** hela synkmekanismen. Snapshoten är sanningen; händelser är
flyktiga.

### 13.4 Kommandokatalog

`device.hello`, `device.presence`, `device.config` (+ `ack`), `train.lookup`,
`train.position.set`, `train.crew_ready.set`, `train.track.change`,
`train.departed`, `train.arrived`, `clearance.request`, `clearance.response`,
`clearance.cancel`, `line.available.publish`, `line.available.acknowledge`.

`state.sync` sker implicit via retained-mekanismen. `event.ack` utgår som egen
topic — kvittensen som bifogas varje kommando täcker command-triggade fall, och
för ensidiga meddelanden är det `display_acknowledged`.

## 14. Lokala tillståndsmaskiner

Oförändrade. `state_resync` betyder konkret "vänta på retained assignment +
config + snapshot".

Håll lokal navigation, urval, kommandosida och inputbuffert **separerade** från
domäntillståndet. Ett lokalt sidbyte får aldrig ändra serverstatus.

## 15. Offline, retry och återstart

Oförändrade principer. Skilj tydligt på: Wi-Fi saknas / Wi-Fi finns men servern
hittas inte / servern finns men enheten saknar tilldelning / aktiv anslutning
bröts.

Vid offline: visa senaste verifierade status märkt `CACHE` / `INGEN KONTAKT`.
Tillåt aldrig nya operativa beslut. **Köa aldrig gamla operativa tangenttryck.**

Två förtydliganden:

- **Cachen ligger i RAM, inte i NVS.** Att skriva snapshots till flash ger
  slitage och risk att en gammal bild visas som aktuell efter omstart.
- Om ett kommando hann skickas men svaret tappades får samma `message_id`
  återanvändas för att fråga efter utfallet — aldrig för att skapa ett nytt
  beslut.

## 16. Robusthet och säkerhet

**Beslut B3: v1 kör lösenordsfri MQTT på träffens isolerade nät.** Detta är ett
medvetet, dokumenterat beslut i hela stacken — inte ett förbiseende.
Protokoll v2 reserverar ett `device_token`-fält som ignoreras tills vidare, så
TLS + enhetsautentisering kan läggas till som egen slice utan protokollbrott.

Kvarstår oförändrat: hemligheter lagras säkert och loggas aldrig; captive portal
stängs efter lyckad konfiguration; payloads valideras på båda sidor; idempotens
för retry; optimistic concurrency; watchdog; atomisk konfigurationsuppdatering
med fallback; auditlogg med korrelations-ID.

## 17. Rekommenderad enhetsstruktur

Oförändrad lista, konkretiserad av §3.4/§3.4a-beslutet: `ConfigStore` håller
`config`-topicets payload, `SnapshotStore` håller `snapshot`-topicets payload —
båda ersätts i sin helhet vid varje mottagning, ingen delta-logik. Minst:
`DeviceIdentity`, `WifiManager`, `SetupPortal`, `ServerDiscovery`,
`MqttClient`, `ConfigStore`, `SnapshotStore`, `KeypadInput`, `DisplayRenderer`,
`LocalNavigationState`, `CommandBuilder`, `Watchdog`.

`AttentionController` (ljud/lampor) kräver GPIO som ännu inte finns i
`hardware_profile.h` — lägg till med graceful degradering.

**Krav:** renderaren ska gå att testa med rena fixtures utan fysisk display.
Lägg upp en PlatformIO `native`-miljö så renderare och tillståndsmaskiner körs i
CI utan hårdvara, med fixtures för alla fyra geometrier. CI ska dessutom bygga
`diagnostics/hardware-check`, som idag inte byggs alls.

## 18. Trainmeet-serverns datamodell

Merparten fanns redan. Status per begrepp:

| Begrepp | Status |
|---|---|
| `devices`, `device_assignments` | Fanns (`identity.py`); station_id tillagd |
| `stations`, `station_connections` | Fanns (runtime-schema) |
| `station_tracks` | **Byggt** som `tracks[]` + `TrackConfig` |
| `train_runs` | Fanns som `movement_id` i `tkl_movement_states` |
| `train_track_assignments` | **Byggt** som `assigned_track_id` |
| `clearances`, `clearance_events` | **Byggt** |
| `line_available_messages` | **Byggt** som `line_messages` |
| `track_preferences` | Ej byggt |
| `device_event_offsets` | **Utgår** (§13.3) |
| `audit_events` | Fanns som `tkl_events` |

## 19. Administration i Trainmeet

Oförändrade krav. Det som saknas idag:

- Redigering av spårkatalogen per station/driftplats — **ska byggas i
  `trainmeet-cloud`**, i All data-vyn, med validering att inaktivering blockeras
  för spår som används av en oarkiverad tågrad.
- Per-box-konfiguration: displaygeometri, teckenuppsättning, ljud/lampor.

## 20. Tester

### 20.1 Enhetstester (firmware)

Oförändrad lista. Kräver den native-miljö som §17 beskriver.

### 20.2 Servertester — **115 gröna idag**

Byggt och verifierat: idempotens, spår som text, tidtabellsspår skrivs inte
över, spårbeläggning, request/reply komplett, EJ KLART, avbrott, timeout,
dubblett, linjen-ledig utan falskt reply, riktade kanaler.

Kvarstår: spårbyte under väntande request (kroken finns, anroparen saknas),
spårförslag nästa körning, flera inkommande i rätt ordning.

### 20.3 Integrations- och återstartstester

Ett testfall som ska vara namngivet och explicit: **retained assignment, config
och snapshot får anlända i valfri ordning och ändå ge korrekt slutläge.** Detta
var ett verkligt fynd i den första firmware-granskningen.

## 21. Observability

Oförändrad. Strukturerad loggning med korrelations-ID, utan hemligheter.

## 22. Implementationsordning mot första releasen

Detta ersätter originalets §22 helt. Ordningen utgår från vad som faktiskt finns.

**Klart:** steg 1–5 i originalet (inventering, gap-analys, beslut, protokoll­
kontrakt, datamodell), serverns v2-lager, och **MQTT v2-adaptern**
(`MQTTGatewayAdapterV2`, `protocol-v2`-branchen — hello/assignment/config/
snapshot/command/ack på `tmbox/v2/...`, ren transport ovanpå de redan
testade `v2_*`-metoderna, verifierad mot en riktig Mosquitto-broker inklusive
en fullständig tvåstations-klareringsväxling). **Hårdvaran kan nu nå
v2-lagret** — det som saknas är firmware som faktiskt pratar med den.

**Upptäckt under implementationen, kräver beslut:** `v2_movement_command`,
`v2_assign_track`, `v2_clearance_request` och `v2_line_publish` kräver alla
ett aktivt TKL-pass (`start_tkl_shift`) på stationen och avvisar annars med
`tkl_shift_not_started`. Det är en importerad förutsättning från TKL-lagret,
inte ett medvetet TMBox-beslut. Praktiskt betyder det att en fysisk box inte
kan användas fristående — någon måste ha startat ett pass via webbsimulatorn
eller TKL-terminalen först. Antingen är det avsedd design (TKL "loggar in"
en gång per dag, boxen ärver den identiteten för auditloggen), eller så
behöver TMBox ett eget lätt sätt att sätta en aktör utan ett fullt pass.
Detta måste avgöras innan slutpaketering (steg 9 nedan).

**Kvar till första riktiga TMBox-releasen:**

1. **Namnbyte** i firmware och protokoll: `TBX-` → `TMBOX-`, `tambox/v1` →
   `tmbox/v2`, mDNS-tjänstnamn. Inga driftsatta boxar betyder noll
   migreringskostnad.
2. **Firmware-härdning:** de fyra anslutningsfynden i §4.2 plus mDNS-IP i §4.3.
   Oberoende av §3.4a, kan göras parallellt med (1).
3. **Firmware v2:** `ConfigStore`/`SnapshotStore` enligt §3.4a, lokalt
   tåguppslag och spårväljare mot cachead data, rendering enligt §6, komplett
   kommando vid varje operativ knapptryckning. Native testmiljö + CI för
   `diagnostics/hardware-check`.
4. **`#`-regelns utrullning** (§7) synkront i motor, webbsimulator, Swift och
   TKL. Detta är den enda ändringen som rör alla fyra klienter samtidigt.
5. **Spårkatalog i Cloud-admin** (§19) så katalogen går att redigera, inte bara
   valideras.
6. **Koppla spårbyte till klareringsinvalidering** (§9.5).
7. **Hårdvaruverifiering mot Bennys fysiska box** — kortmodell, I2C-adress,
   kablage, teckenuppsättning.
8. **Slutpaketering:** flashinstruktion, driftdokumentation, felsökningsguide.
   Kräver att TKL-passfrågan ovan är avgjord.

Senare slices, uttryckligen **inte** i första releasen: TLS + enhetsautentisering,
OTA, Wi-Fi-reservnät, spårförslag ur historik, ljud/lampor om GPIO saknas.

## 23. Definition of done — första releasen

- Produkten heter TMBox konsekvent; repot heter `trainmeet-tmbox`.
- En fysisk box startar, visar `TMBOX-XXXXXX`, ansluter till Wi-Fi, hittar
  servern via mDNS och tilldelas Charlottendal från adminvyn.
- Boxen kör på lokal nedladdad konfiguration.
- De fyra anslutningsfynden är åtgärdade och boxen återhämtar sig från
  router-omstart, serveromstart och strömbortfall utan handpåläggning.
- Avgång 421 Cda → Vst fungerar komplett på fysisk hårdvara, inklusive spårval.
- Ankomst 428 Vst → Cda fungerar komplett på fysisk hårdvara.
- Request/reply och linjen-ledig fungerar och blandas aldrig ihop.
- `#` lämnar aldrig ett operativt beslut, i någon klient.
- Nätavbrott kan inte skapa ett lokalt falskt KLART eller återspela ett gammalt
  operativt tangenttryck.
- Renderaren är testad i CI utan fysisk display, för varje geometri boxen stöder.
- Alla tester gröna.
- Dokumentationen beskriver flash, installation, konfiguration, protokoll och
  felsökning.

## 24. Ändringar som ska in i flödesbilden (`tmbox-flodesbild.html`)

Detta är listan du behöver för att uppdatera HTML-referensen. Varje punkt är
antingen ett fel i originalet eller en följd av ett fattat beslut.

### 24.1 Rättade fel

| # | Var | Ändring |
|---|---|---|
| 1 | `departure`, steg 8 | Skärmen efter `A=FÖRARE` var **identisk** med skärmen före. Lägg in mellanskärm: `421 [1B] FORARE` / `VANTAR REDO...` |
| 2 | `request` steg 6, `exceptions` steg 12 | `B=NEJ` → `B=EJ`. En etikett överallt. |

### 24.2 Följder av fattade beslut

| # | Var | Ändring |
|---|---|---|
| 3 | `setup`, alla steg | Enhets-id `TMBOX-7A42F1` står redan rätt. mDNS-raden `_trainmeet._tcp` måste matcha vad firmwaren faktiskt annonserar — avgör namnet först. |
| 4 | `setup`, steg 6 | `TLS-identiteten sparas` — stämmer inte för v1 (B3: lösenordsfri lokal MQTT). Skriv om till serveridentitet utan TLS, eller markera steget som framtida. |
| 5 | `linefree`, steg 5–6 | Ta bort `server_registered` som enhetssynligt tillstånd. Endast `delivered_to_device` → `display_acknowledged`. |
| 6 | `exceptions`, steg 9 | `state.sync {last_event_id}` utgår. Ersätt med retained snapshot + config + assignment. |
| 7 | Alla steg med klockslag | Förtydliga att `14:32` är träffklockan, inte väggklockan. |
| 8 | `trackchoice` | Lägg till att kollision ger `SPAR 1A UPPTAGET` / `B=ANDRA D=MER` — beläggningsregeln är nu byggd. |

### 24.3 Nya skärmar som saknas

| # | Flöde | Ny skärm |
|---|---|---|
| 9 | `lookup` | Tvetydigt tågnummer: `421 2 TRAFFAR` / `C=NASTA #=VALJ` (§8) |
| 10 | `arrival` | `D=NÄRMAR SIG` som egen TKL-åtgärd före `A=ANKOMMIT` |
| 11 | `departure` / `arrival` | Kvittensskärm efter operativ åtgärd, så `#`-regeln syns i bilden |

### 24.4 Strukturellt

Flödesbildens tre kolumner (Lokal TMBox / Trainmeet / Motstationens TMBox) är
rätt modell och ska behållas. Detsamma gäller växlingen 16/20 tecken och 2/4
rader.

---

## 25. Vad du ska leverera tillbaka

Oförändrat från originalet: sammanfattning, arkitekturbeslut, ändrade filer per
område, migreringar, protokolländringar med exempel, bygg-/flashinstruktion,
serverdrift, testkommandon med faktiska resultat, namnbyteschecklista, och
kvarvarande **verkliga** risker — inte allmänna framtidsidéer.

Bevara enkelheten. TMBox ska kännas som ett fokuserat järnvägsinstrument.
Komplexiteten hör hemma i Trainmeets validerade tillstånd, inte i att TKL måste
förstå nätverk, API:er eller domänobjekt.
