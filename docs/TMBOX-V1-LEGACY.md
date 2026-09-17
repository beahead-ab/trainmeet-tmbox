# TMBox v1 Legacy — de boxar som redan finns

Det här dokumentet sammanfattar verifierad hårdvara från den tidigare
generationens boxar och deras kopplingsschema.

> De här boxarna är **TMBox v1 Legacy**. De behåller `mqttTamBox` och rörs
> inte automatiskt. TMBox v2-firmwaren portas **inte** till ESP8266 — se
> [TMBOX-V2-HARDWARE.md](TMBOX-V2-HARDWARE.md) för TMBox v2.
>
> Dokumentet står kvar som referens: det beskriver hårdvara som är i drift på
> riktiga träffar, och den kunskapen ska inte tappas bara för att vi bygger
> något nytt vid sidan av.

Det finns även en **valfri, separat TrainMeet MQTT v1-klient för NodeMCU**,
framtagen för den som uttryckligen vill testa en sådan variant på ett eget
kort. Den ersätter inte `mqttTamBox` i befintliga boxar och har inte v2:s
funktioner. Se [NodeMCU-guiden](../firmware/esp8266/README.md) för skillnaden,
kompilering och hårdvarutest före eventuell manuell laddning.

## Kortet

| | |
|---|---|
| Modul | ESP8266 (ESP-12F), nodeMCU V3 |
| Märkning på burken | `ESP8266MOD 12-F` (foto) |
| Antal byggda | Flera enheter med likvärdig koppling |
| Kopplade lika? | Ja — samma programvara, likvärdig koppling |

**Det är inte en ESP32.** V2-firmwaren i `firmware/esp32` byggs mot
`platform = espressif32` och använder `ESPmDNS.h`, `WiFi.h`, `Preferences.h`
och `esp_random()`, som alla är ESP32-specifika. Den kan inte laddas i en
v1-låda, och ska inte heller anpassas för att kunna det.

## Knappsats

| | |
|---|---|
| Typ | 4×4 matris |
| Ansluten via | **PCF8574 I2C-kort, adress `0x20`** |
| GPIO-matris | Finns inte |

Hårdvaruprofilen i `hardware_profile.h` läser tangenterna som en direkt
GPIO-matris (`TMBOX_ROW_PINS`, `TMBOX_COL_PINS`) med biblioteket
`chris--a/Keypad`. Det motsvarar ingenting i en v1-låda — och behöver inte
göra det. En passiv matris direkt på 3,3 V-GPIO är enklare än ett PCF8574 på
en buss som måste nivåanpassas, vilket är varför v2 gör så.

Frågan om GPIO12 som boot-pin är därmed inte längre relevant: ingen tangent
sitter på en GPIO.

## Display

| | |
|---|---|
| Typ | HD44780, 16×2 tecken |
| Backpack | PCF8574-kort |
| I2C-adress | `0x27` |
| Matning | Vin (5 V) |
| Nivåomvandlare | **Nej** |
| Kontrastpotentiometer | Ja |
| ÅÄÖ | Inte i standardteckenuppsättningen — hanteras som egendefinierade tecken, max 8 |

Det finns även legacy-boxar med 20×4-display. Programvaran ritar alla fyra
geometrierna, så det är en inställning per låda, inte en begränsning.

Att backpacken matas med 5 V utan nivåomvandlare betyder att SDA och SCL
vilar på 5 V genom kortets pull-up, medan ESP8266:ns GPIO är specificerade
för 3,3 V. Lådorna har fungerat så i drift. Det är ändå värt att veta innan
något nytt kopplas in på samma buss.

## I2C-buss

| Signal | nodeMCU-pin | GPIO |
|---|---|---|
| SDA | D2 | 4 |
| SCL | D1 | 5 |

Båda I2C-enheterna sitter på samma buss: knappsatsen på `0x20`, displayen på
`0x27`. Detta är nodeMCU:s default.

## Övriga pinnar (ur kopplingsschemat)

| Pin | GPIO | Funktion |
|---|---|---|
| D0 | 16 | Lösenordsåterställning |
| D5 | 14 | Summer, I/O |
| D6 | 12 | Led12, I/O |

`VIN` matar I2C-kortens VCC. `3V` matar summerns och Led12:s VCC.

## Ljud och ljus

| | |
|---|---|
| Summer | Passiv, sitter i samtliga boxar |
| Lysdioder | Led12 (mollehem.se) i en box; 4× RGB 5 mm gemensam anod i dellistan |
| Plats och ledig pinne | Ja |

Uppmärksamhetspolicyn i `lib/tmbox_core/attention.cpp` behöver alltså ingen
ny hårdvara — summern finns redan.

## Låda

| | |
|---|---|
| Märkning som skiljer lådor åt | Ja |
| USB nåbar utan att skruva isär | Ja |
| Display och knappsats | Fastmonterade i locket |

## Kända problem i dagens lådor

- Knappsats och display har fått limmas: en 3D-utskriven låda har inte
  tillräckligt stark plast.
- **USB-kontakten kan tryckas in i kortet och ger då dålig kontakt.** Värt
  att veta innan någon står och undrar varför en box inte kommer upp.

## Ström

| | |
|---|---|
| Matning | USB-adapter |
| Kapacitet | 2,4 A |
| Delad med DCC eller växelström | Nej |
| Avkopplingskondensatorer | Inte monterade |

## Befintlig programvara

Alla byggda boxar kör idag `mqttTamBox`:
<https://github.com/etxbct/mqttTamBox>

Den är skriven för exakt den här hårdvaran, den fungerar, och den fortsätter
vara det som kör i v1-lådorna.

Vi rör den inte. TrainMeets nya firmware och `mqttTamBox` är två program för
två generationer hårdvara, och det är avsiktligt.
