# Fullständig ESP8266-backup på Mac och Windows

Hämta **trainmeet-esp8266-backup-1.0.0.zip** under [testutgåvans filer](https://github.com/beahead-ab/trainmeet-tmbox/releases/tag/v0.9.0-wifi-test.1). Packa upp hela mappen.

- **Mac:** dubbelklicka på `Backup-ESP8266-Mac.command`.
- **Windows:** dubbelklicka på `Backup-ESP8266-Windows.cmd`.
- Välj boxens USB-port och backupmapp. Vänta på **KLART** och starta sedan om boxen med RST/RESET.

Python 3.10 eller senare behöver finnas. Första gången hämtas esptool 4.11.0 i en separat användarmapp; därefter behövs normalt inget internet. Ingen administratörsbehörighet behövs. Se [LAS-MIG.txt](LAS-MIG.txt) för hela instruktionen och felsökning.

Verktyget läser **hela den upptäckta flashstorleken** och verifierar hela filen mot boxens minne. Endast ett godkänt försök får en slutlig backupfil och verifieringsmetadata. Det finns inga kommandon för att skriva eller radera flash. Tidigare backuper skrivs inte över.

**Backupfiler kan innehålla wifi-lösenord. Spara dem privat och lägg dem inte på GitHub.** Skriptet laddar inte upp dem. Kopian gäller boxens program och lokala inställningar, inte TrainMeet Servers trafikdata.

Följande skapas för varje lyckad körning:

- `esp8266-full-backup.bin`
- `backup-info.json` med minnesstorlek, tid, MAC om tillgänglig och verifieringsresultat
- `SHA256SUMS.txt`, `flash-info.txt`, `backup.log` och en kort läsanvisning

Skript och startfiler testas på Mac, Windows och Linux med simulerad USB. Fysisk provning på box återstår.
