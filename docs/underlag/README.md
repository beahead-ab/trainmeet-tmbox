# TMBox-underlag (2026-08-19)

Detta är rå-underlaget för nästa generations TMBox, sammanställt i en extern
ChatGPT/Codex-konversation och överlämnat för implementation.

Den slutgiltiga, fristående produktbeskrivningen som kom ut av detta arbete
finns i [../tmbox.md](../tmbox.md). Läs den för hur TMBox faktiskt är
specificerad idag; filerna här är underlaget som ledde dit, sorterade efter
hur mycket de fortfarande gäller.

## Gällande referens

- [tmbox-scenarier.html](tmbox-scenarier.html) — interaktiv referens byggd ur
  `lib/tmbox_core`: simulator vars tangenter kör en portning av
  `navigation.cpp` och `renderer.cpp`, flödeskarta över tolv scenarier med
  topic och payload, katalog över alla nitton `Screen`-värden i valfri
  geometri, och dokumentation av vad firmwaren gör — och vad den inte gör.

  Den ersatte `tmbox-flodesbild.html` (2026-08-23). Den gamla filen visade
  `D=MER` på sjutton ställen, men `'D'` förekommer inte en enda gång i
  `navigation.cpp` — tangenten gör ingenting. Den visade också fasta
  kommandosidor och en trafiköversikt med linjerader som aldrig byggdes. Två
  referenser som säger emot varandra är sämre än en, så den togs bort i
  stället för att lämnas kvar bredvid.

## Normerande

- [beslut.md](beslut.md) — beslutslogg B1–B7. Ersätter motsvarande
  formuleringar i monsterprompten.
- [protokoll-v2-kontrakt.md](protokoll-v2-kontrakt.md) — MQTT-topics,
  meddelandekuvert, revisionsregler och tillståndsmaskiner (gap-analysens
  steg 3). Topics stämmer med `../tmbox.md`.
- [sparkatalog-schema-v3.md](sparkatalog-schema-v3.md) — schemaförslag för
  spårkatalogen (gap-analysens steg 2).

## Historik

Filerna nedan bär en banner högst upp som säger att man inte ska implementera
ur dem. De är ersatta av `../tmbox.md` och `beslut.md`, men bevaras för att
visa varför besluten ser ut som de gör.

- [tmbox-monsterprompt-claude.md](tmbox-monsterprompt-claude.md) — den
  ursprungliga implementationsspecifikationen: namngivning, arkitektur,
  protokoll, tillståndsmaskiner, tester och definition of done.
- [tmbox-monsterprompt-v2.md](tmbox-monsterprompt-v2.md) — en andra version av
  samma specifikation.
- [gap-analys.md](gap-analys.md) — djupanalys av monsterprompten mot faktisk
  kod i `trainmeet-tmbox`, `trainmeet-server` och `trainmeet-cloud`.

Spåras även som [GitHub-issue #1](https://github.com/beahead-ab/trainmeet-tmbox/issues/1),
med den ursprungliga listan över öppna frågor och avvikelser mot arkitekturen
vid tidpunkten. Det här underlaget är sedan dess omsatt i kod: se
[../tmbox.md](../tmbox.md) för den färdiga specen och
[architecture.md](../architecture.md) för det äldre MQTT v1-protokollet som
fortfarande betjänar Swift-appen och webbklienten.
