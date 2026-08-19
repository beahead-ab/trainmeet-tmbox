# Spårkatalog: schema v3 (gap-analysens steg 2)

Konkret schemaförslag för spårkatalogen, grundat i faktisk kod i
`trainmeet-cloud` (`cloud/domain.py`, `cloud/store.py`) och
`trainmeet-server` (`src/tambox_gateway/runtime.py`, `models.py`,
`http_server.py`, `operations.py`). Ersätter formuleringen i
[gap-analys.md](gap-analys.md) §3.1 där spår beskrevs som ren fritext — det
var för grovt. Se korrigeringen i §1 nedan.

## 1. Korrigering av gap-analysen: vad som redan finns

Cloud har redan ett halvt steg mot en katalog. `_ensure_operating_point` i
`cloud/domain.py:83` bygger `operating_point["tracks"]` som en deduplicerad,
sorterad lista av spårsträngar, ackumulerad från varje PDF-import
(`apply_extraction`, rad 100–175). Det är **inte** ren fritext utan redan en
uppsättning kända spårnamn per driftplats.

Problemet är att den uppsättningen saknar identitet: inga id:n, ingen
`display_label` skild från namnet, ingen `active`-flagga, ingen
sorteringsordning oberoende av alfabetisk sortering. Och allvarligare:
**servern konsumerar den aldrig.** `http_server.py:1043–1108` (runtime-
installationsvyn) räknar i stället om spårmängden helt oberoende, genom att
skanna `movement.get("track")` över samtliga tågrader. Cloud har alltså en
spårlista och servern har en annan, båda härledda men aldrig jämförda mot
varandra — de kan tyst divergera. `runtime.py`s `RuntimePublication.parse()`
validerar överhuvudtaget inte `operating_point["tracks"]`; fältet passerar
opåverkat i `payload`-dicten.

TKL-lagrets `tkl_movement_states.actual_track` (`operations.py`) är fritext
utan koppling till någotdera.

**Slutsats:** grundarbetet för att samla in spårnamn finns. Det som saknas är
en enda auktoritativ katalog med stabil identitet, som servern faktiskt
validerar mot och som TKL-lagret refererar via id i stället för fritext.

## 2. Designmål

Enligt monsterprompten §9.1/§9.3 och besluten i [beslut.md](beslut.md)
(B5: Charlottendal är den riktiga referensen, B7: dubbelspår i scope):

- Spår är stabila textidentifierare, aldrig heltal.
- En station med flera driftplatser (Charlottendal: C + RBG) har separata
  spårkataloger per driftplats.
- Katalogen är servervaliderad: TKL kan inte skriva ett spår-id som inte
  finns, oavsett väg (TMBox, webb, TKL-terminal).
- Referenskonfigurationerna (både riktiga Charlottendal och den fiktiva
  Cda/Lek/Vst/Kun-trion) ska kunna uttrycka katalogen — Charlottendal via
  AI-import, den fiktiva via manuellt ifylld katalog i en testfixture.

## 3. Schema v3: platt toppnivålista

Rekommendation: lägg till en **platt toppnivålista** `tracks`, parallell med
`stations`/`connections`/`panels`/`trains` i driftpaketet — inte nästlad
under `operating_point["tracks"]`. Skälen:

- `runtime.py`s parser validerar redan alla toppnivålistor enhetligt (unika
  id:n, referensintegritet mot `station_ids`). En nästlad lista skulle kräva
  en egen valideringsväg.
- `SessionConfig` i `models.py` är redan en samling platta `dict[id, Config]`
  — ett `TrackConfig`-dataclass som mönstret för `StationConfig`/
  `ConnectionConfig` är minsta möjliga tillägg.
- Ett spår kan i princip referera en station utan driftplats (enkla
  stationer utan `operating_points`), vilket en nästling under
  `operating_point` inte uttrycker naturligt.

```json
{
  "tracks": [
    {
      "id": "track-cda-1a",
      "display_label": "1A",
      "station_id": "st-cda",
      "operating_point_id": null,
      "active": true,
      "sort_order": 10
    },
    {
      "id": "track-charlottendal-c-3",
      "display_label": "3",
      "station_id": "st-charlottendal",
      "operating_point_id": "op-charlottendal-c",
      "active": true,
      "sort_order": 30
    }
  ]
}
```

Fältregler:

- `id`: stabil sträng, `safe_id("track", f"{station_id}-{operating_point_id or ''}-{display_label}")`
  — samma mönster som `_ensure_operating_point` redan använder för
  driftplats-id:n. Får aldrig återanvändas för ett annat spår.
- `display_label`: det TKL ser (`1A`, `1B`, `7`, `S1`). Fri text men unik
  inom sin station/driftplats.
- `station_id`: obligatorisk, måste finnas i `stations`.
- `operating_point_id`: valfri. Om satt måste den referera en driftplats som
  tillhör `station_id` (samma kontroll som redan finns för
  `trains[].operating_point_id` i `runtime.py:166–173`).
- `active`: `false` döljer spåret för nya val utan att radera historik eller
  bryta gamla referenser. Inaktivering, aldrig hårdradering, av spår som
  redan använts.
- `sort_order`: heltal, styr ordningen i TMBox spårväljare (monsterprompt
  §9.2: "ett spår i taget"). Oberoende av alfabetisk sortering så `1A` kan
  ligga före `10` även om strängsortering säger annat.

## 4. Migrering av tågradernas spårfält

`trains[].track` (fritext, `runtime.py` validerar den inte alls idag) blir
`trains[].track_id`, en referens till katalogen. Regel i
`RuntimePublication.parse()`:

```text
om track_id är satt:
  måste finnas i tracks
  tracks[track_id].station_id måste == train.station_id
  om train.operating_point_id är satt:
    tracks[track_id].operating_point_id måste == train.operating_point_id
```

Detta är **schemalagt spår** (`scheduled_track_id` i monsterprompten §9.3),
inte tilldelat eller faktiskt spår — de senare hör hemma i TKL-lagrets
körtidstillstånd (`operations.py`), inte i det statiska driftpaketet, och
byggs i protokoll v2-kontraktet (steg 3), inte här. Schema v3:s jobb är bara
att ge dem en giltig katalog att peka mot.

Ingen kompatibilitetsperiod med både `track` och `track_id` — README:n för
`trainmeet-server` är uttrycklig om att inget kompatibilitetslager hålls
före första externa release, och `trainmeet-cloud` bygger paketet, så bytet
är en enda samtidig ändring i båda repona.

## 5. Ändringar i trainmeet-server

**`models.py`** — nytt dataclass, samma mönster som `StationConfig`:

```python
@dataclass(frozen=True)
class TrackConfig:
    id: str
    display_label: str
    station_id: str
    operating_point_id: str | None
    active: bool
    sort_order: int
```

`SessionConfig` får `tracks: dict[str, TrackConfig]`.

**`runtime.py`** — `RuntimePublication.parse()`: ny valideringsblock för
`tracks` (unika id:n via befintlig `_unique_ids`-hjälpare, referensintegritet
enligt §4). `session_config()` bygger `tracks`-dicten. Om katalogen saknas
helt för en driftplats som redan har importerade spår i dagens `tracks`-lista
→ `RuntimePublicationError`, inte tyst tomt resultat; annars tappar
spårväljaren data utan varning.

**`http_server.py:1043–1108`** — ta bort den självständiga
spårberäkningen från `movement.get("track")`. Läs `package["tracks"]`
direkt och gruppera per `station_id`/`operating_point_id`. Detta eliminerar
den dolda divergensrisken från §1 helt, inte bara döljer den.

**`operations.py`** — `tkl_movement_states.actual_track` (och kommande
`assigned_track_id`) ska innehålla ett `track_id` från katalogen, inte
fritext. Kolumntypen (TEXT) ändras inte, men skrivvägen
(`update_tkl_movement` och den framtida spårvals-endpointen i protokoll v2)
måste validera mot `SessionConfig.tracks` innan lagring. Lägg till samma
kontroll där TKL-terminalens `POST /v1/tkl/movement` sätter spår idag.

**Ny läsväg för TMBox spårväljaren** (monsterprompt §9.2): antingen ett nytt
`GET /v1/tracks?station_id=...&operating_point_id=...`, eller ett
`tracks`-block i den befintliga panelsnapshoten. Rekommendation: lägg det i
snapshoten (samma leveransmönster som `allowed_keys` och `attention` redan
använder) snarare än en separat REST-resa, eftersom spårväljaren används
mitt i ett pågående MQTT-styrt flöde.

## 6. Ändringar i trainmeet-cloud

**`cloud/domain.py`**:

- `_ensure_operating_point` (rad 83) tilldelar `id`, `active=True` och
  löpande `sort_order` för varje ny sträng i `tracks`-listan i stället för
  att bara lägga strängen i en `set`. Befintliga spår vid omimport matchas
  på `display_label` (normaliserad), inte skapas på nytt.
- `apply_extraction` sätter `train_row["track_id"]` i stället för
  `train_row["track"]` när ett spår identifieras i importen; okänt spår i en
  ny import skapar en ny katalogpost (`active=True`) snarare än att avvisas
  — flödet ska inte kräva att katalogen är komplett innan import.
- `validate_draft` (rad 429): lägg till kontroll att varje tågrads
  `track_id`, om satt, finns i den tillhörande stationens/driftplatsens
  katalog och är `active`.
- `build_runtime_package` (rad 489): serialisera den platta `tracks`-listan
  från stationernas/driftplatsernas interna katalogstruktur.

**Admin-UI**: en editeringsvy för spårkatalogen per station/driftplats i
`All data`-vyn — lägg till, byt `display_label`, inaktivera, ändra
`sort_order` (drag-and-drop eller nummerfält). Inaktivering måste blockeras
om spåret är `track_id` på en oarkiverad tågrad, med tydligt felmeddelande
i stället för tyst dataförlust.

## 7. Referenskonfigurationerna (B5)

- **Charlottendal**: katalogen fylls automatiskt av `apply_extraction` från
  PDF-importen enligt §6 — inget manuellt arbete. Detta blir den första
  riktiga end-to-end-verifieringen av schema v3.
- **Cda/Lek/Vst/Kun (fiktiv)**: ingen PDF att importera från. Katalogen
  (`1A`, `1B`, `2A`, `2B` enligt monsterprompten §5) skrivs för hand som en
  testfixture i `trainmeet-server`s testsvit, med samma schema v3-form som
  produktionsdata. Detta är också den naturliga platsen att testa
  dubbelspårskanalerna (B7) tillsammans med spårkatalogen, eftersom
  referensen explicit har `Lekby linjespår 1/2` och `Vagnsta linjespår 1/2`.

## 8. Explicit utanför scope för schema v3

Dessa hör till protokoll v2-kontraktet (steg 3) eller senare, inte till
katalogschemat:

- `assigned_track_id`, `suggested_track_id`, `actual_track_id` som separata
  körtidsfält (gap-analys §3.2) — kräver katalogen som förutsättning men är
  inte katalogen.
- `track_preferences`-tabellen för spårförslag nästa körning (gap-analys
  §9.4 i monsterprompten).
- Validering av spårbeläggning/konflikt vid tilldelning.

## 9. Rollout-ordning

1. `models.py` + `runtime.py`: `TrackConfig`, parser, `session_config()`.
2. `cloud/domain.py`: katalog-id:n, `track_id` på tågrader, `validate_draft`.
3. Testfixture: Cda/Lek/Vst/Kun-katalogen i `trainmeet-server`s testsvit.
4. `http_server.py`: byt den självständiga spårberäkningen mot katalogläsning.
5. Admin-UI i Cloud för katalogredigering.
6. Snapshot-fältet för TMBox spårväljaren (kan vänta till protokoll v2 om
   webbsimulatorn uppgraderas separat).
7. Full omkörning av Charlottendal-importen som regressionstest.
