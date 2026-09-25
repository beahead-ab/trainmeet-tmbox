# Installera TrainMeet TMBox

En guide för dig som inte programmerar. Samma TrainMeet Server styr både
NodeMCU/ESP8266 och ESP32-S3. Boxarna visar information och skickar dina val;
de fattar inte egna trafikbeslut. Servern kan vara Raspberry Pi, Mac, PC eller
Linux. TrainMeet Cloud behövs inte under träffens drift.

**Status: förhandsversion.** Programmen byggs och mjukvarutestas automatiskt,
men de här hårdvaruprofilerna är ännu inte fysiskt bänktestade. Du kan läsa
hela guiden nu utan ansluten box. Markera inte en installation eller provkörning
som klar förrän den faktiskt är gjord.

## Innan du börjar

Du behöver:

- En av de två boxmodellerna nedan, inkopplad enligt sin anvisning.
- En USB-kabel för **data**, inte bara laddning.
- En Windows-, Mac- eller Linux-dator med Chrome eller Edge för USB-installationen.
- Wi-Fi på **2,4 GHz**, nätets lösenord och gärna en telefon för Wi-Fi-inställningen.
- TrainMeet Server med en aktiv träff på samma lokala nätverk.

Webbguiden finns i repots `installer/`. Ett komplett byggpaket innehåller
både guiden och färdig firmware; användaren behöver inte kompilera någonting.
En guide utan firmwarefiler visar detta tydligt och spärrar installationen.
**En offentlig installationsadress är ännu inte aktiverad.** Använd inte en
antagen `install.trainmeet.app`-adress innan den har publicerats.

## 1. Välj rätt box

| Välj i guiden | Exakt profil | Display | Knappsats |
|---|---|---|---|
| NodeMCU · ESP8266 | NodeMCU 1.0, ESP-12E/ESP-12F, 4 MB | 16×2, I²C `0x27` | PCF8574 `0x20`, kolumner P0–P3, rader P4–P7 |
| TMBox · ESP32-S3 | ESP32-S3-DevKitC-1-N8R2 | 20×4, I²C `0x27` | Passiv 4×4-matris direkt på GPIO |

**En vanlig ESP32 är inte en ESP32-S3.** USB-verktyget kontrollerar chipfamiljen,
men kan inte kontrollera display, ledningar eller I²C-modul. MCP23017 och andra
moduler ersätter inte PCF8574 utan en annan drivrutin.

### NodeMCU

- D2/GPIO4 → SDA, D1/GPIO5 → SCL.
- Knappsatsens PCF8574 matas på 3,3 V-sidan.
- LCD på 5 V kopplas via dubbelriktad I²C-nivåomvandlare, med gemensam jord.
- Kontrollera matrisordningen och modulernas adresser. Ledarordningen är inte standardiserad.
- [Komplett koppling och hårdvarutest](https://github.com/beahead-ab/trainmeet-tmbox/blob/main/firmware/esp8266/README.md).

### ESP32-S3

- GPIO8 → SDA, GPIO9 → SCL.
- Knappsatsens rader → GPIO4, 5, 6, 7; kolumner → GPIO15, 16, 17, 18.
- LCD på 5 V kopplas via dubbelriktad I²C-nivåomvandlare, med gemensam jord.
- [Komplett specifikation inklusive summer och lysdioder](https://github.com/beahead-ab/trainmeet-tmbox/blob/main/docs/TMBOX-V2-HARDWARE.md).

**Stäng av strömmen innan du kopplar. Anslut aldrig 5 V direkt till GPIO.**
Anslut inte DCC-, växel- eller körström till kortet. Om kopplingen inte stämmer
med profilen: stanna här, välj inte en nästan likadan firmware.

## 2. Installera via USB — nyinstallation

1. Öppna webbguiden i Chrome eller Edge på datorn och välj rätt box.
2. Anslut en USB-datakabel. På ESP32-S3 använder du USB–UART-uttaget.
3. Stäng andra seriella monitorer, exempelvis Arduino IDE:s monitor.
4. Läs varningen och markera att kopplingen är kontrollerad och boxen är ur trafik.
5. Klicka **Kontrollera installationsfilen**. Guiden hämtar filen och verifierar SHA-256.
6. Klicka **Anslut boxen och installera** och välj boxens USB-port i webbläsarens fråga.
7. Välj **Install** i USB-dialogen (på engelska). Vid installation på en ny box,
   välj **Erase device** när frågan visas och bekräfta installationen.
8. Vänta tills dialogen bekräftar att installationen lyckats. Koppla inte ur kabeln.
9. Låt boxen starta om. Markera utförd installation först när detta faktiskt har lyckats.

Guiden öppnar aldrig USB utan ditt klick. Den skickar inga trafikkommandon och
lagrar inga Wi-Fi-lösenord. Firmwarefilen behöver inte väljas manuellt.

### Viktigt om en befintlig box

Detta flöde ersätter det gamla programmet. Det är **inte** en garanterat
databevarande uppdatering. Spara originalets program och inställningar för
återgång innan du ändrar en fungerande äldre box. Prova helst på ett separat kort.

En full radering tar bort nätinställningar. Även utan full radering kan den
sammanslagna ESP32-bilden skriva över lagringsområden. Guiden lovar därför
inte att Wi-Fi, serverval eller andra inställningar överlever. Trafikdata i
TrainMeet Server raderas inte av USB-installationen. Hårdvarans identitet består.

### Om USB-porten inte syns

- **Windows:** prova annan datakabel/USB-port. Kontrollera i Enhetshanteraren om
  USB-kretsen behöver sin tillverkares CH340- eller CP210x-drivrutin. Installera
  inte en slumpmässig drivrutin eller ge webbplatser fjärråtkomst.
- **Mac:** använd Chrome eller Edge för USB-steget. Stäng program som använder
  porten. Eventuell USB-drivrutin måste motsvara kortets USB-krets.
- **Linux/Raspberry Pi OS Desktop:** skrivbordssession och webbläsare krävs.
  Användaren kan behöva seriell-portbehörighet. På Debian/Raspberry Pi OS:

  ```sh
  sudo usermod -a -G dialout "$USER"
  ```

  Logga därefter ut och in igen. Kör inte webbläsaren som root och gör inte
  seriella portar allmänt skrivbara. En Pi med enbart Lite/SSH kan inte visa
  webbguidens USB-dialog; använd en annan dator eller utvecklarflödet i README.
- **iPhone/iPad:** guiden går att läsa; gör USB-installationen från en dator.

ESP32-S3 som inte går in i uppladdningsläge: håll **BOOT**, tryck och släpp
**RESET**, släpp BOOT och försök igen. NodeMCU brukar gå in automatiskt.
Om verktyget anger fel chipfamilj: avbryt och kontrollera modell och vald port.

## 3. Koppla boxen till Wi-Fi

1. Låt USB ge ström. Vid första start öppnar boxen nätverket `TrainMeet-XXXXXX`.
2. Anteckna identitetskoden på displayen: `TBX-XXXXXX` för NodeMCU eller
   `TMBOX-XXXXXX` för ESP32-S3.
3. Anslut telefonen till boxens nät. Godkänn att vara kvar utan internet.
4. Om portalen inte öppnas, skriv **http://192.168.4.1** på telefonen.
5. Välj träffens **2,4 GHz-Wi-Fi** och ange dess lösenord.
6. Låt boxen hitta den lokala servern automatiskt. Ingen av modellerna har manuella IP-/portfält.
7. Spara och anslut telefonen till det vanliga nätet igen.

Gör detta på en betrodd plats; boxens tillfälliga installationsnät är öppet.
Wi-Fi sätts via boxens portal. **Improv Serial/Wi-Fi-inställning direkt via USB
är ännu inte implementerat** och guiden låtsas inte kunna läsa anslutningsstatus.

### Server och portar

Servern upptäcks med mDNS på samma Wi-Fi. MQTT använder normalt port 1883,
webbadmin normalt 8787. En isolerad gäst-Wi-Fi eller multicastspärr kan hindra
upptäckt. Kontrollera nätet i stället för att ange webbporten i enheten.
Ingen box kan välja sin egen station. **Öppna inte MQTT oskyddat mot internet.**

ESP8266 och ESP32 använder samma regel: en ny box ansluter när exakt en server
upptäcks. Vid flera servrar visas **FLERA SERVRAR / BE ADMIN HJALPA**. Administratören
behöver då se till att bara avsedd server annonseras på boxens nät vid första
anslutningen. Boxen väljer inte slumpmässigt första svaret.

Efter stationstilldelning sparas serverns annonserade ID på boxen. Om serverns
IP-adress ändras hittas den igen, men boxen byter inte till en annan server om
den sparade saknas. Stationstilldelningen ligger fortfarande på servern.
Uppdatera servern före denna firmware: den nya serverkoden sparar ett unikt
installations-ID; äldre servrar använder datornamnet, som inte är garanterat unikt.
ID:t är hjälp för serverval på ett betrott lokalnät, inte kryptografisk autentisering.

### Öppna installationen igen

- **Båda modellerna:** håll `*` i fem sekunder. Portalen öppnas utan att sparade
  Wi-Fi-uppgifter eller serverval raderas. Spara nya nätuppgifter eller avsluta portalen.
- Vill du avsiktligt byta server: markera **Byt TrainMeet Server (behall Wi-Fi)**
  i portalen och spara. Markeringen är avstängd från början. Boxens identitet,
  språkcache och trafikdata på servern raderas inte. Den nya serverns admin
  tilldelar station. Finns flera servrar gäller regeln ovan.

## 4. Administratören tilldelar station i TrainMeet Server

1. Öppna **din servers webbadmin**, inte Cloud eller boxens Wi-Fi-portal.
2. Logga in som administratör. Ny server: skapa eget konto och aktivera träffens config först.
3. Öppna administrationen för boxar/enheter. Menynamnet kan skilja mellan versioner.
4. Välj boxen i listan; ej tilldelade klienter visas först.
5. Klicka **Tilldela station**, välj station och spara. Enhetskoden visas redan
   och behöver inte skrivas av. För en tilldelad box heter knappen **Ändra station**.
6. Kontrollera att rätt stationsnamn och aktuellt läge visas på boxen.

Boxen rapporterar sitt permanenta ID och inväntar administratören.
Stationsbyte görs i serverns inställningar, aldrig på boxen eller i Cloud.
Ingen gammal A–D-panel krävs för den nya serverstyrda 16×2-profilen.
Borttagna klienter återaktiveras endast med det separata valet
**Återanslut borttagen klient → Återanslut med enhetskod**. Automatisk upptäckt
återställer aldrig en borttagen klients behörighet.

### Vilken serverversion?

**Firmware 0.7.0 kräver Server 1.10.0 eller senare. Uppdatera servern först.**
Båda korten får texter och logik från servern. På 20×4-skärm används tills
vidare 16×2 yta. Avsluta äldre pågående klareringar före första uppgraderingen;
servern vägrar att tappa dem. Prova den nya firmwaren på fysisk testbänk.

## 5. Kontrollera — när du har hårdvaran

- [ ] USB-installationen bekräftades som lyckad, boxen startade om.
- [ ] Display, kontrast och alla tangenter är verifierade på testbänk.
- [ ] Stationsnamn och boxkod stämmer med servern.
- [ ] Omstart återger samma identitet och rätt station.
- [ ] Bortkopplad server spärrar trafikåtgärder; återanslutning hämtar nytt läge.
- [ ] Ett helt trafikärende fungerar mellan stationer, inklusive de klienttyper
      som ska användas tillsammans.

Kontrollera tangenter i hårdvarutest eller separat testträff, inte genom att
trycka godkännande-/avgångsknappar under trafikdrift. C/D bläddrar i serverns aktuella val; knapptest och trafikfunktion är olika saker.
Guidens kryssrutor är egna markeringar, inte automatiska godkännanden.

Efter installation behöver datorn inte vara kvar. Ge boxen stabil USB-ström,
Wi-Fi och kontakt med TrainMeet Server. Ingen internetanslutning eller Cloud
behövs för att fortsätta trafik på en redan konfigurerad lokal server.

## För den som publicerar guiden

Byggjobbet i GitHub Actions skapar `trainmeet-tmbox-installer`, ett komplett
paket med HTML, lokala typsnitt, JavaScript, båda firmwarebilderna,
versionsnummer, källrevision, chipmanifest och SHA-256. Ingen hemlighet eller
Wi-Fi-nyckel ska någonsin finnas i paketet.

ESP32-bilden slås ihop av esptool med det faktiska PlatformIO-byggets
bootloader, partitioner, boot_app0 och applikation samt rätt flashinställningar.
Guiden erbjuder inte en ensam ESP32-applikationsfil som nyinstallation.

Hela paketet ska publiceras **atomiskt på samma HTTPS-origin**, exempelvis
senare på `/tmbox/`. Behåll versionspaketen för återgång. Actions-artefakter
är tillfälliga utvecklarleveranser; de kräver normalt GitHub-inloggning och
ersätter inte en publik installationssida. Workflow `Package installer for a
version` kan bygga ett versionspaket och valfritt publicera en **prerelease**
med ZIP-fil. Ett ZIP-paket måste serveras via HTTPS eller localhost; dubbelklick
på HTML-filen räcker inte för USB-funktionen. Ingen DNS eller serverinställning
ändras av byggjobbet.

Utvecklarens lokala förhandsvisning av enbart guiden:

```sh
npm ci --prefix installer
npm run build --prefix installer
python3 -m http.server 8792 --bind 127.0.0.1 --directory dist/installer
```

Öppna **http://localhost:8792**. Den förhandsvisningen har inga firmwarefiler
och spärrar därför installationen. För komplett paket bygg först profilerna:

```sh
pio run -d firmware/esp8266 -e nodemcu-i2c
pio run -d firmware/esp32 -e esp32-s3
python3 scripts/package_installer.py
```

Detta är utvecklarens byggflöde; slutanvändaren ska använda de publicerade,
färdiga filerna. Båda profilerna måste vara från samma firmwareversion och
källrevision. Publicera inte en blandning av gamla och nya byggfiler.

## Tekniska källor

- [ESP Web Tools — manifest, chipkontroll, HTTPS, radering och Improv](https://esphome.github.io/esp-web-tools/).
- [Web Serial — säker kontext och användarens portval](https://developer.mozilla.org/en-US/docs/Web/API/Web_Serial_API).
- Kopplingsanvisningarna och Wi-Fi-flödena ovan följer firmwaren i detta repo.
