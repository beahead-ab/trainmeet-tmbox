# ESP32 och ESP32-S3: backup och återställning på Mac och Windows

[Hämta hela verktygspaketet](https://github.com/beahead-ab/trainmeet-tmbox/releases/download/v0.9.0-wifi-test.1/trainmeet-esp32-backup-restore-1.0.0.zip). Packa upp hela mappen.

| Funktion | Mac | Windows |
|---|---|---|
| Fullständig backup | `Backup-ESP32-Mac.command` | `Backup-ESP32-Windows.cmd` |
| Fullständig återställning | `Aterstall-ESP32-Mac.command` | `Aterstall-ESP32-Windows.cmd` |

Dubbelklicka, välj USB-port och följ frågorna. Python 3.10 eller senare krävs. Första gången hämtas esptool 4.11.0 i en separat användarmapp.

Klassisk ESP32 och ESP32-S3 identifieras automatiskt. Hela det upptäckta flashminnet (1–16 MB) kopieras och verifieras; ingen minnesstorlek gissas. Backupen innehåller firmware och lokala inställningar, inklusive wifi-uppgifter.

Återställning kräver en verifierad backupmapp, samma chipmodell, samma MAC-adress och samma fulla minnesstorlek. En ny verifierad säkerhetskopia tas före bekräftelsen **ATERSTALL**. Efter skrivningen verifieras hela minnet igen. **Hela boxens program och lokala inställningar ersätts.**

Aktiv flashkryptering, Secure Boot, Secure Download Mode eller okänd säkerhetsstatus stoppar verktyget. Inga eFuses ändras, inga skydd kringgås och inga backupdata laddas upp. ESP8266 och andra ESP32-varianter som C3/S2 stöds inte av detta paket.

[Läs hela instruktionen](LAS-MIG.txt). Spara backupmappar privat eftersom de kan innehålla lösenord. Resultaten sparas normalt i `TMBox-backuper-ESP32` i hemmappen.

Testerna använder simulerad USB/flash på Mac, Windows och Linux och de riktiga ROM-definitionerna från esptool. Fysisk provning återstår.
