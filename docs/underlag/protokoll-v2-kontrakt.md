# Protokoll v2-kontrakt (gap-analysens steg 3)

Konkret protokollkontrakt för TMBox ↔ TrainMeet Server, grundat i besluten i
[beslut.md](beslut.md) (B1–B7) och spårkatalogen i
[sparkatalog-schema-v3.md](sparkatalog-schema-v3.md). Referenser till
faktisk kod: `trainmeet-server/src/tambox_gateway/{engine,mqtt_adapter,
operations,models,identity}.py`.

## 1. Scope

Detta dokument definierar transport, topics, meddelandekuvert, revisions-
och idempotensregler samt tillstånds­maskiner. Det definierar **inte**
skärmtexter (de finns redan i [tmbox-flodesbild.html](tmbox-flodesbild.html))
och inte implementationsdetaljer i respektive repo — det är nästa steg.

## 2. Arkitekturskifte: enhet → station, inte enhet → panel

**Detta är den enskilt största förändringen i kontraktet.** V1 tilldelar en
enhet en eller flera `panel_id`:n (`identity.py: panels_for_client`), och
varje panel har fasta A–D-slots mot förbindelser (`PanelConfig.slots`,
`models.py`). V2 tilldelar en enhet **en station** direkt (monsterprompt
§4.4: "administratören... tilldelar station, exempelvis cda"), inte en
panelkonfiguration.

Konsekvens för servern: `identity.py` behöver `station_id` i stället för
`panel_ids` i `PairedClient`/tilldelningsflödet. Trafiköversiktsskärmen
(monsterprompt §6.3 — vänster/höger-topologi, linjespår per rad) ersätter
den gamla panelens A–D-idévy, men bygger nu på en **topologikonfiguration**
i `device.config` (vilka grannstationer/linjespår som visas i vilken rad),
inte på fasta förbindelse-slots. `engine.py`s `TrafficEngine`/
`ConnectionRuntime`/`PanelRuntime` är alltså **inte** vidare-vidgade av v2 —
de var byggda för tangent-för-tangent-A–D-protokollet. V2:s motsvarighet är
en ny movement- och clearance-centrerad motor (§7) som konsumerar TKL-lagret
(`operations.py`) direkt.

En station kan ha flera enheter (flera fysiska boxar i samma driftrum); en
enhet har exakt en station.

## 3. Transport och namnrymd

MQTT (B1), samma Mosquitto-stack som idag. Topic-prefixet byts från
`tambox/v1/...` till **`tmbox/v2/...`** — konsekvent med namnbytet i B6.
Ingen bro eller dubbelkörning mellan v1 och v2; de körs på skilda prefix så
en gammal enhet med v1-firmware inte råkar tolka v2-trafik, men det finns
inget krav på att samma broker ska serva bådadera samtidigt i produktion.

| Riktning | Topic | QoS | Retain |
|---|---|---|---|
| box → server | `tmbox/v2/device/{device_id}/hello` | 1 | nej |
| server → box | `tmbox/v2/device/{device_id}/assignment` | 1 | **ja** |
| server → box | `tmbox/v2/device/{device_id}/config` | 1 | ja |
| box → server | `tmbox/v2/device/{device_id}/config/ack` | 1 | nej |
| box → server | `tmbox/v2/device/{device_id}/presence` | 1 | ja |
| box → server | `tmbox/v2/device/{device_id}/command` | 1 | nej |
| server → box | `tmbox/v2/device/{device_id}/ack` | 1 | nej |
| server → box | `tmbox/v2/device/{device_id}/snapshot` | 1 | ja |
| server → alla | `tmbox/v2/gateway/{gateway_id}/status` | 1 | ja |

Ändringar mot v1:
- **En snapshot-topic per enhet**, inte per panel (`.../snapshot/{panel_id}`
  utgår) — eftersom enheten nu är stationsbunden, inte panelbunden (§2).
- **`config`/`config/ack`** är nya — separerar statisk stationskonfiguration
  (topologi, displaygeometri, spårkatalog-utdrag) från dynamisk trafikstatus
  (snapshot). Matchar monsterprompt §4.4/§13 (`device.config.get`,
  `device.config.ack`) och samma utkast/aktivera-mönster som redan finns i
  Cloud→Server-synken (`local_config.py`).
- Retained assignment **och** config ger boxen ett komplett läge direkt vid
  reconnect, innan någon snapshot ens behöver anlända — detta löser
  granskningsfyndet från 2026-08-19 om reconnect-racet (assignment kom före
  snapshot; nu är båda retained och orderberoendet spelar ingen roll).

## 4. Meddelandekuvert

### 4.1 Kommando (box → server)

```json
{
  "protocol_version": 2,
  "message_id": "b3f1...-uuid",
  "device_id": "TMBOX-7A42F1",
  "station_id": "st-cda",
  "action": "clearance.request",
  "expected_revision": {
    "scope": "movement",
    "movement_id": "run-2026-421",
    "value": 3
  },
  "sent_at": "2026-08-19T12:30:00Z",
  "expires_at": "2026-08-19T12:30:05Z",
  "payload": { "...": "..." }
}
```

`expected_revision.scope` ∈ `movement | case | config` (§5). Fältet
`sent_at`/`expires_at` stämplas av gatewayn för klocklösa ESP32-klienter,
exakt som v1 gör idag (`mqtt_adapter.py: use_gateway_clock`) — ingen ändring
där.

### 4.2 Kvittens (server → box, på `.../ack`)

```json
{
  "message_id": "b3f1...-uuid",
  "status": "accepted",
  "reason": null,
  "revision": { "scope": "movement", "movement_id": "run-2026-421", "value": 4 },
  "snapshot": { "...": "aktuell komplett snapshot, samma som topic snapshot" }
}
```

`status` ∈ `accepted | rejected | duplicate` (oförändrat från v1:s
`CommandAck`). Snapshoten bifogas alltid i kvittensen — boxen behöver aldrig
en extra rundresa för att se resultatet av sitt eget kommando (samma
princip som v1:s `ack.snapshots`).

### 4.3 Snapshot / händelse (server → box, retained)

```json
{
  "protocol_version": 2,
  "device_id": "TMBOX-7A42F1",
  "station_id": "st-cda",
  "revisions": {
    "config_version": 12,
    "movements": { "run-2026-421": 4 },
    "cases": { "clr-88f2": 2 }
  },
  "context": { "...": "aktivt uppslag/väntande ärende, se §7" },
  "traffic_overview": { "...": "topologirader enligt device.config, se §2" },
  "interaction": { "allowed_keys": ["A", "B"], "input": { "...": "se §9" } },
  "display": { "line1": "421 [1B] >VST", "line2": "A=UPP B=ANDR C>" },
  "attention": { "active": false },
  "clock": { "time": "14:32", "running": true }
}
```

`revisions` är en karta, inte ett enda tal (§5). `display` är kvar som i v1
för enkla klienter (webb-Tambox-simulering under övergången), men **den
lokala renderaren i firmware konsumerar `context`/`traffic_overview`/
`interaction` direkt** och genererar sin egen `display`-text för aktuell
geometri (16/20 × 2/4) — servern behöver inte längre känna till boxens
displaystorlek för att beräkna radbrytning (det gjorde v1:s `display.py`,
som därmed blir en fallback-renderare för enkla klienter, inte sanningen).

## 5. Revision och idempotens

Enligt B4: revision **per train_run**, med `config_version`/ärende-id som
fallback. Tre skilda revisionsutrymmen, samtliga i samma snapshot (§4.3):

| Scope | Nyckel | Ökar vid |
|---|---|---|
| `movement` | `movement_id` | positioned, förare-status, spårbyte, avgång, ankomst för den rörelsen |
| `case` | `clearance_id` (nytt aggregat, §7.3) eller linjen-ledig-meddelande-id | begär, svara, avbryt, revidera |
| `config` | station | ny driftpaket-aktivering, ny topologi/spårkatalog |

Ett kommando anger **exakt en** scope+nyckel i `expected_revision`. Servern
avvisar med `stale_revision` om värdet inte matchar — precis som
`engine.py: _validate_command` gör idag, men nu per nyckel i stället för
globalt.

Idempotens: `message_id`-cache generaliseras från `engine.py:
processed_commands` till att gälla per enhet, oavsett scope. Ett återskickat
`message_id` (efter tappat ack) ger samma svar utan ny sidoeffekt —
oförändrat mönster, bredare tillämpning.

**Inget event-replay.** Ingen `last_event_id`/`device_event_offsets` (avviker
medvetet från monsterprompt §13/§15 — se gap-analys §3.3). Retained
assignment + config + snapshot vid reconnect är hela synk-mekanismen. Ett
enkelt journal-baserat replay-lager skulle vara ny infrastruktur utan
motsvarande nytta när varje topic redan bär ett komplett läge.

## 6. Kommando-/händelsekatalog

Mappning från monsterprompt §13 till v2-topics och serverlager. `→ NY`
betyder att fältet/lagret inte finns i servern idag och måste läggas till.

| Kommando/händelse | Topic (payload i `action`) | Serverlager | Status |
|---|---|---|---|
| `device.hello` | `.../hello` | `identity.py: record_discovery` | finns |
| `device.presence` | `.../presence` | `identity.py` | finns |
| `device.config.get`/sync | `.../config` (retained push, ej pull) | `identity.py` + `runtime.py: session_config` | → NY (topologi + spårkatalogutdrag) |
| `device.config.ack` | `.../config/ack` | — | → NY |
| `train.lookup` | command `action=train.lookup` | `runtime.py: timetable()` | finns (läsning), → NY (protokollyta) |
| `train.position.set` (uppställt) | command | `operations.py: update_tkl_movement` (`departure=positioned`) | finns, TKL-actor only (B2) |
| `train.crew_ready.set` (förare på plats) | command | → NY kolumn i `tkl_movement_states`, TKL-actor, **separat från** `train_readiness` (B2) | → NY |
| `train.track.change` | command | ny spårvalidering mot `tracks`-katalogen (schema v3) | → NY (körtidsskrivning) |
| `train.departed` | command | `update_tkl_movement` (`departure=departed`) | finns |
| `train.arrived` | command | `update_tkl_movement` (`arrival=arrived`) | finns |
| `clearance.request` | command | → NY clearance-aggregat (§7.3) | → NY |
| `clearance.response` | command (A/B) | → NY | → NY |
| `clearance.cancel` | command | → NY | → NY |
| `clearance.revised` | händelse (server-initierad, spårbyte under väntan) | → NY | → NY |
| `clearance.approved`/`rejected`/`expired` | händelse i snapshot, ej egen topic | → NY | → NY |
| `line.available.publish` | command | `http_server.py: tkl_line_action` — **måste verifieras** (gap-analys §3.6) | delvis, kräver granskning |
| `event.ack` | ej separat — `interaction.mode` i snapshot ersätter kvittens-som-egen-händelse (§4.3) | — | ersatt, ingen egen topic |

`state.sync` (monsterprompt §14.1-övergången) sker implicit: retained
assignment + config + snapshot vid varje (re)connect **är** state.sync. Ingen
separat begäran behövs.

## 7. Nya servermoduler

### 7.1 `tkl_movement_states.crew_ready` (kolumn)

Boolean eller `none|declared`, satt av TKL via `train.crew_ready.set`,
skild från den befintliga `train_readiness`-tabellen (rangerarens flöde,
uttryckligen inte sammanslagen per B2). REDO härleds server-side:
`positioned && crew_ready && rules_ok` (monsterprompt §10.5), exponeras som
`allowed_actions`/`interaction.allowed_keys` i snapshoten.

### 7.2 Directed connection channels (B7)

`ConnectionRuntime` (v1, `models.py`) är en (1) tillståndsyta per
förbindelse. V2 kräver riktade kanaler för dubbelspår (referens: Lekby/
Vagnsta linjespår 1/2 i varsin riktning). Modellera som två oberoende
`ChannelRuntime` per `TrackType.DOUBLE`-förbindelse, en per riktning, i
stället för att vidga den befintliga enkelkanalsmodellen med flaggor.

### 7.3 Clearance-aggregat

Ny tabell/motor, ersätter inte `ConnectionRuntime`/kanalerna (§7.2) utan
lever ovanpå dem: `clearance_id`, `movement_id`, `channel_id`, tillstånd
enligt monsterprompt §12.1 (`draft_local → request_submitting → waiting →
approved/rejected/cancelled/expired/invalidated_by_revision`), `case`-
revision (§5), TTL/`expires_at`, samt invalideringsregel vid spårbyte under
väntande begäran (gap-analys §3.2/§9.5). Detta är den enda delen av
monsterpromptens §18-datamodell som faktiskt ska nybyggas från grunden —
resten är utökningar av befintliga tabeller.

## 8. Tillståndsmaskiner

### 8.1 Anslutning (oförändrad från monsterprompt §14.1)

Ingen ändring mot vad som redan är beslutat; `boot → wifi_connecting →
... → ready → reconnecting → state_resync → ready`, där `state_resync` nu
konkret betyder "vänta på retained assignment + config + snapshot" (§3, §6).

### 8.2 Klarering — request/reply

```text
draft_local (lokalt, ingen nättrafik)
  → request_submitting (clearance.request skickat)
  → waiting (server_registered, case-revision tilldelad)
  → approved | rejected            (mottagarens clearance.response)
  → [invalidated_by_revision]      (spårbyte/omdirigering under waiting)
  → cancelled                      (avsändarens clearance.cancel, kräver "AVBRYT BEGARAN? #=JA")
  → expired                        (TTL utan svar)
```

`#` får aldrig lämna `approved`/`rejected` utan uttrycklig kvittens av
värdet (specens regel, oförändrad). `A`/`B` är de enda operativa besluten.

### 8.3 Linjen är ledig

Ensidigt, tre leveransnivåer separata från operativt beslut (monsterprompt
§12.2): `server_registered → delivered_to_device → display_acknowledged`.
Ingen av dessa är ett `clearance`-tillstånd; modelleras som egen enkel
statusrad, inte som ett skenbart clearance-ärende med bara en part.

## 9. Lokal inmatning (från tidigare protokolldiskussion)

`interaction.input`-blocket (diskuterat innan monsterprompten anlände)
ingår i v2 för responsiv siffer-inmatning utan rundresa per tangent:

```json
"interaction": {
  "allowed_keys": ["0","1","2","3","4","5","6","7","8","9","#","*"],
  "input": { "field": "train_number", "max_length": 5, "submit_key": "#", "clear_key": "*" }
}
```

Boxen ekar lokalt, skickar ett komplett `train.lookup`-kommando vid `#`.
Detta är den enda platsen boxen "vet" något om ett fält innan servern
bekräftat det — och det är just därför bara ett textfält, aldrig ett
operativt beslut.

## 10. Reserverat för säkerhet (B3)

`device_token`-fält reserveras i kommando-/hello-kuvertet men valideras
inte i v1-driften (lösenordsfri lokal MQTT, B3). Ingen bruten kompatibilitet
när TLS + enhetsautentisering läggs till som egen slice — fältet finns
redan, det bara ignoreras till dess.

## 11. Kontraktstestsvit (fake-server)

Enligt monsterprompt §20.2/§20.3, konkretiserat mot detta kontrakt:

- Idempotent `message_id` per scope (§5).
- `stale_revision` per scope testas separat (movement vs. case vs. config
  får inte krocka med varandra).
- Reconnect: retained assignment + config + snapshot anländer i **valfri**
  ordning och ger ändå ett korrekt slutläge (löser 2026-08-19-fyndet
  explicit, ska vara ett namngivet testfall).
- Spårbyte under `waiting` → `invalidated_by_revision`, inte tyst
  omskrivning.
- Directed channel-test: två samtidiga rörelser i motsatt riktning på samma
  dubbelspårsförbindelse ska **inte** blockera varandra.
- Linjen-ledig ger aldrig ett `clearance`-svar från mottagaren.
- Charlottendal (schema v3, verklig import) + fiktiv Cda/Lek/Vst/Kun
  (handskriven fixture) körs som två separata publikationer genom hela
  svaret.

## 12. Explicit öppna punkter till implementationsfasen

- Exakt fältnamn/typ för `crew_ready` (§7.1) — boolean vs. tidsstämplad
  status; avgörs när `operations.py` faktiskt ändras.
- `tkl_line_action` (§6, `line.available.publish`) måste läsas i sin helhet
  och jämföras mot §8.3 innan protokollet låses — flaggat men inte verifierat
  här.
- Multipla enheter per station (§2): ägarskap av en pågående lokal
  inmatning när två boxar delar station — `owner_client_id`-mönstret från
  v1 (`PanelRuntime`) återanvänds sannolikt per movement/case i stället för
  per panel, men är inte specificerat i detalj här.
