#pragma once
#include <stdint.h>
#include <stddef.h>

struct Settings {
  uint32_t magic;
  char host[64];
  uint16_t port;
  uint16_t reserved;
  uint32_t checksum;
};
inline uint32_t settingsChecksum(const Settings& value) {
  uint32_t hash = 2166136261u;
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&value);
  for (size_t i = 0; i < offsetof(Settings, checksum); ++i) hash = (hash ^ bytes[i]) * 16777619u;
  return hash;
}
inline bool validSettings(const Settings& value) {
  return value.magic == 0x544d3836 && value.host[63] == 0 && value.port && value.checksum == settingsChecksum(value);
}
