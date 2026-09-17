#pragma once
#include <Wire.h>
#include "hardware_profile.h"

// A PCF8574 input is released by writing 1, not by changing a direction register.
// Multiple keys are rejected by KeyState, including ambiguous/ghosted matrices.
inline bool keypadWrite(uint8_t value) {
  Wire.beginTransmission(TAMBOX_KEYPAD_ADDRESS);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}
inline bool keypadScan(uint16_t& mask) {
  mask = 0;
  for (uint8_t row = 0; row < 4; ++row) {
    if (!keypadWrite(uint8_t(0xff & ~(1u << TAMBOX_KEYPAD_ROWS[row])))) return false;
    delayMicroseconds(100);
    if (Wire.requestFrom(uint8_t(TAMBOX_KEYPAD_ADDRESS), uint8_t(1)) != 1) {
      keypadWrite(0xff); return false;
    }
    const uint8_t value = Wire.read();
    for (uint8_t col = 0; col < 4; ++col) {
      if (!(value & (1u << TAMBOX_KEYPAD_COLS[col]))) mask |= uint16_t(1u << (row * 4 + col));
    }
  }
  return keypadWrite(0xff);
}
