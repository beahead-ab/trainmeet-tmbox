#pragma once
#include <stdint.h>

// NodeMCU 1.0 / ESP-12E or ESP-12F, 4 MB flash. These are GPIO numbers!
constexpr uint8_t TAMBOX_SDA = 4; // D2
constexpr uint8_t TAMBOX_SCL = 5; // D1
#ifndef TAMBOX_LCD_ADDRESS
#define TAMBOX_LCD_ADDRESS 0x27
#endif
#ifndef TAMBOX_KEYPAD_ADDRESS
#define TAMBOX_KEYPAD_ADDRESS 0x20
#endif
// PCF8574: P0..P3 = R1..R4, P4..P7 = C1..C4. Change these arrays to match
// the actual module. Values are PCF pin numbers, NOT NodeMCU GPIO numbers.
constexpr uint8_t TAMBOX_KEYPAD_ROWS[4] = {0, 1, 2, 3};
constexpr uint8_t TAMBOX_KEYPAD_COLS[4] = {4, 5, 6, 7};
static_assert(TAMBOX_LCD_ADDRESS != TAMBOX_KEYPAD_ADDRESS, "LCD and keypad need different I2C addresses");
constexpr char TAMBOX_KEYS[] = "123A456B789C*0#D";
constexpr char TAMBOX_FIRMWARE_VERSION[] = "0.3.2";
constexpr char TAMBOX_MODEL[] = "NodeMCU ESP8266 PCF8574";
