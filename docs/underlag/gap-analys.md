# TMBox djupanalys: monsterprompten mot verkligheten (2026-08-19)

Analysen ställer [tmbox-monsterprompt-claude.md](tmbox-monsterprompt-claude.md)
och [tmbox-flodesbild.html](tmbox-flodesbild.html) mot faktisk kod i tre repon:

| Repo | Innehåll som granskats |
|---|---|
| `trainmeet-tambox` | ESP32-firmware, hårdvaruprofiler, MQTT v1-klient |
| `trainmeet-server` | Trafikmotor, TKL-lager, runtime schema 2, identitetsregister, MQTT-gateway, klocka, webbadmin |
| `trainmeet-cloud` | AI-import, kanoniska stationer/driftplatser, topologi, paneler, schema v2-publiceringar |

## 1. Huvudslutsats

**Monsterprompten är skriven utan kännedom om hur mycket som redan finns.**
Den beställer flera subsystem som existerar i drift under andra namn, och
missar att den verkliga uppgiften inte är att bygga nytt utan att **förena två
parallella operativa lager som servern redan har**:

1. **Tambox-panelmotorn** (`engine.py`): förbindelsecentrerad, A–D-slots,
   tangent-för-tangent-protokoll, servern renderar exakta 16×2-rader.
2. **TKL-lagret** (`operations.py`): tågcentrerat, med skift, driftplatser,
   `departure_status` (`none → positioned → ready → departed`),
   `arrival_status`, `actual_track`, samt "Tåg klart"-överlämning med roller
   (rangerare/TKL: `ready → acknowledge → revoke`).

Monsterprompten beskriver i praktiken att den fysiska boxen ska få
**TKL-lagrets tågcentrerade flöde**. Det är rätt riktning — men beställningen
till servern blir då "exponera och komplettera det som finns", inte "bygg
tabellerna i §18 från noll". Att bygga §18 som specen skriver den skulle skapa
exakt den parallella domänmodell som specens §1 själv förbjuder.

## 2. Vad som redan finns (mappning spec → verklighet)

| Spec-krav | Finns idag | Var | Status |
|---|---|---|---|
| Tidtabell, tågrader per station/dag | Ja | `runtime.py` schema 2, `GET /v1/timetable`, AI-import i Cloud | Klart — grund för `train.lookup` |
| `train_run_id` | Ja, som `movement_id` | `tkl_movement_states` | Återanvänd, döp inte om |
| `positioned`/`departed`/`arrived` | Ja | `departure_status`, `arrival_status` i TKL-lagret | Exponeras ej till boxar ännu |
| `crew_ready` | Delvis | `train_readiness` (rangerare färdigställer, TKL kvitterar, med roller och historik) | **Rikare än specen** — se lucka 6 |
| `actual_track` | Ja | `tkl_movement_states.actual_track` | Fritext, ej validerad mot katalog |
| Tidtabellsspår | Ja | `track`-fält per tågrad från PDF-importen | Fritext |
| Idempotens, dubbletter, revision, expiry | Ja | `processed_commands`, `expected_revision`, `expires_at`, atomisk persist/rollback, SQLite-restore | Klart för kommandon; saknas för klareringsärenden |
| Klockstämpling för klocklösa boxar | Ja | Gatewayn sätter `sent_at`/`expires_at` för `ESP32_PANEL` | Specens `sent_at`-krav på enheten är redan löst serverside |
| Träffklocka med hastighet/stopp | Ja | `start_clock`/`stop_clock`/`set_speed`, `POST /v1/clock` | **Specen nämner den inte alls** — se lucka 3 |
| Enhetsregister, upptäckt, tilldelning | Ja | `identity.py`: `record_discovery`, `assign_discovered_device`, admin-UI | Saknar per-box displaygeometri/funktioner |
| Konfigurationsversionering + distribution | Ja | Cloud → Server: oföränderliga schema v2-paket, sexsiffrig koppling, auto-synk, utkast/aktivera | Mönstret saknas bara i sista hoppet Server → box |
| Retained snapshot + assignment + resync vid presence | Ja | `mqtt_adapter.py` (assignment publiceras retained — obs: `docs/architecture.md` i tambox-repot påstår motsatsen, dokumentfel) | Fungerar |
| Charlottendal som referens | Ja | Cloud: en station med driftplatserna `C` och `RBG`; servern hanterar `operating_points` | **Specens Lekby/Vagnsta/Kungsfors är fiktion** — se lucka 11 |
| Simulerad TMBox | Ja | Serverns webb-Tambox-simulering | Snabbaste vägen att iterera protokoll v2 |
| Rate limiting-grund, auditlogg | Delvis | `engine.audit`, TKL-events i SQLite | Behöver enhetligt korrelations-id |

## 3. Verkliga luckor på servern — beställningen till trainmeet-server

Detta är vad trainmeet-server behöver tillhandahålla som inte finns idag:

### 3.1 Spårkatalog per station/driftplats (störst enskild lucka)
Spår är idag **fritext** ur tidtabellsimporten, aggregerad till mängder för
visning (`http_server.py`). Spårväljaren i specen kräver en förstklassig
entitet: `{id, display_label, station_id, operating_point_id?, active,
sort_order}`. Kräver schema v3 tillsammans med Cloud (se §4). Servern ska
validera `actual_track`/tilldelat spår mot katalogen.

### 3.2 Spårfältseparation + förslag
`scheduled_track` (finns som tidtabellens `track`) och `actual_track` (finns)
ska kompletteras med `assigned_track_id` och `suggested_track_id` samt
tabellen `track_preferences` nycklad på trafikmönster (movement-identitet, ej
bara synligt tågnummer). Förslagsordningen i specens §9.4 är bra som den är.

### 3.3 Tågcentrerat boxprotokoll (protokoll v2) — kärnbeställningen
Exponera TKL/movement-lagret över MQTT för boxklienter:

- `train.lookup` → slår mot tidtabell + movements; svarar med movement_id,
  riktning, motstation, spårfält, `allowed_actions`, revision.
- `train.position.set`, readiness-händelser → mappa till befintliga
  `tkl_movement_states`/`train_readiness` med `actor_role`-utvidgning
  (`source=tambox`).
- `clearance.request/response/cancel` → se 3.4.
- `train.departed`/`train.arrived` → befintliga statusövergångar.
- Kompletta idempotenta kommandon ersätter `key_press`-protokollet.
- Rekommendation: **stryk** specens `last_event_id`/`device_event_offsets`/
  event-replay. Retained snapshot + resync vid presence är dagens fungerande
  modell och räcker; ett event-journal-subsystem är stor kostnad utan
  motsvarande nytta. Snapshot är sanningen, händelser är flyktiga.

### 3.4 Klareringsärenden som egna aggregat
Dagens `ConnectionRuntime` är en (1) tillståndsyta per förbindelse utan
historik, utan TTL-timer, utan revidering. Behövs: `clearances`-tabell med
`clearance_id`, tillståndsmaskinen i specens §12.1, utgångstid, invalidering
vid spårbyte, och koppling till movement. Detta är ny motorlogik — den enda
delen av specens §18 som faktiskt ska nybyggas.

### 3.5 Dubbelspår som riktade kanaler
`TrackType.DOUBLE` finns i konfigurationen men motorn har en kanal per
förbindelse. Specens referens (Lekby/Vagnsta dubbelspår, linjespår 1–2 per
rad) kräver riktade kanaler. Står redan som "next slice" i architecture.md —
måste nu in i scopet eftersom referenskonfigurationen förutsätter det.

### 3.6 Linjen-ledig-meddelanden
`POST /v1/tkl/line` finns i TKL-API:t — semantiken måste verifieras och
harmoniseras med specens §12.2 (trenivåstatus `server_registered` /
`delivered_to_device` / `display_acknowledged`). Definiera också vad som
frigör linjestatus i detta läge (avsändarens `departed`+`arrived`, inte
mottagarens kvittens).

### 3.7 Per-boxkonfiguration
Enhetsregistret saknar displaygeometri (16/20 × 2/4), teckentabell,
ljud/lampa-beteende och funktionsflaggor per box. Läggs i `identity.py`-lagret
+ admin-UI, distribueras via nya `device.config`-meddelanden (samma
utkast/aktivera-mönster som Cloud→Server).

### 3.8 Attention-signal till boxar
Riktad väckning (ljud/lampa) vid inkommande ärende. Enklast som fält i
snapshoten (`attention`-blocket finns redan i panelsnapshoten — generalisera
till protokoll v2), ingen separat kanal behövs.

## 4. Beställningen till trainmeet-cloud

1. **Spårkatalog i schema v3**: redigering av stationens/driftplatsens spår
   (id, etikett, ordning, aktiv) i All data-vyn; validering att importerade
   tidtabellsrader refererar katalogspår; publicering i driftpaketet.
   Läget är gynnsamt: README:n säger uttryckligen att inga
   kompatibilitetslager hålls före första externa release — schemautbyggnad
   är billig **nu** och dyr sen.
2. **Klareringsläge per riktning/relation** om §12 kräver finare styrning än
   dagens per-sträcka-läge (verifiera behovet innan det byggs).
3. **Inget mer.** Boxarnas hårdvarunära inställningar (geometri, ljud) hör
   hemma lokalt på servern, inte i Cloud. Firmware-release/OTA-kanal är en
   möjlig framtida cloud-tjänst men behövs inte för att kedjan ska fungera —
   boxen rapporterar redan firmware-version, flashning sker via USB i v1.

## 5. Konsekvenser för trainmeet-tambox (firmware)

- **Renderingen flyttar in i boxen.** Idag renderar servern exakta rader;
  specen kräver lokal renderare + lokal inmatning + kommandosidor. Detta är
  det stora arkitekturskiftet, och det gäller **alla klienter**: webbsimulering,
  Swift och TKL delar idag serverns rendering och `#=Ja`-grammatik.
- **`#`-regeln bryter dagens UX i hela klientfloran.** Motorn använder `#`
  för att bekräfta avgång (`CONFIRM_DEPARTURE`). Specens regel (operativa
  beslut endast på A/B) är en förbättring men måste rullas ut synkront i
  motor, webb, Swift och TKL — annars får olika klienter olika
  säkerhetsgrammatik. Detta är en dold men stor del av beställningen.
- **Anslutningslagret överlever** (identitet, portal, mDNS, MQTT, backoff) —
  granskningsfynden från 2026-08-19 (mDNS-IP, portal-fällan, LWT, wraparound)
  ska fixas och behållas. Notera att fynd 1 (reconnect-racet) mildras av att
  servern faktiskt retainar assignment; fixen "behåll tilldelning över
  reconnect" är ändå rätt.
- **Host-native testmiljö** behövs (PlatformIO native env) så renderaren och
  tillståndsmaskinerna testas i CI utan hårdvara, med fixtures för alla fyra
  displaygeometrier. CI ska även bygga `diagnostics/hardware-check`.
- ESP32-S3-profilen finns redan; LED/ljud-pinnar saknas i
  `hardware_profile.h` och ska in med graceful degradering.

## 6. Luckor i specen och föreslagna täppningar

1. **Transport**: behåll MQTT. Mappa specens 19 kommandon/händelser till
   `tambox/v2/...`-topics i ett kontraktsdokument. HTTP/WS-modellen förkastas
   — specen tillåter det själv ("återanvänd befintligt transportlager").
2. **Pi-formuleringen**: "En Raspberry Pi ska inte behövas" är redan sant —
   servern kör på Mac/PC/VPS/Pi. Förtydliga att boxen alltid talar med den
   **lokala** TrainMeet Servern; Cloud är byggkedja, aldrig runtime-part.
3. **Träffklockan saknas helt i specen.** Alla tider på skärmarna
   ("ANKOMMER 14:32") ska vara träffklockan (som kan gå i annan hastighet och
   stoppas), aldrig väggklocka. Boxen behöver ingen egen klocka — servern
   stämplar redan tid för ESP32-klienter, och klocktid ingår i snapshoten.
4. **Readiness-kollision**: specens `A=FÖRARE` (crew_ready) överlappar
   befintlig `train_readiness` där *rangeraren* färdigställer och *TKL*
   kvitterar. Besluta EN modell. Rekommendation: behåll serverns
   rollmodell; `A=FÖRARE` på boxen blir TKL:s kvittens/registrering i samma
   flöde (`action=ready, actor_role=tkl` alt. `acknowledge`), inte en ny
   parallell status.
5. **Ambiguitet i tåguppslag**: samma tågnummer kan ha flera rörelser vid
   stationen samma dag (ankomst + senare avgång). Servern väljer via
   tidsfönster mot träffklockan; vid tvetydighet returneras en lista och
   boxen bläddrar med `C` (`1/2`). Skärm saknas i flödesbilden — lägg till.
6. **Preemption och felmanöverskydd**: servern har redan principen "ett svar
   avbryter aldrig pågående inmatning". Specen ska ärva den explicit:
   inkommande ärende auto-visas bara från vilo-/översiktsläge, annars
   lampa/ljud + kö. Lägg också till inmatningsspärr ~500 ms efter varje
   skärmbyte så ett tryck avsett för gamla skärmen inte tolkas mot nya.
7. **10.4-dubbletten**: skärmen efter `A=FÖRARE` är identisk med skärmen
   före. Lägg in mellanskärm (`421 [1B] FORARE / VANTAR REDO...`).
8. **`B=EJ` vs `B=NEJ`**: två undantagsskärmar avviker. En etikett.
9. **Säkerhetsmodell v1**: specens TLS + tokens står mot hela stackens
   medvetna, dokumenterade beslut om lösenordsfri MQTT på isolerat träffnät.
   Rekommendation: v1 behåller dagens modell (uttalat, inte av slarv);
   protokoll v2 reserverar token-fält; TLS blir egen senare slice. Specen
   får inte tyst höja ribban som servern medvetet lagt.
10. **Revisionsscope**: motorn har global revision; specen vill ha per
    train_run. Besluta i kontraktet: rekommendation revision per
    station/ärende + separat `config_version`, globala revisioner skalar inte
    när flera stationer arbetar parallellt.
11. **Referenskonfigurationen**: byt specens fiktiva Lekby/Vagnsta/Kungsfors
    mot det riktiga Charlottendal-paketet (C + RBG) som integrationsfixture.
    Den fiktiva trion kan behållas som syntetisk enhetstestkonfiguration, men
    referensflödena ska gå mot data som Cloud faktiskt kan publicera.
12. **mDNS-tjänstenamn**: firmware söker `_tambox._tcp`, specen säger
    `_trainmeet._tcp`. Verifiera vad serverinstallationen faktiskt annonserar
    och standardisera; lägg `server_id` i TXT-posten för identitetskontrollen.
13. **Wi-Fi-reservnät**: WiFiManager lagrar en profil. V1 behåller en;
    fleranätsstöd är egen liten slice om träffarna kräver det.
14. **Offlinecache**: RAM-cache + `INGEN KONTAKT` räcker; skriv inte
    snapshots till NVS (flash-slitage, inaktualitetsrisk efter omstart).
15. **Namnbytet**: gör det tidigt (noll driftsatta boxar = ingen migration),
    men föreslå repo-namnet **`trainmeet-tmbox`** i stället för specens
    `tmbox`, så repo-familjen förblir konsekvent (`trainmeet-server`,
    `trainmeet-cloud`, `trainmeet-iphone`, `trainmeet-tkl`).
16. **Dokumentfel att rätta**: tambox-repots `architecture.md` säger "endast
    snapshots och status retained" — servern retainar även assignment.
    Uppdatera dokumentet när protokoll v2-kontraktet skrivs.

## 7. Något annat

- **Protokollkontraktet behöver ett hem.** Lägg `docs/protocol/v2/` i
  trainmeet-server-repot (JSON-scheman + topictabell + exempel) och låt
  tambox-repot konsumera en versionerad kopia. Kontraktet är den enda
  artefakt som håller ihop fyra klientrepon — idag finns det ingenstans.
- **Simulatorn först.** Uppgradera serverns webb-Tambox-simulering till
  protokoll v2 innan firmware skrivs om. Alla flöden i flödesbilden kan då
  verifieras mot riktig motor utan hårdvara, och golden-master-testerna
  flyttar från 16×2-textrader till protokollnivå + renderarfixtures.
- **Rekommenderad ordning** (ersätter specens §22):
  1. Beslut: punkterna 1, 4, 9, 10, 11, 15 i §6 ovan.
  2. Schema v3 med spårkatalog (Cloud + Server) — dyrast att vänta med.
  3. Protokoll v2-kontrakt som dokument + fake-server/testsvit.
  4. Servermotor: movement-exponering, clearances-aggregat, spårfält,
     dubbelriktade kanaler.
  5. Webb-simulatorn till v2 (verifiera alla flödesskärmar).
  6. Firmware v2 (rendering, inmatning, tillståndsmaskiner) + host-tester.
  7. Swift/TKL-klienterna till v2 (`#`-regeln synkront).
  8. Härdningsslices: linjen-ledig, spårförslag/historik, TLS, OTA,
     Wi-Fi-reservnät.
- **Uppskattad tyngdpunkt**: ~55 % server, ~25 % firmware, ~10 % cloud,
  ~10 % kontrakt/test/dokumentation. Firmware är alltså inte huvuddelen —
  planera bemanningen därefter.
