#pragma once
#include "Preferences.h"
#include <array>
struct HostEEPROM {
  inline static std::array<unsigned char, 4096> committed{};
  std::array<unsigned char, 4096> working{};
  void begin(size_t) { working = committed; }
  void end() {}
  template<class T> void get(size_t offset, T& value) { std::memcpy(&value, working.data() + offset, sizeof value); }
  template<class T> void put(size_t offset, const T& value) { std::memcpy(working.data() + offset, &value, sizeof value); }
  bool commit() {
    if (Preferences::failWrites) return false;
    committed = working; ++Preferences::writes; return true;
  }
};
inline HostEEPROM EEPROM;
