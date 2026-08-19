# Beslutslogg: gap-analysens steg 1 (2026-08-19)

Besluten nedan avgör de öppna punkterna i [gap-analys.md](gap-analys.md) §6
(punkterna 1, 4, 9, 10, 11, 15 samt dubbelspårsfrågan från §3.5). De är
normerande för protokoll v2-kontraktet och ersätter motsvarande formuleringar
i [monsterprompten](tmbox-monsterprompt-claude.md).

## B1. Transport: MQTT v2-topics

Protokoll v2 körs över befintlig Mosquitto-stack (QoS 1, retained snapshots).
Specens kommandon/händelser mappas till `tambox/v2/...`-topics i
kontraktsdokumentet. Specens HTTP/WebSocket-förslag utgår.

## B2. TMBox-flödet för uppställt/förare — rangerare separeras

TKL ska via TMBox kunna deklarera **tåg uppställt** och **lokförare på
plats**, och därefter fortsätta med de befintliga klareringsstatusarna. De två
första statusarna är primärt TKL:s egen lägeskontroll (lokal bokföring inför
klarering).

**Rangerarna får en helt egen framtida panel med helt egna kommandon.**
Serverns befintliga `train_readiness`-flöde (rangerare färdigställer, TKL
kvitterar) hålls därför separat och byggs inte in i TMBox v2.

Konsekvenser:

- TMBox-kommandona skrivs som TKL-aktör mot `tkl_movement_states`
  (`positioned`, förare-status, vidare till klarering); ingen sammanslagning
  med rangerarflödet.
- Specens §10.4 kompletteras med en mellanskärm efter `A=FÖRARE` (identisk
  före/efter-skärm var ett spec-fel).
- Härledd `REDO` (specens §10.5) beräknas ur TKL:s två deklarationer plus
  serverregler, precis som specen anger.

## B3. Säkerhetsmodell v1: lösenordsfri lokal MQTT

Dagens dokumenterade modell för träffens isolerade nät behålls uttalat.
Protokoll v2-schemat reserverar token-fält så TLS + enhetsautentisering kan
läggas till som egen senare slice utan protokollbrott. Specens §16 nedgraderas
på denna punkt från krav till framtida slice.

## B4. Revisionsscope: per train_run (specens modell)

Monotont ökande revision per tågrörelse (`movement_id` är nyckeln), enligt
specens §13/§18. Kontraktet måste komplettera med revisionskälla för
kommandon som inte är bundna till en rörelse (linjen-ledig, spårkatalog,
stationsnivå): dessa villkoras mot `config_version` respektive ärende-id i
stället. Motorns nuvarande globala revision fasas ut ur boxprotokollet.

## B5. Referenskonfiguration: Charlottendal + fiktiv

Riktiga Charlottendal-paketet (driftplatserna `C` + `RBG`, finns i Cloud)
blir integrationsfixture; alla referensflöden ska gå mot data som Cloud
faktiskt kan publicera. Specens fiktiva Lekby/Vagnsta/Kungsfors behålls som
syntetisk enhetstestkonfiguration där topologin behöver vara konstruerad
(bl.a. tre grannar och dubbelspår).

## B6. Namnbyte: `trainmeet-tmbox`, tidigt

Produktnamn **TMBox**, slug `tmbox`, enhets-id `TMBOX-<stabilt hårdvaru-id>`.
Repot döps om till **`trainmeet-tmbox`** (avvikelse från specens `tmbox`) så
repo-familjen förblir konsekvent. Görs tidigt — noll driftsatta boxar betyder
ingen migrationskostnad; specens kompatibilitetslager för gamla id:n utgår.
GitHub-rename ger automatisk redirect från gamla URL:er; README-länkar i
`trainmeet-server` m.fl. uppdateras i samband med bytet.

## B7. Dubbelspår: riktade kanaler i scopet nu

Motor och protokoll byggs med riktade kanaler per förbindelse från start,
så protokollet aldrig låses i enkelspårsantaganden. (Krävs även av den
fiktiva testkonfigurationen enligt B5.)

## Nästa steg

Enligt [gap-analys.md](gap-analys.md) §7: steg 2 — schema v3 med spårkatalog
(Cloud + Server), därefter steg 3 — protokoll v2-kontraktet som dokument i
`trainmeet-server` (`docs/protocol/v2/`) med topictabell enligt B1 och
revisionsregler enligt B4.
