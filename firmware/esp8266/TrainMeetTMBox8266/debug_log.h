#pragma once
#include <stdint.h>

// Include after Arduino.h and hardware_profile.h. No String allocation and
// no global DEBUG macro that could collide with the core or a library.

// Arduino's Debug port is undefined when Disabled, or a HardwareSerial object
// when enabled. Resolve its presence at compile time, never as a runtime bool.
// Keep explicit source/build overrides compatible with existing PlatformIO use.
#ifndef TAMBOX_DEBUG_ENABLED
#ifdef DEBUG_ESP_PORT
#define TAMBOX_DEBUG_ENABLED 1
#else
#define TAMBOX_DEBUG_ENABLED 0
#endif
#endif
#if TAMBOX_DEBUG_ENABLED != 0 && TAMBOX_DEBUG_ENABLED != 1
#error "TAMBOX_DEBUG_ENABLED must be 0 or 1"
#endif

#if TAMBOX_DEBUG_ENABLED
namespace TrainMeetDebug {
// Keep malformed/repeated network messages from flooding USB and the loop.
inline bool allowVerbose(uint32_t now) {
  static uint32_t window = 0;
  static unsigned count = 0;
  if (uint32_t(now - window) >= 1000) { window = now; count = 0; }
  if (count >= 20) return false;
  ++count;
  return true;
}
}
// TMBox diagnostics and the boot-only web-test code stay on USB Serial,
// regardless of the port chosen for the ESP8266 core's own diagnostics.
#define TMBOX_LOG(format, ...) do { \
  Serial.printf("[%10lu ms] %-22s : (%u) ", static_cast<unsigned long>(millis()), __func__, unsigned(__LINE__)); \
  Serial.printf(format, ##__VA_ARGS__); \
} while (0)
#define TMBOX_DEBUG(format, ...) do { \
  if (TrainMeetDebug::allowVerbose(uint32_t(millis()))) { TMBOX_LOG(format, ##__VA_ARGS__); } \
} while (0)
#else
#define TMBOX_LOG(format, ...) do { Serial.printf(format, ##__VA_ARGS__); } while (0)
// No evaluation of arguments or clock reads when debug is disabled.
#define TMBOX_DEBUG(...) do {} while (0)
#endif
