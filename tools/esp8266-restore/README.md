# Återställ ESP8266 från en verifierad backup

[Hämta återställningsverktyget för Mac och Windows](https://github.com/beahead-ab/trainmeet-tmbox/releases/download/v0.9.0-wifi-test.1/trainmeet-esp8266-restore-1.0.0.zip).

Packa upp hela ZIP-filen och dubbelklicka på **Aterstall-ESP8266-Mac.command** eller **Aterstall-ESP8266-Windows.cmd**. Python 3.10 eller senare krävs.

Välj resultatmappen från backupverktyget och rätt USB-port. Programmet:

1. Kontrollerar att backupen är hel och verifierad, inklusive SHA-256 och full minnesstorlek.
2. Kontrollerar att chip, MAC-adress och minnesstorlek matchar samma fysiska ESP8266.
3. Tar en ny verifierad säkerhetskopia av boxens nuvarande minne.
4. Visar vad som ersätts och kräver att du skriver **ATERSTALL**.
5. Skriver hela originalbackupen och verifierar hela flashminnet igen.

**Hela programmet och de lokala inställningarna ersätts.** Vid avbrott under skrivningen kan boxen behöva ett nytt komplett återställningsförsök. Serverns trafikdata ändras inte. Backupfiler ska hållas privata eftersom de kan innehålla wifi-lösenord.

[Läs hela instruktionen och felsökningen](LAS-MIG.txt). Detta verktyg använder den kompletta backupmappen från [backupverktyget](../esp8266-backup/); det godtar inte lösa firmwarefiler eller backuper från en annan box.

Testerna körs med simulerad USB på Mac, Windows och Linux. Fysiskt boxprov återstår.
