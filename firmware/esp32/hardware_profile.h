#pragma once

#include <Arduino.h>

// Select with -DTAMBOX_HARDWARE_PROFILE=1, 2 or 3.
// Profile 1 matches the pin layout in the existing TrainMeet/Benny sketch.
#ifndef TAMBOX_HARDWARE_PROFILE
#define TAMBOX_HARDWARE_PROFILE 1
#endif

#if TAMBOX_HARDWARE_PROFILE == 1

#define TAMBOX_MODEL_NAME "Tambox ESP32 Benny"
constexpr uint8_t TAMBOX_LCD_SDA = 21;
constexpr uint8_t TAMBOX_LCD_SCL = 22;
constexpr uint8_t TAMBOX_ROW_PINS[4] = {13, 12, 14, 27};
constexpr uint8_t TAMBOX_COL_PINS[4] = {26, 25, 33, 32};

#elif TAMBOX_HARDWARE_PROFILE == 2

#define TAMBOX_MODEL_NAME "Tambox ESP32-S3"
constexpr uint8_t TAMBOX_LCD_SDA = 8;
constexpr uint8_t TAMBOX_LCD_SCL = 9;
constexpr uint8_t TAMBOX_ROW_PINS[4] = {4, 5, 6, 7};
constexpr uint8_t TAMBOX_COL_PINS[4] = {15, 16, 17, 18};

#elif TAMBOX_HARDWARE_PROFILE == 3

// Recommended for a newly wired classic ESP32. It avoids GPIO12, which is a
// boot strapping pin on the original ESP32 and can stop some modules booting
// if a keypad key is held while power is applied.
#define TAMBOX_MODEL_NAME "Tambox ESP32 Safe"
constexpr uint8_t TAMBOX_LCD_SDA = 21;
constexpr uint8_t TAMBOX_LCD_SCL = 22;
constexpr uint8_t TAMBOX_ROW_PINS[4] = {13, 23, 14, 27};
constexpr uint8_t TAMBOX_COL_PINS[4] = {26, 25, 33, 32};

#else
#error "Unknown TAMBOX_HARDWARE_PROFILE"
#endif

#ifndef TAMBOX_LCD_ADDRESS_VALUE
#define TAMBOX_LCD_ADDRESS_VALUE 0x27
#endif

constexpr uint8_t TAMBOX_LCD_ADDRESS = TAMBOX_LCD_ADDRESS_VALUE;
constexpr uint8_t TAMBOX_LCD_COLUMNS = 16;
constexpr uint8_t TAMBOX_LCD_ROWS = 2;
