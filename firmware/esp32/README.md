# TrainMeet physical TMBox firmware

This firmware turns a Wi-Fi capable ESP32 into a thin, local TMBox client
speaking protocol v2. The TrainMeet Server owns every traffic decision. The
ESP32 caches its assigned station's config and live snapshot in RAM, browses
that cache locally for speed, and only speaks on the wire to send a complete
command. See [`docs/underlag/tmbox-monsterprompt-v2.md`](../../docs/underlag/tmbox-monsterprompt-v2.md)
§3.4/§3.4a for the design this implements.

This is the connectivity-and-proof-of-protocol slice: identity, discovery,
station assignment, config/snapshot caching, a minimal movement browser and
one real write command (uppställt). The full local command-page interaction
(train lookup, spårväljare, klarering, linjen-ledig) is a separate, later
pass — see §22 step 3 in the same document.

## What happens at startup

1. The display shows the permanent box code, for example `TMBOX-A7K2C3`.
2. The box tries its last saved Wi-Fi network.
3. When Wi-Fi is available it discovers the TrainMeet Server over mDNS
   (`_tmbox._tcp`), connects without an MQTT password and announces its id
   and printed code on `tmbox/v2/device/{id}/hello`.
4. If the box is not assigned, the display says `KOPPLA BOXEN` and shows the
   same code. The administrator enters that code in the server's web view and
   assigns it to a station.
5. The server sends the station assignment, then retained `config` and
   `snapshot` topics. The box caches both wholesale in RAM and starts
   rendering: a station overview, or the day's cached movements (`C` cycles
   between them, `*` returns to the overview, `A` sends `train.position.set`
   for the movement currently shown).

If saved Wi-Fi cannot be reached, the box keeps retrying and then creates the
open setup network `TrainMeet-XXXXXX` (last six characters of the box code).
Join it with a phone and choose Wi-Fi in the captive portal. Credentials are
stored in the ESP32's non-volatile memory. Holding `*` for five seconds
clears Wi-Fi and reopens setup after reboot.

Network and server interruptions are normal states. The box never freezes in
an endless error loop; it retries with backoff and accepts no write command
until a fresh authoritative config and snapshot have arrived.

## Hardware profiles

- `esp32-benny` is the default. It uses the keypad pins from the existing
  TrainMeet sketch (rows 13/12/14/27, columns 26/25/33/32) and I2C on 21/22.
- `esp32-s3` is a separate profile so the application protocol remains the
  same when the electronics are changed later.
- `esp32-classic-safe` is recommended when wiring a new classic ESP32. It uses
  GPIO23 instead of the original boot-strapping pin GPIO12.

The display is a 16x2 I2C LCD at address `0x27`. Pin choices live only in
`hardware_profile.h`. The complete power, level-shifter, display, keypad and
connector guide is in [WIRING.md](WIRING.md). Run the standalone hardware
check in `diagnostics/hardware-check` before loading the network firmware.

## Build

Open this directory in PlatformIO and use one of:

```sh
pio run -e esp32-benny
pio run -e esp32-s3
pio run -e esp32-classic-safe
```

For Arduino IDE, install ArduinoJson, ArduinoMqttClient, Keypad,
LiquidCrystal_I2C and WiFiManager, then open `TrainMeetTMBox.ino`.
