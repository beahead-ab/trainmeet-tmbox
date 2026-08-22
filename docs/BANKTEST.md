# Bänktest av TMBox

Allt i den här listan kräver **en människa och fysisk utrustning**. Ingenting
av det går att avgöra från gröna mjukvarutester, och firmwaren står kvar under
`1.0.0` tills den körts på en riktig låda.

## Externt blockerande beslut

**Bennys svar på hårdvarufrågorna saknas fortfarande.** Frågorna finns
formulerade och märkta med vilka som är blockerande. Utan svaren vet vi inte
kortmodell, knappsatsens GPIO-karta, I2C-adress, kablage eller elektriska
nivåer — och att ladda firmware i en befintlig låda på gissningar riskerar
hårdvaran.

Det här är ett externt beroende, inte en mjukvarubrist, och redovisas som
sådant. Punkterna märkta ⛔ nedan kan inte påbörjas innan svaren kommit.

---

## 1. Installation och uppstart ⛔

- [ ] Firmwaren kompilerar mot den **faktiska** kortmodellen (klassisk ESP32
      eller S3 — Benny 1.1)
- [ ] Flashning lyckas över USB utan att lådan skruvas isär (Benny 6.2)
- [ ] Boxen startar och visar sin identitetskod (`TMBOX-XXXXXX`)
- [ ] Koden på skärmen matchar etiketten under lådan
- [ ] Kall omstart ger samma kod — identiteten är stabil, inte slumpad

## 2. Display ⛔

- [ ] I2C-adressen stämmer (`0x27` eller `0x3F` — Benny 3.2)
- [ ] SDA/SCL sitter på de antagna GPIO (Benny 3.3)
- [ ] **Ingen 5 V på ESP32:ns GPIO** utan nivåomvandlare (Benny 3.4, 3.5)
- [ ] Rätt geometri: text fyller raderna utan att klippas eller radbrytas fel
- [ ] Kontrasten går att ställa (Benny 3.7)
- [ ] Jämför mot `golden_frames.txt` i lådans geometri — rutorna ska stämma
      **tecken för tecken** mot det CI redan verifierar

## 3. Knappsats ⛔

- [ ] Alla sexton tangenter registrerar rätt tecken (Benny 2.1, 2.2)
- [ ] Ingen tangent ger dubbla utslag — studsfiltret räcker
- [ ] `§5`-inmatningslåset: två snabba tryck efter en skärmändring ska ge
      **ett** utslag, inte två
- [ ] **GPIO12-risken**: håll `4`, `5`, `6` eller `B` nere vid strömpåslag.
      Startar kortet ändå? (Benny 2.3)

## 4. Nät och återanslutning

- [ ] Setup-portalen syns när inget nät är konfigurerat
- [ ] Boxen ansluter till träffens nät
- [ ] mDNS hittar servern utan att adressen skrivs in
- [ ] **Dra ur servern**: boxen visar `SERVERN BORTA` och fortsätter försöka
- [ ] **Sätt tillbaka**: boxen återansluter av sig själv, utan omstart
- [ ] Slå av routern helt: boxen visar `NAT SAKNAS`, inte en frusen skärm

## 5. MQTT och stationstilldelning

- [ ] `hello` når servern med rätt `firmware_version` — och det numret ska
      vara samma som `VERSION` i det här repot
- [ ] Boxen dyker upp bland upptäckta enheter i webbadmin
- [ ] Tilldela en station: boxen hämtar config och snapshot
- [ ] `config_version` ändras när en ny version aktiveras → boxen läser om
- [ ] **Aktivera en lokal revision** på servern → boxen läser om den också

## 6. Klareringsflöden

- [ ] Stationsöversikten visar dagens rörelser i tidsordning
- [ ] Bläddring med `C` fungerar genom hela listan
- [ ] Tåguppslag: fyra siffror, träffar visas, bläddring fungerar
- [ ] Spårväljare: valet når servern och syns i webbadmin
- [ ] **Spårbeläggning**: välj ett spår ett annat icke-avgånget tåg står på —
      boxen ska visa `SPAR UPPTAGET`
- [ ] Klareringsbegäran: anslutningsväljaren namnger rätt sträcka
- [ ] Två boxar mot varandra: begäran syns hos mottagaren
- [ ] `A` godkänner, `B` nekar, **`#` gör ingetdera**
- [ ] Linjen-ledig: skickas, tas emot, kvitteras
- [ ] Avslag visas med begriplig orsak och boxen väntar på nästa snapshot

## 7. Summer och GPIO ⛔

Blockerad av **Benny 5.2** — finns plats och en ledig GPIO?

- [ ] `TMBOX_BUZZER_PIN` satt i hårdvaruprofilen
- [ ] Tonerna hörs och skiljer sig åt: begäran hit, svar, linjen ledig,
      servern borta
- [ ] Jämför mot simulatorn under server.trainmeet.app — **samma frekvenser**
- [ ] Det som ska vara **tyst** är tyst: en klarering som redan väntade, första
      ögonblicksbilden efter start, och det man själv nyss gjorde
- [ ] Ljudnivån är rimlig i lokalen när flera boxar piper (Benny 5.3)

## 8. Teckenuppsättning

- [ ] Kan displayen visa Å, Ä, Ö? (Benny 3.6)
- [ ] Om ja: koppla bort translittereringen och verifiera `SPÅR`, `BEGÄR`
- [ ] Om nej: `SPAR`, `BEGAR`, `FORARE` ska vara läsbara och inte klippta

## 9. Långtidstest

- [ ] Boxen kör **en hel träffdag** utan omstart
- [ ] Ingen minnesläcka: `MAX_CACHED_MOVEMENTS` är 96, en stor station kan
      överstiga det — kontrollera vad som händer då
- [ ] Watchdog: framprovocera en hängning och se att boxen startar om
- [ ] Efter omstart: boxen kommer tillbaka till rätt station utan handpåläggning
- [ ] Strömavbrott mitt i en klarering: inget tillstånd blir korrupt

---

## Vad som redan är bevisat utan hårdvara

Det här behöver **inte** provas på bänk — det körs i CI vid varje ändring:

| Vad | Hur |
|---|---|
| Alla skärmar i fyra geometrier | `golden_frames.txt`, 60 rutor |
| Vad varje tangentsekvens gör | `golden_traces.txt`, 12 spår |
| Vad som förtjänar en signal | `golden_attention.txt`, 3 förlopp |
| Att simulatorn ritar samma sak | serverns testsvit faller annars |

Bänktestet ska alltså inte leta buggar i logiken. Det ska svara på en enda
fråga: **stämmer antagandena om hårdvaran?**

## När 1.0.0 är rimligt

När punkterna 1–6 och 9 är avbockade på en riktig låda. Punkt 7 kan lämnas
öppen om Benny svarar att det inte finns någon ledig GPIO — då är summern
struken, inte trasig, och det ska stå så.
