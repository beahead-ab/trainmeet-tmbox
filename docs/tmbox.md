# TMBox

TMBox är en fysisk terminal för tågrörelser och klarering mellan
trafikplatser, byggd för modelljärnvägsträffar. Den är inte ett ställverk.
LocoNet, JMRI, digital ställverkspanel och förseningstjänster ligger utanför
produkten.

TMBox är en tunn, självläkande klient till **TrainMeet Server** — den lokala
auktoriteten under en träff. Servern kan köra på Raspberry Pi, Mac, PC eller
en VPS; TrainMeet Cloud är byggkedja och referensdata, aldrig en runtime-part.
Boxen fattar inga trafikbeslut. Den skickar kompletta, idempotenta kommandon
och renderar verifierade tillstånd från servern.

All tid som visas är **träffklockan**, aldrig väggklockan. Träffklockan kan
gå i annan hastighet och kan stoppas. En skärm som visar `ANKOMMER 14:32`
menar modelltid.

## 1. Namngivning

- Produktnamn i löptext och GUI: **TMBox**
- Teckendisplay: **TMBOX**
- Slug/mappar/paket: **tmbox**
- Enhets-ID: `TMBOX-<hårdvaru-id>`, exempelvis `TMBOX-7A42F1`
- Repository: `trainmeet-tmbox`, konsekvent med resten av repo-familjen
  (`trainmeet-server`, `trainmeet-cloud`, `trainmeet-iphone`, `trainmeet-tkl`)

## 2. Arkitektur

### 2.1 Ansvarsfördelning

TrainMeet Server äger sanningen. TMBox skickar kompletta idempotenta
kommandon och renderar de tillstånd servern skickar tillbaka. En
tangenttryckning som bara flyttar en markör eller skriver en siffra skickas
aldrig till servern — bara en färdig, meningsfull operativ handling gör det.

### 2.2 Transport

MQTT (Mosquitto), QoS 1, retained snapshots, topic-prefix `tmbox/v2/...`.
Retained-mekanismen löser det svåraste synkproblemet av sig själv: en box som
återansluter får ett komplett läge utan att fråga, oavsett i vilken ordning
meddelandena kommer fram.

### 2.3 Enheten tilldelas en station

En box tilldelas **en station**, inte fasta A–D-paneler mot enskilda
sträckor. Trafiköversikten byggs av en topologikonfiguration i enhetens
`config`-data: vilka spår och anslutningar stationen har, och hur de ska
visas.

### 2.4 Boxen är tyst tills den har något komplett att säga

Boxen håller en levande lokal kopia av sin stations data i RAM — dagens
tidtabell och aktuell status — och pratar bara på nätet för att skicka ett
komplett operativt kommando. Ingen rundresa per tangenttryck, ingen rundresa
för att slå upp ett tåg eller bläddra ett spår.

En skarp gräns mellan tre lager gör detta säkert, och den gränsen får
**aldrig blandas ihop**:

1. **Data** (tidtabell, spårkatalog, levande rörelsestatus) — cachas
   aggressivt i RAM, uppdateras automatiskt via retained MQTT-topics. Detta
   är fritt fram, och är precis vad som gör boxen snabb.
2. **Formatering** (teckenbudget, pilgrammatik, `TOMT`-logik, fasta
   kommandosidor) — mekaniska regler, byggda in i firmware. Ändras sällan
   och kräver omflashning när de gör det, men är stabila nog för det.
3. **Affärslogik och ordalydelse** (vilka handlingar som är tillåtna, exakt
   skärmtext, klareringsregler) — **stannar hos servern, alltid**. Skickas
   som data (lager 1), inte som kod. En ändrad regel eller ett ändrat ordval
   når en ansluten box direkt via samma retained config-kanal — ingen
   omflashning, och alla klienter (box, webb, Swift, TKL-terminal) förblir
   synkade eftersom ingen av dem har hårdkodat sin egen version av sanningen.

Det som gör tangentinmatning trögt är aldrig MQTT i sig (10–30 ms på ett
lokalt nät) — det är om varje knapptryck betalar en egen rundresa. Ett
komplett kommando tar bort det. Att hålla stationens snapshot lokalt tar
bort väntan även för uppslag och navigering, eftersom svaret redan finns i
RAM när frågan ställs.

### 2.5 Vad boxen håller i RAM

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
rör stationen.

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

Tåguppslag är en lokal sökning i `movements[]` mot `train_number` — inget
skickas på nätet. Spårväljaren bläddrar `config.tracks[]` lokalt. Vilka
knappar som är giltiga (`A=BEGAR`, `B=EJ` osv.) härleds av firmware ur
`movements[].departure`/`arrival`/`crewReady` med exakt samma mekaniska
regler servern själv använder — reglerna är stabila nog att duplicera
(lager 2), men de faktiska besluten (godkänt/nekat, tilldelat spår) kommer
alltid från servern, aldrig lokalt.

Operativa beslut fattas aldrig lokalt. En knapp som ser tillåten ut enligt
cachead data men vars kommando avvisas av servern (stale revision, spår
upptaget under tiden) visar avslagsorsaken och väntar på nästa snapshot —
den agerar aldrig på den gamla datan igen. Lokal navigation (vald
kommandosida, markörläge, `AVBRYT BEGARAN?`-frågan) är ren UI-state och rör
aldrig detta lager.

### 2.6 Plattform

ESP32-S3 med Arduino-ramverket. Ingen Raspberry Pi krävs i boxen själv.

## 3. Anslutning, installation och identitet

### 3.1 Stabil enhetsidentitet

Ett permanent id härlett ur enhetens efuse-MAC, visat som en kort kod vid
start:

```text
TMBOX-7A42F1
STARTAR...
```

### 3.2 Wi-Fi

Boxen försöker ansluta till senast sparade nätverk. Saknas ett sparat nät,
eller går det inte att nå, öppnas ett tillfälligt setup-nätverk
(`TrainMeet-XXXXXX`) med en captive portal där uppgifterna matas in.
Uppgifterna sparas i beständigt minne. Att hålla `*` i fem sekunder rensar
sparat Wi-Fi och öppnar setup igen efter omstart.

WiFiManager lagrar en profil. Fleranätsstöd (primärt + reserv) är en egen,
senare utökning.

Anslutningen ska tåla verkligheten på en modelljärnvägsträff utan
handpåläggning: en router som startar om, en box som förlorar ström mitt i,
och tidsjämförelser som måste vara wraparound-säkra över `millis()`.
Installationsportalen ska ha en timeout så att den aldrig permanent låser
boxen ute från vanlig anslutning, och boxen ska annonsera sig som offline
(MQTT last will) om den tappar strömmen.

### 3.3 Serverupptäckt

Ordning: senast verifierade endpoint → mDNS → manuell adress. mDNS-tjänsten
heter `tmbox` (`_tmbox._tcp`), och boxen slår upp serverns IP direkt
(`MDNS.IP`) snarare än ett värdnamn — ett mDNS-värdnamn går ofta inte att
slå upp via vanlig DNS på ett isolerat träffnät.

### 3.4 Tilldelning till station

Boxens `hello`-meddelande innehåller enhets-id, hårdvaruversion,
firmwareversion, displaykapacitet och protokollversion. Tills en
administratör kopplar koden till en station i serverns webbadmin visar
displayen:

```text
KOPPLA BOXEN
TMBOX-7A42F1
```

Efter tilldelning:

```text
CDA TILLDELAD
#=BEKRAFTA
```

## 4. Stationskonfiguration och referensdata

Charlottendal (driftplatserna `C` och `RBG`) är den verkliga
integrationsreferensen — data som redan går att importera i TrainMeet Cloud
och som allt ska bevisas fungera mot. En fiktiv fyrstationstopologi
(Cda/Lek/Vst/Kungsfors, dubbelspår åt två håll, spåren `1A`/`1B`/`2A`/`2B`)
används som konstruerad enhetstestfixtur där topologin behöver vara
kontrollerad, till exempel för dubbelspårskanaler.

En befintlig linje utan aktuellt tåg visas `TOMT`. En anslutning som inte
finns lämnas blank.

## 5. Displayregler

Fyra geometrier stöds: 16×2, 20×2, 16×4, 20×4. Logiken är identisk mellan
dem — skillnaden är hur mycket kontext som ryms per skärm.

Varje skärm har en exakt teckenbudget och en fast layout — inga rullande
kommandon. Riktning skrivs med en enkel pilgrammatik: `421>[1A]` för utgående,
`[2A]<428` för inkommande.

Teckenuppsättningen är antingen ASCII-translitterering (`SPAR`, `BEGAR`,
`FORARE`) eller ÅÄÖ via CGRAM på displayer som stödjer det. `hello`
annonserar `display: {rows, cols, charset}` så servern vet vad boxen klarar
och formaterar därefter.

Efter varje skärmbyte spärras inmatning i cirka 500 ms, så ett tryck avsett
för föregående skärm inte råkar tolkas mot den nya.

## 6. Knappmodell

- `0–9` numerisk inmatning
- `A` primär operativ handling (`UPPSTÄLLT`, `FÖRARE`, `BEGÄR`, `KLART`, `AVGÅTT`, `ANKOMMIT`)
- `B` sekundär/negativ (`EJ KLART`, `ÄNDRA`) — etiketten är alltid **`B=EJ`**
- `C` nästa tåg / rad / kommandosida
- `D` mer information / vyväxling
- `*` radera, tillbaka, initiera avbrott
- `#` välj, OK, bekräfta data, kvittera visning

**Säkerhetsregel:** `#` får aldrig lämna ett operativt beslut — `KLART`,
`EJ KLART`, `AVGÅTT` eller `ANKOMMIT` bekräftas alltid via `A`/`B`, aldrig
via `#`. Regeln gäller synkront i motorn och i alla klienter (box,
webbsimulator, Swift, TKL-terminal); ingen klient får implementera ett
undantag på egen hand.

## 7. Tåguppslag

Ett tågnummer kan ha flera rörelser vid samma station samma dag (till
exempel en ankomst och en senare avgång). Servern väljer i första hand via
tidsfönster mot träffklockan. Är det ändå tvetydigt returneras en lista och
boxen bläddrar med `C`:

```text
421 2 TRAFFAR
C=NASTA #=VALJ
```

## 8. Spårmodell och spårväljare

Spår är textidentifierare (`id`, `display_label`, `station_id`,
`operating_point_id`, `active`, `sort_order`) — inte fritext och inte en
uppräkning. `B=ÄNDRA` öppnar en serverstyrd lista, `C` bläddrar, `#` väljer,
`*` lämnar. `A`/`B` används aldrig som bokstavsinmatning.

Fyra spårvärden hålls isär:

- `scheduled_track_id` — tidtabellens spår, skrivs aldrig över
- `suggested_track_id` — serverns förslag
- `assigned_track_id` — TKL:s faktiska val
- `actual_track_id` — verkligt utfall

Ett spår kan bara vara tilldelat en icke-avgången rörelse per station och
dag. En kollision ger:

```text
SPAR 1A UPPTAGET
B=ANDRA D=MER
```

Ett spårbyte under en väntande klareringsbegäran ogiltigförklarar den
begäran.

Serverns spårförslag rangordnas tilldelat → tidtabell → stationsdefault.
Ett förslag är alltid ett förslag, aldrig ett automatiskt beslut.

Historikbaserade förslag mellan träffar (`track_preferences`) utgår. Varje
träff är unik och delar ingenting med en annan, så historik från en tidigare
träff kan inte betyda något för den här. Se beslut D6 i
[trainmeet-server `docs/cloud-server.md`](https://github.com/beahead-ab/trainmeet-server/blob/main/docs/cloud-server.md).

## 9. Avgångsflöde — exempel tåg 421 Cda → Vst

```text
421 [1B] UPPST
A=FORARE D=MER
```

`A=FÖRARE` bekräftar att föraren är på plats. Detta är TKL:s egen
lägeskontroll, registrerad beständigt på servern — en omstart får aldrig
tappa den — och skild från rangerarnas separata `train_readiness`-flöde.
Mellanskärm medan servern väntar på att alla villkor är uppfyllda:

```text
421 [1B] FORARE
VANTAR REDO...
```

När servern har härlett redo (uppställt, förare på plats och regler
uppfyllda):

```text
421 [1B] REDO
A=BEGAR D=MER
```

## 10. Ankomstflöden

`D=NÄRMAR SIG` är en egen TKL-åtgärd som bekräftar observation innan tåget
fysiskt är inne, skild från själva ankomstkvittensen. Ankomst bekräftas
alltid med `A=ANKOMMIT`, aldrig med `#`.

## 11. Klareringslägen

### 11.1 Begäran och svar

Tillstånd: `waiting → approved | rejected | cancelled | expired |
invalidated_by_revision`. Varje begäran har ett stabilt `clearance_id` och
ett `message_id` för idempotens, och en TTL som kontrolleras lat vid varje
request/response — korrektheten beror aldrig på att ett bakgrundsjobb hunnit
köra.

En enkelspårsförbindelse har en delad kanal. En dubbelspårsförbindelse har
en oberoende kanal per riktning, så motriktade rörelser aldrig blockerar
varandra.

### 11.2 Linjen är ledig

Ett ensidigt meddelande, aldrig en fråga. Två tillstånd:
`delivered_to_device → display_acknowledged`. Meddelandet
beläggningskontrolleras aldrig mot en klareringsbegäran — det är inget
beslut, bara information.

## 12. Protokoll

### 12.1 Topics

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

Snapshot-topicet är per **enhet**, inte per panel. `config` skiljer statisk
stationskonfiguration från dynamisk trafikstatus, så de kan uppdateras
oberoende av varandra.

### 12.2 Revision

Tre skilda revisionsutrymmen, inte en enda global räknare:

| Scope | Nyckel | Ökar vid |
|---|---|---|
| `movement` | `movement_id` | uppställt, förare, spårbyte, avgång, ankomst |
| `case` | `clearance_id` / meddelande-id | begär, svara, avbryt, revidera |
| `config` | station | ny driftpaket-aktivering |

Ett kommando anger exakt ett scope. Global revision skulle skala dåligt — en
orelaterad händelse vid en annan station skulle annars ge falska
`stale_revision`-avslag.

### 12.3 Ingen händelseuppspelning

Det finns inget `last_event_id` eller uppspelning av missade händelser.
Retained `assignment` + `config` + `snapshot` vid återanslutning **är** hela
synkmekanismen. Snapshoten är sanningen; händelser är flyktiga.

### 12.4 Kommandokatalog

`device.hello`, `device.presence`, `device.config` (+ `ack`), `train.lookup`,
`train.position.set`, `train.crew_ready.set`, `train.track.change`,
`train.departed`, `train.arrived`, `train.approaching`,
`clearance.request`, `clearance.response`, `clearance.cancel`,
`line.available.publish`, `line.available.acknowledge`.

Synk sker implicit via retained-mekanismen. Kvittens bifogs varje kommando
för command-triggade fall; för ensidiga meddelanden är kvittensen
`display_acknowledged`.

## 13. Lokala tillståndsmaskiner

`state_resync` betyder konkret: vänta på retained `assignment` + `config` +
`snapshot`.

Lokal navigation, urval, kommandosida och inputbuffert hålls strikt
separerade från domäntillståndet. Ett lokalt sidbyte ändrar aldrig
serverstatus.

## 14. Offline, retry och återstart

Fyra tillstånd hålls isär: Wi-Fi saknas / Wi-Fi finns men servern hittas
inte / servern finns men enheten saknar tilldelning / en aktiv anslutning
bröts.

Vid offline visas senaste verifierade status märkt `CACHE` / `INGEN
KONTAKT`. Nya operativa beslut tillåts aldrig, och gamla operativa
tangenttryck köas aldrig upp för senare avsändning.

Cachen ligger i RAM, inte i flash (NVS) — det undviker slitage och risken att
en gammal bild visas som aktuell efter en omstart. Om ett kommando hann
skickas men svaret tappades återanvänds samma `message_id` för att fråga
efter utfallet — aldrig för att skapa ett nytt beslut.

## 15. Robusthet och säkerhet

MQTT körs lösenordsfritt på träffens isolerade nät — ett medvetet val för
den fysiska boxen. Protokollet reserverar ett `device_token`-fält som
ignoreras tills vidare, så TLS och enhetsautentisering kan läggas till som
en egen utökning utan protokollbrott.

I övrigt: hemligheter lagras säkert och loggas aldrig; captive portal stängs
efter lyckad konfiguration; payloads valideras på båda sidor; kommandon är
idempotenta vid retry; optimistic concurrency via revisionsfälten; en
watchdog håller boxen igång; konfigurationsuppdateringar är atomiska med
fallback; en auditlogg med korrelations-id spårar vad som hänt.

## 16. Enhetsstruktur (firmware)

`ConfigStore` håller `config`-topicets payload, `SnapshotStore` håller
`snapshot`-topicets payload — båda ersätts i sin helhet vid varje
mottagning, ingen delta-logik. Övriga komponenter: `DeviceIdentity`,
`WifiManager`, `SetupPortal`, `ServerDiscovery`, `MqttClient`, `KeypadInput`,
`DisplayRenderer`, `LocalNavigationState`, `CommandBuilder`, `Watchdog`.

`AttentionController` (ljud/lampor) kräver GPIO som ännu inte finns
definierat i hårdvaruprofilen och läggs till med graceful degradering när
den hårdvaran finns.

Renderaren ska gå att testa med rena fixtures utan fysisk display — en
native testmiljö kör renderare och tillståndsmaskiner i CI utan hårdvara,
med fixtures för alla fyra geometrier.

## 17. Serverns datamodell

| Begrepp | Beskrivning |
|---|---|
| `devices`, `device_assignments` | Enhetsidentitet och stationstilldelning |
| `stations`, `station_connections` | Topologi mellan trafikplatser |
| `station_tracks` | Spårkatalog per station |
| `movements` | Enskilda tågrörelser (avgång/ankomst) |
| `train_track_assignments` | Tilldelat spår per rörelse |
| `clearances`, `clearance_events` | Klareringsbegäran, svar och historik |
| `line_available_messages` | Linjen-ledig-meddelanden |
| `audit_events` | Auditlogg med korrelations-id |

## 18. Administration

Spårkatalogen redigeras per station/driftplats i TrainMeet Clouds
adminvy, med validering som blockerar inaktivering av ett spår som
fortfarande används av en oarkiverad tågrad. Per-box-konfiguration
(displaygeometri, teckenuppsättning, ljud/lampor) hanteras separat från
själva trafikkonfigurationen.

## 19. Tester

**Firmware:** enhetstester för renderare och tillståndsmaskiner i en native
PlatformIO-miljö, utan fysisk hårdvara, för samtliga fyra displaygeometrier.
Ett fristående hårdvarukontrollprogram bygger och verifierar keypad, LCD och
I2C-adress innan nätverksfirmwaren laddas.

**Server:** idempotens, spår som text, tidtabellsspår som aldrig skrivs
över, spårbeläggning, komplett request/reply, `EJ KLART`, avbrott, timeout,
dubbletthantering, linjen-ledig utan falskt svar, riktade dubbelspårskanaler.

**Integration:** ett namngivet testfall säkerställer att retained
`assignment`, `config` och `snapshot` får anlända i valfri ordning och ändå
ge ett korrekt slutläge — det är den situation en fysisk box faktiskt möter
vid varje omstart och återanslutning.

## 20. Observability

Strukturerad loggning med korrelations-id genom hela kedjan, utan att
hemligheter någonsin loggas.

## 21. Definition of done — första releasen

- Produkten heter TMBox konsekvent; repot heter `trainmeet-tmbox`.
- En fysisk box startar, visar `TMBOX-XXXXXX`, ansluter till Wi-Fi, hittar
  servern via mDNS och tilldelas Charlottendal från adminvyn.
- Boxen kör på lokal nedladdad konfiguration.
- Boxen återhämtar sig från router-omstart, serveromstart och strömbortfall
  utan handpåläggning.
- Avgång 421 Cda → Vst fungerar komplett på fysisk hårdvara, inklusive
  spårval.
- Ankomst 428 Vst → Cda fungerar komplett på fysisk hårdvara.
- Request/reply och linjen-ledig fungerar och blandas aldrig ihop.
- `#` lämnar aldrig ett operativt beslut, i någon klient.
- Ett nätavbrott kan inte skapa ett lokalt falskt `KLART` eller återspela
  ett gammalt operativt tangenttryck.
- Renderaren är testad i CI utan fysisk display, för varje geometri boxen
  stöder.
- Alla tester gröna.
- Dokumentationen beskriver flash, installation, konfiguration, protokoll
  och felsökning.

Bevara enkelheten. TMBox ska kännas som ett fokuserat järnvägsinstrument.
Komplexiteten hör hemma i TrainMeets validerade tillstånd, inte i att TKL
måste förstå nätverk, API:er eller domänobjekt.
