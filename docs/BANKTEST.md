# Bänktest av TMBox v2

Den här checklistan gäller den fastställda referenskonstruktionen i
[TMBOX-V2-HARDWARE.md](TMBOX-V2-HARDWARE.md): ESP32-S3-DevKitC-1-N8R2,
20×4-display på adress `0x27`, passiv 4×4-matris och summer på GPIO10.
Mjukvarutester ersätter inte verifiering av en fysisk prototyp. Firmwaren bör
stå kvar under `1.0.0` tills hela listan är genomförd.

## 1. Installation och uppstart

- [ ] `pio run -e esp32-s3` bygger utan fel.
- [ ] Flashning över det panelmonterade USB-uttaget lyckas.
- [ ] Boxen startar och visar sin identitetskod (`TMBOX-XXXXXX`).
- [ ] Koden på skärmen matchar etiketten under lådan.
- [ ] Kall omstart ger samma kod.

## 2. Elektronik och display

- [ ] Kontrollera kortvariant och alla anslutningar mot pinntabellen innan ström ansluts.
- [ ] 5 V når aldrig ESP32-S3:s GPIO; nivåomvandlaren sitter på I2C-bussen.
- [ ] Displayen svarar på `0x27` och visar 20×4 tecken utan klippning.
- [ ] Kontrast och bakgrundsbelysning fungerar i tänkt mötesmiljö.
- [ ] Skärmbilderna stämmer tecken för tecken mot `golden_frames.txt`.
- [ ] Å, Ä och Ö verifieras med vald teckenuppsättning eller egna CGRAM-tecken.

## 3. Knappsats och lokalt beteende

- [ ] Alla sexton tangenter registrerar rätt tecken.
- [ ] Ingen tangent ger dubbla utslag.
- [ ] Två snabba tryck efter ett skärmbyte ger bara avsett utslag.
- [ ] Navigation, tåguppslag, spårval och klareringsdialoger följer guldfilerna.
- [ ] Långt tryck på provisioneringsknappen rensar Wi-Fi enligt specifikationen.

## 4. Nät och återanslutning

- [ ] Setup-portalen syns när inget nät är konfigurerat.
- [ ] Boxen ansluter till träffens 2,4 GHz-nät.
- [ ] mDNS hittar TrainMeet Server utan manuellt angiven adress.
- [ ] När servern kopplas bort visas `SERVERN BORTA` och boxen försöker igen.
- [ ] När servern återkommer ansluter boxen utan omstart.
- [ ] När routern stängs av visas `NAT SAKNAS` i stället för en frusen vy.
- [ ] Antennens räckvidd provas med vald låda och frontpanel.

## 5. Server, station och trafikflöden

- [ ] `hello` når servern med samma versionsnummer som repo-filen `VERSION`.
- [ ] Boxen upptäcks och kan tilldelas en station i serveradministrationen.
- [ ] Ny konfiguration och nytt snapshot hämtas efter tilldelning och aktivering.
- [ ] Stationsöversikt, bläddring, tåguppslag och spårval fungerar med verklig server.
- [ ] Ett upptaget spår ger `SPAR UPPTAGET`.
- [ ] Två boxar kan genomföra begäran, godkännande, avslag och linjen-ledig.
- [ ] `A` godkänner, `B` nekar och `#` gör inget av dessa.
- [ ] Omstart mitt i ett ärende återställer korrekt läge från serverns snapshot.

## 6. Ljud, ljus och mekanik

- [ ] Summern hörs och tonerna för godkänd, nekad, begäran och nätfel går att skilja åt.
- [ ] Ingen gammal händelse ger ljud efter omstart eller återanslutning.
- [ ] Statuslysdiodens beteende verifieras när funktionen är implementerad.
- [ ] Kort, display och knappsats sitter med mekaniska infästningar utan lim.
- [ ] USB-kabelns kraft tas upp av paneluttaget och når inte utvecklingskortet.
- [ ] Boxen klarar upprepade kabelanslutningar, omstarter och normal hantering.

## Godkännande

Notera prototypens id, firmwareversion, datum, testare och eventuella avvikelser
i ett GitHub-ärende. `1.0.0` kan släppas när en komplett v2-prototyp har klarat
checklistan och kvarvarande avvikelser är antingen åtgärdade eller uttryckligen
accepterade.
