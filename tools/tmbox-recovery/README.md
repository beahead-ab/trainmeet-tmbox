# Gemensam meny för TMBox-backup och återställning

[Hämta hela paketet för Mac och Windows](https://github.com/beahead-ab/trainmeet-tmbox/releases/download/v0.9.0-wifi-test.1/trainmeet-tmbox-backup-restore-1.0.0.zip).

1. Packa upp hela ZIP-filen i en ny mapp.
2. **Mac:** dubbelklicka på `Starta-TMBox-Mac.command`.
3. **Windows/PC:** dubbelklicka på `Starta-TMBox-Windows.cmd`.
4. Välj backup eller återställning för ESP8266 respektive ESP32/ESP32-S3.

Python 3.10 eller senare krävs. Första gången hämtar respektive verktyg esptool i en egen användarmapp. Inga kommandon behöver skrivas.

| Val | Funktion |
|---|---|
| 1 | Backup av ESP8266 / NodeMCU |
| 2 | Återställ ESP8266 / NodeMCU |
| 3 | Backup av ESP32 / ESP32-S3 |
| 4 | Återställ ESP32 / ESP32-S3 |
| 0 | Avsluta |

[Läs hela instruktionen och felsökningen](LAS-MIG.txt).

Menyn kontrollerar att paketet är komplett före start och varje åtgärd. Bakom menyn används de redan publicerade verktygen **oförändrade**, hämtade från checksumme- och revisionslåsta ZIP-filer. De ursprungliga startfilerna och detaljerade instruktionerna finns också i undermapparna.

**Återställning ersätter hela boxens program och lokala inställningar.** Den kräver en verifierad backup, rätt fysisk box och bekräftelsen **ATERSTALL**. En ny verifierad säkerhetskopia tas före skrivningen och hela minnet verifieras efteråt. Menyn kringgår inga skydd.

Spara backupmappar privat eftersom de kan innehålla lösenord. Inga backupdata laddas upp. Fysiskt boxprov återstår; menyn, paketet och verktygen testas med simulerad USB på Mac, Windows och Linux.
