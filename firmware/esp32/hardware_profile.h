#pragma once

#include <Arduino.h>

// Select with -DTMBOX_HARDWARE_PROFILE=1, 2 or 3.
// Profile 1 matches the pin layout in the existing TrainMeet/Benny sketch.
#ifndef TMBOX_HARDWARE_PROFILE
#define TMBOX_HARDWARE_PROFILE 1
#endif

#if TMBOX_HARDWARE_PROFILE == 1

#define TMBOX_MODEL_NAME "TMBox ESP32 Benny"
constexpr uint8_t TMBOX_LCD_SDA = 21;
constexpr uint8_t TMBOX_LCD_SCL = 22;
constexpr uint8_t TMBOX_ROW_PINS[4] = {13, 12, 14, 27};
constexpr uint8_t TMBOX_COL_PINS[4] = {26, 25, 33, 32};

#elif TMBOX_HARDWARE_PROFILE == 2

#define TMBOX_MODEL_NAME "TMBox ESP32-S3"
constexpr uint8_t TMBOX_LCD_SDA = 8;
constexpr uint8_t TMBOX_LCD_SCL = 9;
constexpr uint8_t TMBOX_ROW_PINS[4] = {4, 5, 6, 7};
constexpr uint8_t TMBOX_COL_PINS[4] = {15, 16, 17, 18};

#elif TMBOX_HARDWARE_PROFILE == 3

// Recommended for a newly wired classic ESP32. It avoids GPIO12, which is a
// boot strapping pin on the original ESP32 and can stop some modules booting
// if a keypad key is held while power is applied.
#define TMBOX_MODEL_NAME "TMBox ESP32 Safe"
constexpr uint8_t TMBOX_LCD_SDA = 21;
constexpr uint8_t TMBOX_LCD_SCL = 22;
constexpr uint8_t TMBOX_ROW_PINS[4] = {13, 23, 14, 27};
constexpr uint8_t TMBOX_COL_PINS[4] = {26, 25, 33, 32};

#else
#error "Unknown TMBOX_HARDWARE_PROFILE"
#endif

#ifndef TMBOX_LCD_ADDRESS_VALUE
#define TMBOX_LCD_ADDRESS_VALUE 0x27
#endif

constexpr uint8_t TMBOX_LCD_ADDRESS = TMBOX_LCD_ADDRESS_VALUE;
constexpr uint8_t TMBOX_LCD_COLUMNS = 16;
constexpr uint8_t TMBOX_LCD_ROWS = 2;

// Attention output. Benny's answer 5.2 decides whether a free GPIO exists for
// a buzzer; until it does, TMBOX_BUZZER_PIN stays unset and the attention
// controller still runs — it just has nothing to drive. Every decision about
// *what* deserves a sound is already made and tested; only the wire is
// missing. Define TMBOX_BUZZER_PIN_VALUE in the build to light it up.
#ifdef TMBOX_BUZZER_PIN_VALUE
constexpr int TMBOX_BUZZER_PIN = TMBOX_BUZZER_PIN_VALUE;
constexpr bool TMBOX_HAS_BUZZER = true;
#else
constexpr int TMBOX_BUZZER_PIN = -1;
constexpr bool TMBOX_HAS_BUZZER = false;
#endif
