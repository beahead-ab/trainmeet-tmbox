# TrainMeet TMBox – NodeMCU / ESP8266

Separat firmware för **NodeMCU 1.0 (ESP-12E/ESP-12F, 4 MB)** med:

- 16×2 LCD med I²C-backpack, normalt adress `0x27`
- passiv 4×4-knappsats via en **separat PCF8574/PCF8574A** på I²C, normalt `0x20`
- TrainMeet Server och dess MQTT-broker på det lokala nätverket

**I²C beskriver bussen, inte vilken krets modulen använder.** Denna variant
är för PCF8574-familjen. MCP23017, TCA8418 eller en modul med egen processor
och eget protokoll behöver en annan drivrutin. Kontrollera kretsens märkning
innan inkoppling. Ingen direktkoppling av åtta ledare till NodeMCU antas.

## Status

Byggjobbet [Build TMBox firmware](https://github.com/beahead-ab/trainmeet-tmbox/actions)
kompilerar både huvudprogram och hårdvarutest för `nodemcuv2`, kör tester för
tangenthantering och erbjuder de kompilerade `.bin`-filerna som byggartefakt.
Ett godkänt bygge är **inte ett fysiskt hårdvarutest**. Följ kontrollen nedan
på just din display, knappsatsmodul och kablage innan trafikdrift.

Den befintliga ESP32-varianten ligger kvar oförändrad i `firmware/esp32`.

**Detta är en separat MQTT v1-klient, inte en portning av TMBox v2.**
Display och tillåtna tangenter kommer från serverns `tambox/v1`-protokoll.
Den innehåller inte v2:s lokala menyer, summer-/LED-policy eller v2-simulator.
Befintliga legacy-boxar med `mqttTamBox` ändras inte automatiskt. Laddar du
denna firmware via USB ersätts kortets nuvarande program: prova på ett separat
kort först och behåll originalets firmware och inställningar för återgång.

De dokumenterade legacy-boxarna använder just PCF8574 `0x20`, LCD `0x27`,
D2/SDA och D1/SCL. Matrisledningarnas ordning är däremot inte verifierad här.
Se [legacy-hårdvaran](../../docs/TMBOX-V1-LEGACY.md). Den dokumentationen
beskriver också äldre 5 V-kopplingar utan nivåomvandlare; kopiera inte den
elektriska bristen när du bygger nytt.

## 1. Koppla hårdvaran (strömmen avstängd)

| NodeMCU | GPIO | Anslutning |
|---|---:|---|
| D2 | 4 | SDA, I²C-data |
| D1 | 5 | SCL, I²C-klocka |
| 3V3 | – | PCF8574-knappsatsmodulens VCC och nivåomvandlarens LV |
| GND | – | Gemensam jord inom boxens lågvoltsdel |
| USB | – | Stabil 5 V USB-matning, lämpligen minst 1 A |

GPIO0/D3, GPIO2/D4 och GPIO15/D8 används **inte**. De påverkar hur ESP8266
startar. RX/TX lämnas också fria för USB-programmering och seriell diagnostik.

### Separat knappmodul och displayadapter

```text
NodeMCU D2/SDA ──┬── PCF8574 SDA (3,3 V, adress 0x20)
NodeMCU D1/SCL ──┼── PCF8574 SCL
                │       P0–P7 → knappsatsens åtta ledare
                │
                └── dubbelriktad I²C-nivåomvandlare ── LCD-adapter (5 V, 0x27)
                     LV = 3,3 V / HV = 5 V
                     gemensam GND
```

SDA och SCL är två separata ledare längs hela bussen. Displayen och knappsatsen
delar bussen men **måste ha olika I²C-adresser**. Bilden ovan är en principskiss,
inte ett kopplingsschema för en okänd modul.

**Anslut inte 5 V till NodeMCU:s GPIO.** LCD-backpack har ofta pullupmotstånd
till sin egen matning. Om LCD:n matas med 5 V behövs en dubbelriktad
I²C-nivåomvandlare; PCF8574-knappsatsen ligger på 3,3 V-sidan. Kontrollera
pullupmotstånd, gemensam jord och modulens verkliga matning. Anslut aldrig
DCC-, växel- eller körström till kortet. Använd korta I²C-ledningar.

### Matrisens standardkoppling

| PCF8574 | Knappsatsledning | Tangenter |
|---|---|---|
| P0 | R1 | 1, 2, 3, A |
| P1 | R2 | 4, 5, 6, B |
| P2 | R3 | 7, 8, 9, C |
| P3 | R4 | *, 0, #, D |
| P4 | C1 | 1, 4, 7, * |
| P5 | C2 | 2, 5, 8, 0 |
| P6 | C3 | 3, 6, 9, # |
| P7 | C4 | A, B, C, D |

Ordningen på knappsatsens kontakt är inte standardiserad. Kontrollera den med
multimeter på strömlös, frånkopplad knappsats. Om din modul använder en annan
ordning ändras `TAMBOX_KEYPAD_ROWS` och `TAMBOX_KEYPAD_COLS` i
[`hardware_profile.h`](TrainMeetTambox8266/hardware_profile.h).
Det är **PCF-pinnummer 0–7**, inte NodeMCU-pinnar.

## 2. Hämta och kompilera – PlatformIO (rekommenderat)

Installera [Visual Studio Code](https://code.visualstudio.com/) och tillägget
[PlatformIO IDE](https://platformio.org/install/ide?install=vscode). Öppna
PlatformIO-terminalen, där `pio` finns tillgängligt.

```sh
git clone https://github.com/beahead-ab/trainmeet-tmbox.git
cd trainmeet-tmbox/firmware/esp8266
pio run -e nodemcu-hardware-check
pio run -e nodemcu-i2c
```

Har du redan repot, kör `git pull --ff-only` i repomappen i stället för att
klona igen. PlatformIO hämtar kortstöd och de versionslåsta biblioteken.
Ingen Wi-Fi-nyckel, stationskod eller serveradress behöver skrivas i koden.

## 3. Testa först display och knappsats

Anslut NodeMCU med en USB-kabel som kan överföra data, och kör:

```sh
pio device list
pio run -e nodemcu-hardware-check -t upload
pio device monitor -b 115200
```

Om flera portar finns, välj rätt, exempelvis på Windows:

```sh
pio run -e nodemcu-hardware-check -t upload --upload-port COM5
pio device monitor -b 115200 --port COM5
```

Byt `COM5` mot porten från `pio device list`. På Mac brukar den börja med
`/dev/cu.` och på Linux med `/dev/ttyUSB` eller `/dev/ttyACM`.

Testprogrammet kör **ingen trafik och ansluter inte till Wi-Fi**.

1. Läs I²C-skanningen i seriell monitor. Båda modulerna ska hittas.
2. Standard är knappsats `0x20` och LCD `0x27`. PCF8574A använder normalt
   adresser inom `0x38–0x3F`. Ändra adressdefinitionerna i `hardware_profile.h`
   om ditt kort har andra adresser. Ändra inte bara på chans.
3. Justera LCD-kontrasten tills texten syns.
4. Tryck och släpp alla 16 tangenter en i taget. Kontrollera rätt tecken
   både på displayen och i seriell monitor.
5. Flera samtidiga tangenter ignoreras tills alla släppts. Detta undviker
   spöktangenter i matriser utan dioder.
6. Starta om med en tangent nedtryckt. Kortet ska starta normalt; släpp
   tangenten innan den ska räknas som ett nytt tryck.

Avsluta seriell monitor med Ctrl+C innan nästa uppladdning.

## 4. Ladda in huvudprogrammet

```sh
pio run -e nodemcu-i2c -t upload
pio device monitor -b 115200
```

1. Boxen visar sin permanenta kod, exempelvis `TBX-12AB34`.
2. Vid första starten skapas nätverket `TrainMeet-12AB34`.
3. Anslut telefonen till det nätverket. Öppna `http://192.168.4.1` om portalen
   inte öppnas automatiskt.
4. Välj träffens **2,4 GHz-Wi-Fi** och ange dess lösenord.
5. Serverfältet kan vara tomt för automatisk mDNS-sökning efter `_tmbox._tcp`,
   samma tjänst som ESP32 använder.
   Om fler än en server hittas väljer boxen inte själv. Ange då rätt servers
   lokala IP-adress i portalen, exempelvis `192.168.2.160`.
6. MQTT-porten är normalt **1883**, inte webbporten 8787. Skriv bara IP-adress
   eller värdnamn, utan `https://`, sökväg eller port i adressfältet.
7. I **TrainMeet Servers** admin väljer du station för den upptäckta boxen.
   En uppdaterad server kopplar då även stationens entydiga v1-panel vid
   boxens nästa kontakt. Därefter hämtas display och tillåtna tangenter.

**Serverkrav:** servern behöver rättningen för MQTT v1 efter stationstilldelning
(NodeMCU-kompatibiliteten). Äldre stationbaserade versioner sparar bara
stationen och ger ingen v1-skärmbild. Visas `V1-PANEL SAKNAS`, uppdatera
servern och kontrollera att stationen har exakt en logisk A–D-panel. Finns
flera paneler gissar servern inte: v1 kräver då en uttrycklig paneltilldelning
via serverns äldre API. V2-klienters stationstilldelning ändras inte.

Servern kan köras på Raspberry Pi, Mac, PC eller Linux. Det är samma lokala
protokoll. Att serverns webbsida går att nå via HTTPS betyder inte att dess
MQTT-broker automatiskt är nåbar; boxen behöver en direkt LAN-anslutning till
brokern. **Öppna inte en lösenordslös MQTT-port mot internet.**

Håll `*` i fem sekunder för att öppna installationen igen och byta nät/server.
Det raderar inte boxens identitet eller serverns träff. Nätuppgifter sparas
av Wi-Fi-systemet och servervalet i EEPROM med kontrollsumma. Konfigurera det
tillfälliga, öppna installationsnätet på en betrodd plats.

### Test med bara NodeMCU ansluten via USB

Huvudprogrammet kan ansluta till Wi-Fi, hitta servern och registrera boxens ID
även utan LCD och knappsats. `LCD missing` och `Keypad missing` är då väntade
meddelanden. I normalt driftläge förblir trafikknapparna spärrade; det är inte ett komplett
funktionstest av en TMBox. Raderna `LCD |...|...|` i seriell monitor visar
avsedd text, inte att en fysisk display har hittats.

Läs boxens tilldelade IP-adress och **Webbtestkod** i seriell monitor.
Boxens IP är inte samma adress som TrainMeet Server. `192.168.4.1` är bara
boxens tillfälliga installationsportal.

### Telefon som display och knappsats – webbtestläge

1. Installera huvudprogrammet `nodemcu-i2c`, inte hårdvarutestet.
2. Anslut kortet till Wi-Fi via installationsportalen. Lämna serverfältet
   tomt för automatisk upptäckt.
3. Öppna seriell monitor med 115200 baud. Där finns boxens webbadress och
   en sexsiffrig **webbtestkod**, som byts vid varje omstart.
4. Öppna boxens webbadress, exempelvis `http://192.168.0.45/`, i telefonen
   på samma lokala nät. Ange webbtestkoden för att parkoppla telefonen.
5. Under **Anslut till träffens server** anger du serverns IP-adress och
   **lokala anslutningskod**, och trycker **Anslut till servern**. Adressen
   kan vara exempelvis `192.168.0.160` eller `http://192.168.0.160:8787/`.
   Lämna adressen tom om du vill behålla automatisk upptäckt.
   Sidan sparar adressen, väntar på serverns bekräftelse av boxens registrering
   och kontrollerar sedan koden. Status och eventuella fel visas vid knappen;
   koden behålls vid fel. Det är inte webbtestkoden och inte Cloud-koden.
   Äldre servrar utan `/v1/tmbox/enroll` måste uppdateras för detta steg.
   Ingen osäker reserv till en annan parkopplingsmetod används.
6. Administratören tilldelar boxens ID en station i TrainMeet Server.
   Kodbekräftelsen väljer aldrig station eller ändrar befintlig tilldelning.
7. Vänta på en aktuell serverpanel och välj **Aktivera webbtest**. Telefonen
   visar två displayrader och alla 16 tangenter: 0–9, A–D, `*` och `#`.
   Endast tangenter som den aktuella serverpanelen tillåter är aktiva.
8. Varje tryck går via kortets ordinarie MQTT-kommandoväg. Nästa tryck
   spärras tills kvittens och en ny skärmbild har kommit tillbaka.

Webbtest ersätter tillfälligt saknad display/knappsats. Fysiska tangenter
spärras samtidigt, så att två inmatningssätt inte konkurrerar. Långtryck på
den virtuella `*` öppnar inte installationsportalen; tangenten skickas till
serverns panel precis som övriga tillåtna trafikknappar.

**Detta är riktiga kommandon till träffen, inte en simulator. Använd en
testträff.** Webbtest stängs av vid omstart, tappad server/nätanslutning,
serverbyte, frånkoppling eller tio minuters inaktivitet. Statusavläsning
förlänger inte tiden. Inga trafikkommandon lagras för senare uppspelning.

En telefon åt gången kan vara parkopplad. Fem felaktiga kodförsök ger en
minuts spärr. Använd bara på ett betrott lokalt nät; HTTP och den befintliga
MQTT v1-trafiken är inte krypterade. Vidarebefordra inte dessa portar mot
internet. Serverkodsfunktionen är registrering med adminstyrd tilldelning,
inte en ny autentiseringsmekanism för själva MQTT-brokern.

Under **Avancerade serverinställningar** kan MQTT-port (normalt 1883) och
serverns webbport (normalt 8787, används för kodkontrollen) ändras.
En port i en inklistrad webbadress avser alltid webbporten, aldrig MQTT-porten.
**Spara adress utan kod** byter anslutning utan att godkänna någon kod eller
tilldela en station. Serverkoden sparas inte i boxen. Ett kodanrop skickas
bara en gång; vid ett osäkert nätavbrott, kontrollera servern innan du försöker
igen. Wi-Fi ändras fortsatt i installationsportalen: fysisk `*` i
fem sekunder när webbtest är avstängt. Vid saknat Wi-Fi öppnas portalen
automatiskt efter 30 sekunder. Inget fjärrkommando raderar nätuppgifterna.

### Om servern inte hittas

- Lämna serverfältet tomt för automatisk upptäckt. Boxen söker på nytt vid
  anslutningsförsök, även när en server har startats om eller bytt IP-adress.
- Servern och boxen behöver samma lokala nät med mDNS/multicast tillåtet.
  Servern kan vara kabelansluten. Gästnät, klientisolering och separata VLAN
  kan hindra upptäckt även om internet fungerar.
- Kontrollera att lokal TrainMeet Server och dess MQTT-broker är startade.
  Cloud är inte boxens driftserver. Ange serverns lokala IP och MQTT-port
  manuellt om nätet blockerar mDNS. Öppna inte MQTT mot internet.
- Om flera servrar annonserar sig måste administratören ange vilken som
  ska användas; boxen gissar inte vilken träff som är aktiv.
- Behöver boxen en förutsägbar IP-adress: använd helst en DHCP-reservation
  i routern. Det ändrar inte boxens permanenta ID eller stationstilldelning.

## Alternativ: Arduino IDE

**Enklast:** hämta [Arduino-paketet för NodeMCU](../../docs/FIRMWARE-DOWNLOADS.md),
packa upp i en ny mapp och följ START-HERE.md. Det innehåller alla stödfiler,
men inte PlatformIO:s `src/main.cpp`. Programmet ska inte läggas till som
ett ZIP-bibliotek i Arduino IDE. Ett separat hårdvarutestpaket finns också.

För den som i stället arbetar direkt i repots källkod:

1. Installera [Arduino IDE](https://www.arduino.cc/en/software/).
2. Lägg till denna URL under Inställningar → Ytterligare kort-URL:er:
   `https://arduino.esp8266.com/stable/package_esp8266com_index.json`.
3. Installera **esp8266 by ESP8266 Community**, version **3.1.2**, i Boards Manager.
4. Välj **NodeMCU 1.0 (ESP-12E Module)**, 80 MHz och **4 MB flash**.
5. Installera dessa bibliotek i Library Manager:
   - ArduinoJson **7.4.2** (Benoit Blanchon)
   - ArduinoMqttClient **0.1.8** (Arduino)
   - LiquidCrystal_PCF8574 **2.3.0** (Matthias Hertel)
   - WiFiManager **2.0.17** (tzapu)
6. Öppna `TrainMeetTambox8266/TrainMeetTambox8266.ino`. Alla `.h`-filer i
   **samma mapp måste följa med**; flytta inte ut bara `.ino`-filen.
7. Välj USB-port. Kontrollera adresser och matrisordning i `hardware_profile.h`.
8. För hårdvarutest: skriv `#define TAMBOX_HARDWARE_CHECK 1` allra överst i
   `.ino`-filen, kompilera och ladda upp. Ta bort raden och ladda upp igen
   för nätverksversionen. Därefter följer du Wi-Fi-stegen ovan.

ESP8266 använder [LiquidCrystal_PCF8574 **2.3.0** av Matthias Hertel](https://github.com/mathertel/LiquidCrystal_PCF8574/tree/2.3.0)
i både PlatformIO och Arduino Library Manager. Välj inte LiquidCrystal I2C;
ESP32:s bibliotek är oförändrat. Nedladdningspaketen byggtestas i båda systemen; IDE-menyerna har inte
testats manuellt på ett anslutet kort.

## Frivilligt debugläge via USB

Debugläget är av som standard (`TAMBOX_DEBUG_ENABLED` är `0`). För att se mer
felsökningsinformation, öppna
[`hardware_profile.h`](TrainMeetTambox8266/hardware_profile.h) och ändra den
befintliga definitionen till:

```cpp
#define TAMBOX_DEBUG_ENABLED 1
```

Kompilera och ladda upp samma firmwareprofil igen. Öppna seriell monitor med
**115200 baud**. Debugraderna visar funktion, källkodens radnummer och
millisekunder sedan start (`millis`). Inga extra bibliotek eller separata
debugpaket behövs. Ändra tillbaka till `0` och bygg/ladda upp igen för att
stänga av debugutskrifterna.

Normala statusrader och huvudprogrammets **USB-webbtestkod visas även när debug
är av**. Hårdvarutestprofilen har fortfarande ingen webbpanel eller webbtestkod.
Dela inte webbtestkoden i offentliga loggar. Flaggan ändrar inte anslutning,
stationstilldelning eller MQTT-protokoll.

## Vad som händer vid avbrott

- Servern bestämmer tillåtna tangenter, display, session och revision.
- Gamla retained MQTT-skärmbilder räcker inte för att aktivera tangenttryck.
- Boxen frågar regelbundet efter ett aktuellt tillstånd, även efter omtilldelning.
- I normalt driftläge spärras trafikknapparna om Wi-Fi, MQTT, display eller
  knappsats saknas. Uttryckligt aktiverat webbtest ersätter display/knappsats,
  men kringgår aldrig kravet på en aktuell serveranslutning.
- Inget aktuellt serversvar inom 30 sekunder spärrar också knapparna, även om
  MQTT-brokern fortfarande svarar. Serverns offline-meddelande spärrar direkt.
- Ett skickat kommando väntar på kvittens och därefter aktuellt läge. Efter
  fem sekunder utan kvittens återansluter boxen; tangenttrycket köas inte om.
  Kontrollera serverns läge eftersom kommandot kan ha hunnit utföras.
- Dublettkontroll, serverrevision och behörig panel kontrolleras av servern.
- Pågående trafik använder **inte TrainMeet Cloud**, och boxen hämtar inte
  träffkonfigurationen själv.

## Källor för hårdvaruval

- [NodeMCU och ESP8266-pinnar/bootlägen](https://arduino-esp8266.readthedocs.io/en/latest/boards.html)
- [ESP8266-bibliotek, EEPROM och I²C](https://arduino-esp8266.readthedocs.io/en/latest/libraries.html)
- [PCF8574 datablad](https://www.ti.com/lit/ds/symlink/pcf8574.pdf)
- [PlatformIO nodemcuv2](https://docs.platformio.org/en/latest/boards/espressif8266/nodemcuv2.html)
