# TMBox v2 — hårdvaruspecifikation

**Status:** konstruktionsunderlag. Fastställd 2026-08-23 av produktbeslutet att
inte porta till ESP8266. Öppna punkter står i [sista avsnittet](#öppna-beslut)
och måste besvaras innan något beställs.

TMBox v1 Legacy — den tidigare generationens boxar — beskrivs i
[TMBOX-V1-LEGACY.md](TMBOX-V1-LEGACY.md). De rörs inte och behåller sin
befintliga firmware. Det här dokumentet handlar bara om v2.

---

## 1. Designprinciper

Fyra saker styr varje val nedan.

**Logiknivån är 3,3 V, hela vägen.** ESP32-S3:s GPIO tål inte 5 V. v1 kör
5 V-I2C rakt in på en 3,3 V-krets utan nivåomvandlare, vilket fungerar men
ligger utanför specifikationen. v2 gör inte så.

**Knappsatsen är passiv.** En matris av mekaniska kontakter har ingen egen
spänning — den kan sitta direkt på 3,3 V-GPIO utan nivåanpassning, till
skillnad från v1:s PCF8574 på en 5 V-buss. Det tar bort en hel klass av
problem och en I2C-adress.

**Mekanisk kraft ska aldrig nå kretskortet.** Erfarenhet från legacy-boxarna
visar att USB-kontakten kan tryckas in i kortet och ge glapp. v2 löser det med ett
panelmonterat uttag och en intern kabel, inte med försiktighet.

**Firmware, simulator, tester och dokument delar en profil.** Ett pinnummer
finns på ett ställe. Det ställe är `hardware_profile.h`, profil 2.

---

## 2. Kort

| | |
|---|---|
| Modul | ESP32-S3-WROOM-1 |
| Utvecklingskort | **ESP32-S3-DevKitC-1-N8R2** |
| Flash | 8 MB |
| PSRAM | 2 MB, quad |
| Logiknivå | 3,3 V |

**Varför N8R2 och inte N16R8.** Varianten med 8 MB PSRAM använder *octal*
PSRAM, som upptar GPIO33–37. Quad-PSRAM gör det inte. Firmwaren behöver
varken 16 MB flash eller 8 MB PSRAM — hela sketchen med bibliotek ligger långt
under 2 MB — så det enda N16R8 skulle ge oss är fem färre fria pinnar.

**Varför ett DevKit och inte ett eget kretskort.** Antalet boxar är litet.
Ett DevKitC-1 ger USB, spänningsregulator, autoreset och antenn färdigt och
går att byta ut på en kväll om ett kort går sönder. Ett eget kort blir
billigare först vid betydligt större serier.

### Pinnar som inte får användas

| Pinnar | Varför |
|---|---|
| GPIO0 | Boot-strapping. Låg vid start ger nedladdningsläge. |
| GPIO19, GPIO20 | USB-OTG D− och D+. |
| GPIO26–32 | SPI-flash. |
| GPIO43, GPIO44 | UART0, seriemonitorn. |
| GPIO45, GPIO46 | Strapping. |
| GPIO38, GPIO48 | Kortets egen adresserbara RGB-lysdiod. Vilken av de två som används skiljer mellan kortrevisioner, så v2 använder ingen av dem och sätter sin egen lysdiod på kända pinnar i stället. |

GPIO33–37 är fria på N8R2 men upptagna på N16R8. v2 använder dem inte, så
profilen fungerar på båda om kortet någon gång byts.

---

## 3. Fullständig pinntabell

| Funktion | GPIO | Riktning | Anmärkning |
|---|---|---|---|
| Knappsats rad R1 | 4 | ut | 1 2 3 A |
| Knappsats rad R2 | 5 | ut | 4 5 6 B |
| Knappsats rad R3 | 6 | ut | 7 8 9 C |
| Knappsats rad R4 | 7 | ut | * 0 # D |
| Knappsats kolumn C1 | 15 | in, pull-up | 1 4 7 * |
| Knappsats kolumn C2 | 16 | in, pull-up | 2 5 8 0 |
| Knappsats kolumn C3 | 17 | in, pull-up | 3 6 9 # |
| Knappsats kolumn C4 | 18 | in, pull-up | A B C D |
| I2C SDA | 8 | dubbelriktad | till nivåomvandlarens LV-sida |
| I2C SCL | 9 | dubbelriktad | till nivåomvandlarens LV-sida |
| Summer | 10 | ut, LEDC-PWM | via NPN, se avsnitt 7 |
| Status röd | 11 | ut, aktiv låg | via NPN |
| Status grön | 12 | ut, aktiv låg | via NPN |
| Status blå | 13 | ut, aktiv låg | via NPN |
| Provisioneringsknapp | 14 | in, pull-up | aktiv låg, se avsnitt 8 |

Ingen av dessa är strapping-pinne, flash-pinne eller USB-pinne. Ingen tangent
sitter på en pinne som påverkar start — problemet med GPIO12 på klassisk ESP32
finns inte här.

Pinnarna 4–9 och 15–18 är oförändrade från profil 2 som redan finns i
`hardware_profile.h`. Pinnarna 10–14 är nya.

---

## 4. Display

| | |
|---|---|
| Typ | Teckendisplay, HD44780-kompatibel |
| Storlek | **20 tecken × 4 rader** |
| Vanlig modulbeteckning | 2004A |
| Gränssnitt | PCF8574-backpack på I2C |
| I2C-adress | `0x27` (PCF8574**T**) |
| Matning | 5 V |
| Kontrast | Skruvpotentiometer på backpacken |
| Bakgrundsbelysning | Alltid på |

**Varför 20×4 och inte 16×2.** Renderaren ritar redan alla fyra geometrierna
och guldfilerna täcker dem, så det är ingen mjukvarukostnad. Fyra rader ger
utrymme för tågnummer, spår, tid och åtgärdsrad samtidigt utan att korta av
text. Formatet används även i legacy-boxar och är därför prövat i drift.

**Adressen.** Backpackar med PCF8574T svarar på `0x27`, de med PCF8574A**T**
på `0x3F`. Beställ T-varianten. Firmwaren har `0x27` som förval och tar
`TMBOX_LCD_ADDRESS_VALUE` som byggflagga om en enskild låda skiljer sig.

**ÅÄÖ.** Standardteckenuppsättningen saknar dem. HD44780 tar åtta
egendefinierade tecken i CGRAM; Å, Ä och Ö som versaler tar tre och lämnar fem
över. Det räcker, och firmwaren kan sluta skriva `SPAR`, `BEGAR` och `FORARE`
utan prickar. Se öppen punkt Ö6.

---

## 5. Knappsats

| | |
|---|---|
| Typ | Passiv 4×4-matris, mekanisk |
| Anslutning | Åtta ledare direkt till GPIO |
| Nivåanpassning | Ingen — matrisen har ingen egen spänning |
| Teckenordning | `1 2 3 A` / `4 5 6 B` / `7 8 9 C` / `* 0 # D` |

Rader drivs som utgångar, kolumner läses med interna pull-ups. Biblioteket är
`chris--a/Keypad`, som redan används.

**Varför inte PCF8574 som i v1.** En I2C-knappsats behöver nivåanpassning på
samma buss som displayen, en andra adress, och ett avbrott eller polling för
att kännas svarande. En passiv matris behöver ingenting av det. Åtta ledare
till locket är billigare än den komplexiteten.

**Märkning.** Kontakten mellan lock och kort ska vara märkt `R1`–`R4` och
`C1`–`C4` på båda sidor. Kabelfärg duger inte som dokumentation — den varierar
mellan leveranser.

---

## 6. I2C-buss och nivåanpassning

Detta är den punkt där v2 medvetet skiljer sig från v1.

| | |
|---|---|
| Bussens 3,3 V-sida | ESP32-S3, GPIO8 (SDA) och GPIO9 (SCL) |
| Bussens 5 V-sida | Displayens PCF8574-backpack |
| Omvandlare | **PCA9306** dubbelriktad I2C-nivåtranslator |
| Alternativ | Diskret modul med BSS138, fyra kanaler |
| Pull-up 3,3 V-sidan | 4,7 kΩ till 3V3 |
| Pull-up 5 V-sidan | backpackens egna, normalt 4,7 kΩ till 5 V |
| Busshastighet | 100 kHz |

PCA9306 är byggd för just I2C och klarar att båda sidor är öppen-drän.
BSS138-moduler gör samma sak och är lättare att få tag på; de har egna
pull-ups på båda sidor, och då ska backpackens inte dubbleras med ytterligare
motstånd.

**Enda enheten på bussen är displayen.** Knappsatsen sitter på GPIO, så
adress `0x20` är ledig. Det ger plats för framtida I2C-tillbehör utan
konflikt.

---

## 7. Ljud

| | |
|---|---|
| Typ | Passiv piezosummer |
| Drivning | GPIO10 → 1 kΩ → NPN-bas |
| Transistor | BC547, 2N3904 eller likvärdig |
| Koppling | Emitter till GND, kollektor till summern, summerns andra ben till 3V3 |
| Frihjulsdiod | Behövs inte — piezo är kapacitiv, inte induktiv |

En passiv summer måste matas med en fyrkantvåg; ESP32-S3 gör det med LEDC.
Transistorn finns för att GPIO:n inte ska driva strömmen direkt.

Uppmärksamhetspolicyn är redan byggd och testad i
`lib/tmbox_core/attention.cpp` och hålls mot `golden_attention.txt`. Den vet
vad som förtjänar ett ljud och — lika viktigt — vad som inte gör det:

| När | Ton | Längd |
|---|---|---|
| Någon begär klarering hit | 2200 Hz | 250 ms |
| Vår begäran godkändes | 2600 Hz | 150 ms |
| Vår begäran nekades | 900 Hz | 250 ms |
| Linjen ledig mot oss | 1800 Hz | 150 ms |
| Servern svarar inte | 700 Hz | 600 ms |
| Servern svarar igen | 1400 Hz | 120 ms |

Tyst för det som inte är nyheter: en klarering som redan låg och väntade, allt
som fanns när boxen startade, och det tågklareraren själv nyss gjorde.

---

## 8. Statusindikering och provisioneringsknapp

### Lysdiod

| | |
|---|---|
| Typ | RGB 5 mm, gemensam anod |
| Matning | 5 V till anoden |
| Drivning | Tre NPN som lågsidesbrytare, en per färg |
| Bas | GPIO11/12/13 → 1 kΩ |
| Förmotstånd | 330 Ω röd, 220 Ω grön och blå |

Gemensam anod på 5 V med lågsidesbrytare gör att 3,3 V-logik räcker för att
styra, och att grön och blå får tillräcklig framspänning — vilket de inte får
om de matas direkt från 3,3 V.

> **Inte byggt.** `hardware_profile.h` namnger pinnarna, men firmwaren driver
> ingen av dem. Tabellen nedan är ett förslag, inte beteende. Färgvalen är
> öppen punkt Ö7, och koden skrivs när den är besvarad.

Föreslagna lägen:

| Läge | Färg |
|---|---|
| Ansluten och tilldelad station | grön, fast |
| Väntar på tilldelning | grön, långsam blinkning |
| Klarering väntar på svar | gul, blinkande |
| Ingen kontakt med servern | röd, långsam blinkning |
| Startar | blå, fast |

### Knapp

| | |
|---|---|
| Typ | Momentan, sluter mot GND |
| Pinne | GPIO14 med intern pull-up |
| Avstudsning | 100 nF över kontakten |
| Kort tryck | reserverat, ingen funktion idag |
| Långt tryck ≥ 5 s | rensar Wi-Fi-uppgifter och startar om i provisioneringsläge |

> **Inte byggt.** `TMBOX_PROVISION_BUTTON` och `TMBOX_PROVISION_HOLD_MS` finns
> i profilen, men firmwaren läser inte pinnen. Kopplingen ska finnas i
> prototypen så att funktionen går att lägga till utan att löda om.

Motsvarar `D0` i v1:s kopplingsschema för lösenordsåterställning. Knappen ska
sitta åtkomlig men inte lätt att trycka på
av misstag — försänkt i lådans undersida.

---

## 9. Ström

| | |
|---|---|
| Ingång | USB-C, panelmonterat uttag |
| Spänning | 5 V |
| Adapter | Minst 1 A; 2 A rekommenderas |
| Egen matning | Ja — boxen ska aldrig dela matning med DCC eller växelström |

**Panelmonteringen är hela poängen.** Ett USB-C-genomföringsuttag sitter i
lådans vägg och tar upp all mekanisk kraft. En kort intern USB-C-kabel går
därifrån till DevKitens UART-port. Kortet skruvas fast i distanser. Ingen
kraft från kabeln når kortets lödda kontakt.

Det löser det kända legacy-problemet där kortets kontakt sitter i lådans vägg
och kan tryckas in.

Samma uttag används för att flasha ny firmware — locket behöver inte öppnas.

### Avkopplingskondensatorer

| Var | Värde |
|---|---|
| 5 V-ingång, nära DevKiten | 470 µF elektrolyt, 10 V |
| 5 V-ingång, parallellt | 100 nF keramisk |
| 3V3-skenan vid DevKiten | 10 µF keramisk + 100 nF keramisk |
| Displayens backpack, VCC–GND | 100 nF keramisk |
| Nivåomvandlarens båda sidor | 100 nF keramisk vardera |

v1 har inga alls. Elektrolyten tar spänningsfallet när Wi-Fi-sändaren drar
ström i pulser; de keramiska tar högfrekvent brus nära varje krets.

---

## 10. Låda

| | |
|---|---|
| Skal | 3D-utskrift, väggtjocklek ≥ 3 mm |
| Frontpanel | 2 mm laserskuren akryl eller 1,5 mm aluminium |
| Infästning | M3 gänginsatser av mässing, ivärmda |
| Display och knappsats | Skruvas i frontpanelen |
| USB | Panelmonterat genomföringsuttag |
| Kortet | Distanser mot bottenplattan |

**Varför en separat frontpanel.** I legacy-boxar har display och knappsats
behövt limmas eftersom en 3D-utskriven låda inte har tillräckligt stark plast.
Limning är inte en lösning, det är en eftergift — den gör dessutom att en
trasig display inte går att byta. En skuren panel i akryl eller aluminium bär
infästningen, och skalet behöver då bara hålla panelen.

**Varför gänginsatser.** Skruv direkt i utskriven plast håller några
isärtagningar. Ivärmda mässinginsatser håller i praktiken obegränsat.

### Märkning

Varje låda ska ha en etikett på undersidan med samma kod som visas på
displayen vid start, `TMBOX-XXXXXX`. Koden härleds ur modulens MAC. Det gör
att en låda går att identifiera även när strömmen är av.

---

## 11. Stycklista

| Antal | Artikel | Anmärkning |
|---|---|---|
| 1 | ESP32-S3-DevKitC-1-N8R2 | 8 MB flash, 2 MB quad-PSRAM |
| 1 | Teckendisplay 20×4, HD44780-kompatibel | 2004A |
| 1 | PCF8574T I2C-backpack | adress `0x27` |
| 1 | Knappsats 4×4, mekanisk matris | passiv |
| 1 | PCA9306 nivåomvandlarmodul | eller BSS138, fyra kanaler |
| 1 | Passiv piezosummer | |
| 1 | RGB-lysdiod 5 mm, gemensam anod | |
| 4 | NPN-transistor BC547 eller 2N3904 | summer + tre färger |
| 4 | Motstånd 1 kΩ | bastströmbegränsning |
| 1 | Motstånd 330 Ω | röd |
| 2 | Motstånd 220 Ω | grön, blå |
| 2 | Motstånd 4,7 kΩ | I2C pull-up, 3,3 V-sidan |
| 1 | Momentan tryckknapp | provisionering |
| 1 | Kondensator 470 µF / 10 V | elektrolyt |
| 6 | Kondensator 100 nF | keramisk |
| 1 | Kondensator 10 µF | keramisk |
| 1 | USB-C genomföringsuttag, panelmontage | |
| 1 | USB-C-kabel, kort | internt, uttag till DevKit |
| 4 | M3 gänginsats, mässing | |
| 4 | Distans M3 | kort mot bottenplatta |
| 1 | Frontpanel, skuren | akryl eller aluminium |
| 1 | Skal, 3D-utskrivet | väggtjocklek ≥ 3 mm |

---

## 12. Kopplingsschema

```text
                        USB-C panelmonterat uttag
                                  │
                          kort USB-C-kabel
                                  │
                        ┌─────────┴──────────┐
                        │  ESP32-S3-DevKitC  │
              ┌─────────┤       -1-N8R2      ├──────────┐
              │         │                    │          │
              │    5V ──┤ 5V            3V3  ├── 3,3 V  │
              │         │                    │          │
              │         │ GPIO4  ────────────┼── R1     │
              │         │ GPIO5  ────────────┼── R2     │  KNAPPSATS
              │         │ GPIO6  ────────────┼── R3     │  4×4 passiv
              │         │ GPIO7  ────────────┼── R4     │  direkt på
              │         │ GPIO15 ────────────┼── C1     │  3,3 V-GPIO
              │         │ GPIO16 ────────────┼── C2     │
              │         │ GPIO17 ────────────┼── C3     │
              │         │ GPIO18 ────────────┼── C4     │
              │         │                    │          │
              │         │ GPIO8 (SDA) ───┐   │          │
              │         │ GPIO9 (SCL) ──┐│   │          │
              │         │               ││   │          │
              │         │ GPIO10 ───1k──┼┼───┤>─ summer (piezo) ── 3V3
              │         │ GPIO11 ───1k──┼┼───┤>─ 330R ─┐
              │         │ GPIO12 ───1k──┼┼───┤>─ 220R ─┼─ RGB, gem. anod ── 5V
              │         │ GPIO13 ───1k──┼┼───┤>─ 220R ─┘
              │         │               ││   │
              │         │ GPIO14 ───────┼┼───┤── knapp ── GND
              │         └───────────────┼┼───┘     │
              │                         ││       100nF
              │                         ││         │
              │                        GND       GND
              │                         ││
              │              ┌──────────┴┴──────────┐
              │   3,3 V ─────┤ LV              HV   ├───── 5 V
              │              │      PCA9306         │
              │   4k7 ┬──────┤ LV1 (SDA)  HV1 (SDA) ├──┐
              │   4k7 ┴──────┤ LV2 (SCL)  HV2 (SCL) ├─┐│
              │              └──────────────────────┘ ││
              │                                       ││
              │                        ┌──────────────┴┴───┐
              │                        │  PCF8574T  0x27   │
              │                        │  I2C-backpack     │
              └──── 5 V ───────────────┤ VCC               │
                                       │                   │
                                       │  HD44780 20×4     │
                                       └───────────────────┘

Avkoppling, ej utritad:
  470µF + 100nF vid 5 V-ingången
  10µF + 100nF vid 3V3
  100nF vid backpackens VCC och vid nivåomvandlarens båda sidor
```

---

## 13. Firmware och profil

Allt ovan sitter i **`hardware_profile.h`, profil 2**, som är förval från och
med det här beslutet. Byggmålet heter `esp32-s3` i `platformio.ini` och är
`default_envs`.

### Vad firmwaren gör med profilen idag

| Funktion | Läge |
|---|---|
| Display 20×4 på `0x27` | drivs |
| Knappsatsmatris på GPIO4–7 och 15–18 | drivs |
| Summer på GPIO10 | drivs, hela uppmärksamhetspolicyn |
| Statuslysdiod på GPIO11–13 | **pinnar namngivna, inget beteende** |
| Provisioneringsknapp på GPIO14 | **pinne namngiven, inget beteende** |

De två sista är avsiktligt ospecificerade i kod: Ö7 avgör färgerna, och en
knapp som rensar Wi-Fi-uppgifter ska inte skrivas innan det finns hårdvara att
prova den på. Kopplingen ska ändå finnas i prototypen, så att funktionen går
att lägga till utan att löda om.

Profil 1 och 3 beskriver klassisk ESP32 och står kvar som referens för den som
vill bygga på ett kort som råkar finnas i en låda. De är inte v2.

`lib/tmbox_core/` — renderare, navigation, uppmärksamhet, modell, text — rör
inte hårdvara alls. Den delen är 1279 rader ren C++17 och är oförändrad genom
hela det här beslutet. Guldfilerna gäller.

**Versionen står kvar under 1.0** tills en komplett v2-prototyp har byggts och
klarat bänktest. Den siffran höjs inte av att testerna är gröna. Ett program
som aldrig rört hårdvaran vet inte om det fungerar mot den.

---

## Öppna beslut

Måste besvaras innan konstruktionen kan beställas.

| # | Fråga | Varför den blockerar |
|---|---|---|
| Ö1 | Hur många boxar i första serien? | Avgör om skalet ska 3D-skrivas eller formsprutas, och om en egen kretskortsdesign lönar sig i stället för DevKit. |
| Ö3 | Vilken knappsatsmodell exakt? | Panelurtaget måste matcha. Membrantangentbord limmas; mekaniska skruvas. Vi vill ha mekaniska, men modellen bestämmer måtten. |
| Ö4 | Räcker DevKitens kortantenn i den låda vi väljer? | DevKitC-1 bär en ESP32-S3-WROOM-1 med antenn på kretskortet, och modulen går inte att byta på ett färdigt DevKit. Visar sig en aluminiumfront dämpa för mycket är svaret ett eget kretskort med en WROOM-1**U** och extern antenn — vilket i så fall river hela DevKit-valet i avsnitt 2. Mät på en prototyp innan panelmaterialet bestäms. Hänger ihop med Ö5. |
| Ö5 | Frontpanel i akryl eller aluminium? | Aluminium är starkare och snyggare men kan störa Wi-Fi och kräver isolering runt knappsatsen. Akryl är enklare. |
| Ö6 | Ska ÅÄÖ visas, eller behåller vi translitterering? | Tre CGRAM-tecken av åtta. Påverkar renderaren och guldfilerna — de måste skrivas om ifall svaret är ja. |
| Ö7 | Vilka statuslägen ska lysdioden ha? | Förslaget i avsnitt 8 är ännu inte beslutat. Färgvalen bör stämma med hur användarna tolkar signalfärger. |
| Ö8 | Ska boxen fungera utan accesspunkt på träffen? | Om ja behövs ett AP-läge i firmwaren och det är inte byggt. Om nej måste varje träff ha nät, vilket är ett driftkrav och inte ett hårdvarukrav. |
| Ö9 | Vem bygger, och var? | Påverkar om stycklistan ska peka på svenska leverantörer eller på ett samlat beställningsunderlag. |
| Ö10 | Ska v2-boxar kunna tala med v1-boxar på samma träff? | v1 kör `mqttTamBox` med ett annat protokoll. Om svaret är ja behöver servern översätta mellan de två, vilket är ett arbete ingen har budgeterat. |

Ö3 och Ö5 blockerar frontpanelen och måste besvaras innan något skärs.
Ö6 blockerar renderaren och guldfilerna. Ö4 kan i värsta fall riva DevKit-valet
och därmed hela avsnitt 2 — den ska mätas tidigt, på en prototyp, inte antas.
Ö10 är den enda som kan visa sig vara ett eget projekt.
