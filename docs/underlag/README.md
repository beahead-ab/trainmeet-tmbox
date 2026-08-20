# TMBox-underlag (2026-08-19)

Detta är rå-underlaget för nästa generations TMBox, sammanställt i en extern
ChatGPT/Codex-konversation och överlämnat för implementation.

- [tmbox-monsterprompt-claude.md](tmbox-monsterprompt-claude.md) — normativ
  implementationsspecifikation: namngivning, arkitektur, protokoll,
  tillståndsmaskiner, tester och definition of done.
- [tmbox-flodesbild.html](tmbox-flodesbild.html) — interaktiv referens med
  exakta 16×2/20×2/16×4/20×4-skärmbilder för varje steg i uppstart, tåguppslag,
  spårval, avgång, ankomst, request/reply och linjen-ledig.

Efterföljande analys- och beslutsdokument, i läsordning:

- [gap-analys.md](gap-analys.md) — djupanalys av monsterprompten mot faktisk
  kod i `trainmeet-tambox`, `trainmeet-server` och `trainmeet-cloud`.
- [beslut.md](beslut.md) — beslutslogg B1–B7. **Normerande** — ersätter
  motsvarande formuleringar i monsterprompten.
- [sparkatalog-schema-v3.md](sparkatalog-schema-v3.md) — schemaförslag för
  spårkatalogen (gap-analysens steg 2).
- [protokoll-v2-kontrakt.md](protokoll-v2-kontrakt.md) — MQTT-topics,
  meddelandekuvert, revisionsregler och tillståndsmaskiner (gap-analysens
  steg 3).

Spåras även som [GitHub-issue #1](https://github.com/beahead-ab/trainmeet-tmbox/issues/1),
med en första lista över öppna frågor och avvikelser mot nuvarande arkitektur.
Se [architecture.md](../architecture.md) för det nuvarande, implementerade
protokollet (MQTT v1) som detta underlag ska vägas mot innan det omsätts i
kod.
