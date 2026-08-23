#pragma once

#include <Arduino.h>

// Which box the firmware is built for. Select with
// -DTMBOX_HARDWARE_PROFILE=1, 2 or 3.
//
// Profile 2 is TMBox v2 and is the default. Its pin table, part list and
// wiring live in docs/TMBOX-V2-HARDWARE.md, and that document is the one to
// change first - the numbers below follow it, not the other way round.
//
// Profiles 1 and 3 describe a classic ESP32 and are kept for anyone bringing
// up a board that happens to be lying around. They are not v2.
//
// No profile here describes TMBox v1 Legacy. Those boxes are ESP8266 with an
// I2C keypad, they keep running Benny's mqttTamBox, and this firmware is
// deliberately not ported to them. See docs/TMBOX-V1-LEGACY.md.
#ifndef TMBOX_HARDWARE_PROFILE
#define TMBOX_HARDWARE_PROFILE 2
#endif

#if TMBOX_HARDWARE_PROFILE == 1

#define TMBOX_MODEL_NAME "TMBox ESP32 Benny"
constexpr uint8_t TMBOX_LCD_SDA = 21;
constexpr uint8_t TMBOX_LCD_SCL = 22;
constexpr uint8_t TMBOX_ROW_PINS[4] = {13, 12, 14, 27};
constexpr uint8_t TMBOX_COL_PINS[4] = {26, 25, 33, 32};
constexpr uint8_t TMBOX_LCD_COLUMNS = 16;
constexpr uint8_t TMBOX_LCD_ROWS = 2;

#elif TMBOX_HARDWARE_PROFILE == 2

// TMBox v2. ESP32-S3-DevKitC-1-N8R2.
//
// Every pin below is chosen to avoid the ones the chip and the module spend
// on themselves: GPIO0, 45 and 46 are strapping, 19 and 20 are USB, 26-32 are
// the SPI flash, 43 and 44 are UART0, and 38 and 48 carry the board's own RGB
// LED on one revision or the other. GPIO33-37 are free on the N8R2 but taken
// by octal PSRAM on an N16R8, so v2 leaves them alone and the profile works
// on either board.
#define TMBOX_MODEL_NAME "TMBox v2"
constexpr uint8_t TMBOX_LCD_SDA = 8;
constexpr uint8_t TMBOX_LCD_SCL = 9;
constexpr uint8_t TMBOX_ROW_PINS[4] = {4, 5, 6, 7};
constexpr uint8_t TMBOX_COL_PINS[4] = {15, 16, 17, 18};
constexpr uint8_t TMBOX_LCD_COLUMNS = 20;
constexpr uint8_t TMBOX_LCD_ROWS = 4;

// The keypad is a passive matrix straight onto 3.3 V GPIO. It carries no
// supply of its own, so it needs no level shifting - which is the whole
// reason v2 does not put it on a PCF8574 the way v1 does.
#define TMBOX_KEYPAD_IS_PASSIVE_MATRIX 1

// The display's backpack runs at 5 V and pulls SDA and SCL up to 5 V. The
// ESP32-S3 is not 5 V tolerant, so a PCA9306 sits between them. Recorded here
// because a box wired without it will appear to work for a while.
#define TMBOX_I2C_LEVEL_SHIFTED 1

#define TMBOX_BUZZER_PIN_VALUE 10

// Status LED, common anode on 5 V, switched low-side through an NPN per
// colour. These are macros rather than constants for the same reason the
// buzzer pin is: the block at the bottom fills in the absent case, and
// #ifndef only sees the preprocessor.
#define TMBOX_STATUS_LED_RED_VALUE 11
#define TMBOX_STATUS_LED_GREEN_VALUE 12
#define TMBOX_STATUS_LED_BLUE_VALUE 13

// Momentary button to ground with the internal pull-up. Held for five
// seconds it clears the Wi-Fi credentials. A short press does nothing yet.
#define TMBOX_PROVISION_BUTTON_VALUE 14
#define TMBOX_PROVISION_HOLD_MS_VALUE 5000

#elif TMBOX_HARDWARE_PROFILE == 3

// Recommended for a newly wired classic ESP32. It avoids GPIO12, which is a
// boot strapping pin on the original ESP32 and can stop some modules booting
// if a keypad key is held while power is applied.
#define TMBOX_MODEL_NAME "TMBox ESP32 Safe"
constexpr uint8_t TMBOX_LCD_SDA = 21;
constexpr uint8_t TMBOX_LCD_SCL = 22;
constexpr uint8_t TMBOX_ROW_PINS[4] = {13, 23, 14, 27};
constexpr uint8_t TMBOX_COL_PINS[4] = {26, 25, 33, 32};
constexpr uint8_t TMBOX_LCD_COLUMNS = 16;
constexpr uint8_t TMBOX_LCD_ROWS = 2;

#else
#error "Unknown TMBOX_HARDWARE_PROFILE"
#endif

#ifndef TMBOX_LCD_ADDRESS_VALUE
// PCF8574T answers on 0x27, PCF8574AT on 0x3F. Order the T variant; override
// this in the build for a box that came with the other one.
#define TMBOX_LCD_ADDRESS_VALUE 0x27
#endif

constexpr uint8_t TMBOX_LCD_ADDRESS = TMBOX_LCD_ADDRESS_VALUE;

// Attention output. Profile 2 names the pin because v2 is drawn with a buzzer
// on it. On a profile that does not, the attention controller still runs - it
// just has nothing to drive. Every decision about *what* deserves a sound is
// already made and tested; only the wire would be missing.
#ifdef TMBOX_BUZZER_PIN_VALUE
constexpr int TMBOX_BUZZER_PIN = TMBOX_BUZZER_PIN_VALUE;
constexpr bool TMBOX_HAS_BUZZER = true;
#else
constexpr int TMBOX_BUZZER_PIN = -1;
constexpr bool TMBOX_HAS_BUZZER = false;
#endif

#ifdef TMBOX_STATUS_LED_RED_VALUE
constexpr int TMBOX_STATUS_LED_RED = TMBOX_STATUS_LED_RED_VALUE;
constexpr int TMBOX_STATUS_LED_GREEN = TMBOX_STATUS_LED_GREEN_VALUE;
constexpr int TMBOX_STATUS_LED_BLUE = TMBOX_STATUS_LED_BLUE_VALUE;
constexpr bool TMBOX_HAS_STATUS_LED = true;
#else
constexpr int TMBOX_STATUS_LED_RED = -1;
constexpr int TMBOX_STATUS_LED_GREEN = -1;
constexpr int TMBOX_STATUS_LED_BLUE = -1;
constexpr bool TMBOX_HAS_STATUS_LED = false;
#endif

#ifdef TMBOX_PROVISION_BUTTON_VALUE
constexpr int TMBOX_PROVISION_BUTTON = TMBOX_PROVISION_BUTTON_VALUE;
constexpr uint32_t TMBOX_PROVISION_HOLD_MS = TMBOX_PROVISION_HOLD_MS_VALUE;
constexpr bool TMBOX_HAS_PROVISION_BUTTON = true;
#else
constexpr int TMBOX_PROVISION_BUTTON = -1;
constexpr uint32_t TMBOX_PROVISION_HOLD_MS = 0;
constexpr bool TMBOX_HAS_PROVISION_BUTTON = false;
#endif
