# Koppling av display och tangentbord

Det här dokumentet beskriver den fysiska Tamboxen med en klassisk ESP32,
16x2-teckendisplay med I2C-backpack och ett passivt 4x4-matristangentbord.

## Vad vi vet och vad som måste kontrolleras

Bekräftat från TrainMeet-koden och bilden på Bennys box:

- displayen är 16 tecken bred och 2 rader hög
- tangentbordet är 4x4 med ordningen `1 2 3 A / 4 5 6 B / 7 8 9 C / * 0 # D`
- den tidigare sketchen använder LCD-adress `0x27`
- den tidigare sketchen använder GPIO 13/12/14/27 för rader och
  26/25/33/32 för kolumner

Inte bekräftat förrän lådan öppnas:

- exakt ESP32-kort och vilken märkning som står på modulen
- ordningen på tangentbordets åtta ledare
- om LCD-backpacket drivs med 3,3 V eller 5 V
- om det redan sitter en I2C-nivåomvandlare i lådan
- om I2C-adressen verkligen är `0x27` på alla byggda boxar

Använd därför GPIO-numren på kortets silkscreen, inte pinnens fysiska nummer i
en bild från ett annat fabrikat.

## Rekommenderad uppbyggnad

```text
                 stabil 5 V / USB
                       |
              +--------+--------+
              |                 |
         ESP32 DevKit       LCD 16x2 + I2C
              |                 |
     3,3 V ---+-- LV      HV ---+--- 5 V
     GPIO21 ------ I2C-nivåomv. ----- SDA
     GPIO22 ------ I2C-nivåomv. ----- SCL
        GND ------- gemensam jord ---- GND
              |
              +------ 8 signalledare ------ 4x4 keypad
```

Tangentbordet är normalt bara 16 mekaniska kontakter i en matris. Ett vanligt
8-poligt tangentbord ska inte ha någon VCC- eller GND-ledare. Alla åtta ledare
går direkt till GPIO enligt vald profil.

## LCD: fyra anslutningar

| LCD/I2C | ESP32-sida | Funktion |
|---|---|---|
| GND | GND | Gemensam jord |
| VCC | 5 V via nivåomvandlare, alternativt 3,3 V efter test | Display och bakgrundsbelysning |
| SDA | GPIO21 på klassisk ESP32 | I2C-data |
| SCL | GPIO22 på klassisk ESP32 | I2C-klocka |

### Viktigt om 5 V

ESP32:s GPIO är inte 5 V-toleranta. Många PCF8574-backpack har SDA/SCL-
pullupmotstånd kopplade till samma VCC som displayen. Om backpacket matas med
5 V kan alltså SDA och SCL också ligga på 5 V.

Den elektriskt säkra standardlösningen är:

1. mata LCD/backpack med 5 V
2. sätt en dubbelriktad I2C-nivåomvandlare mellan ESP32 och LCD
3. anslut nivåomvandlarens LV till 3,3 V och HV till 5 V
4. ha gemensam GND på båda sidor

Ett alternativ är att mata hela LCD/backpacket med 3,3 V. Många exemplar
fungerar, men kontrast och bakgrundsbelysning kan bli svaga och det måste provas
med just den displayen. Anslut aldrig 5 V direkt till GPIO21 eller GPIO22.

### Adress och kontrast

- vanlig I2C-adress är `0x27`
- vissa backpack använder `0x3F`
- den lilla potentiometern på backpacket ställer teckenkontrasten
- synliga svarta block men ingen text betyder ofta fel initiering eller adress
- helt tom men belyst display betyder ofta fel kontrast, SDA/SCL eller adress

Diagnostikprogrammet skannar hela I2C-bussen och skriver hittade adresser i
seriell monitor. Hittar det `0x3F` byggs huvudprogrammet med:

```sh
pio run -e esp32-benny --project-option="build_flags=-D TAMBOX_HARDWARE_PROFILE=1 -D TAMBOX_LCD_ADDRESS_VALUE=0x3F"
```

## Tangentbordets matris

Logisk matris:

| | C1 | C2 | C3 | C4 |
|---|---:|---:|---:|---:|
| R1 | 1 | 2 | 3 | A |
| R2 | 4 | 5 | 6 | B |
| R3 | 7 | 8 | 9 | C |
| R4 | * | 0 | # | D |

### Profil 1: Bennys sannolika befintliga koppling

| Matrisledning | ESP32 GPIO | Tangenter på ledningen |
|---|---:|---|
| R1 | 13 | 1, 2, 3, A |
| R2 | 12 | 4, 5, 6, B |
| R3 | 14 | 7, 8, 9, C |
| R4 | 27 | *, 0, #, D |
| C1 | 26 | 1, 4, 7, * |
| C2 | 25 | 2, 5, 8, 0 |
| C3 | 33 | 3, 6, 9, # |
| C4 | 32 | A, B, C, D |

Detta är den enda pinmappning som faktiskt förekommer i den äldre TrainMeet-
sketchen. Den är därför förstahandsvalet när Bennys färdigkopplade box ska
provas.

GPIO12 är samtidigt en boot-strapping-pin på klassisk ESP32. Ett tangentläge
som påverkar GPIO12 under uppstart kan på vissa kort ge startproblem. Släpp alla
tangenter när boxen slås på. Om vi bygger om eller bygger nytt används den
säkrare profil 3.

### Profil 3: rekommenderad ny klassisk ESP32-koppling

Profil 3 är identisk med Bennys profil förutom R2:

| Matrisledning | ESP32 GPIO |
|---|---:|
| R1 | 13 |
| R2 | 23 |
| R3 | 14 |
| R4 | 27 |
| C1 | 26 |
| C2 | 25 |
| C3 | 33 |
| C4 | 32 |

Bygg med `pio run -e esp32-classic-safe`.

### Profil 2: ESP32-S3 DevKitC

| Matrisledning | ESP32-S3 GPIO |
|---|---:|
| R1 | 4 |
| R2 | 5 |
| R3 | 6 |
| R4 | 7 |
| C1 | 15 |
| C2 | 16 |
| C3 | 17 |
| C4 | 18 |
| LCD SDA | 8 |
| LCD SCL | 9 |

Detta är vår rekommenderade S3-profil, inte ett påstående om hur Bennys box är
kopplad.

## Ta reda på ordningen på tangentbordets åtta ledare

Ordningen R1-R4/C1-C4 på den fysiska kontakten är inte standardiserad mellan
alla tangentbord. Gissa därför inte efter kabelns färger.

Säkraste metoden:

1. bryt all ström och koppla loss tangentbordet från ESP32
2. ställ multimetern på kontinuitetsmätning
3. håll `1` nedtryckt och hitta vilka två ledare som sluts; de är R1 och C1
4. håll `2` nedtryckt; den gemensamma ledaren med `1` är R1 och den andra är C2
5. håll `4` nedtryckt; den gemensamma ledaren med `1` är C1 och den andra är R2
6. fortsätt med `3`, `7`, `*` och `A` tills alla åtta är identifierade
7. märk kontakten permanent med R1-R4 och C1-C4

Kontinuitetsmät aldrig i en spänningssatt box.

## Matning och störningar på en modelljärnvägsträff

- mata helst boxen från en egen stabil 5 V USB-adapter på minst 1 A
- koppla inte DCC-, körströms- eller växelspänning direkt till ESP32
- använd gemensam jord enbart inom Tamboxens lågvoltsdel
- placera 100 nF nära ESP32 och LCD samt gärna 220-470 µF över 5 V/GND där
  matningen kommer in i lådan
- håll SDA/SCL och tangentbordskabel borta från DCC-, motor- och reläkablar
- håll I2C-kabeln kort; under cirka 30 cm är ett bra första mål
- använd låsbara kontakter så att R/C-ledningar inte kan flyttas ett steg
- om tangenttryck blir instabila: kontrollera först jord, kontaktordning och
  kabeldragning; lägg inte kondensatorer på matrisen innan den rena kopplingen
  har verifierats

## Förslag på kontakter och märkning

- LCD: 4-polig kontakt märkt `GND / VCC / SDA / SCL`
- keypad: 8-polig kontakt märkt `R1 R2 R3 R4 C1 C2 C3 C4`
- nivåomvandlare: märk låg sida `3V3` och hög sida `5V`
- etikett under lådan: boxkod `TBX-XXXX`, kortmodell och hårdvaruprofil
- etikett inuti locket: komplett GPIO-tabell

Använd inte enbart kabelkulörer som dokumentation; färger varierar mellan
leveranser.

## Kontroll före första nätverksstart

1. Kontrollera att det inte finns kortslutning mellan 5 V och GND.
2. Kontrollera gemensam jord och I2C-nivåomvandlarens LV/HV-sidor.
3. Starta `diagnostics/hardware-check` utan Raspberry Pi eller Wi-Fi.
4. Bekräfta LCD-adressen i seriell monitor.
5. Justera kontrast tills båda textraderna syns.
6. Tryck alla 16 tangenter och kontrollera rätt tecken, rad och kolumn.
7. Starta om med alla tangenter släppta och kontrollera stabil boot.
8. Först därefter laddas `TrainMeetTambox.ino`.

## Felsökning

| Symptom | Trolig orsak | Kontroll |
|---|---|---|
| Ingen bakgrundsbelysning | ingen VCC/GND eller fel polaritet | mät matningen på LCD-kontakten |
| Belyst men tom LCD | kontrast, adress eller I2C | vrid trimmern och kör I2C-skannern |
| Svarta block | LCD har ström men initieras inte | kontrollera adress, SDA/SCL och bibliotek |
| Fel tecken för tangenter | R/C-ledningarna ligger i annan ordning | kartlägg kontakten med multimeter |
| En hel rad fungerar inte | avbrott i en R-ledning | kontrollera R1-R4 |
| En hel kolumn fungerar inte | avbrott i en C-ledning | kontrollera C1-C4 |
| Boxen startar inte när en knapp hålls | GPIO12 påverkar boot | släpp knappar eller använd profil 3 |
| Slumpmässiga tryck | dålig jord, lång kabel eller störning | korta kabeln och separera från DCC/relä |
| LCD fungerar på 3,3 V men är svag | låg backlight/kontrastspänning | använd 5 V med I2C-nivåomvandlare |
