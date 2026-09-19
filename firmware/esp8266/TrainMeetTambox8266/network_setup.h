#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>

namespace TrainMeetNetwork {

// The server and ESP32 use _tmbox._tcp. This is NOT the MQTT topic prefix
// (tambox/v1), which is deliberately unchanged for the passive v1 client.
constexpr char DISCOVERY_SERVICE[] = "tmbox";
constexpr uint16_t DEFAULT_HTTP_PORT = 8787;
constexpr uint16_t DEFAULT_MQTT_PORT = 1883;

struct ServerAdvertisement {
  const char* address;
  const char* hostname;
  uint16_t mqttPort;
};

// mDNS names are case-insensitive and may include a trailing dot or .local.
inline size_t hostLength(const char* value) {
  size_t length = strlen(value);
  if (length && value[length - 1] == '.') --length;
  if (length >= 6) {
    const char* suffix = value + length - 6;
    const char expected[] = ".local";
    bool local = true;
    for (size_t i = 0; i < 6; ++i) local &= tolower(static_cast<unsigned char>(suffix[i])) == expected[i];
    if (local) length -= 6;
  }
  return length;
}

inline bool sameHost(const char* a, const char* b) {
  const size_t length = hostLength(a);
  if (!length || length != hostLength(b)) return false;
  for (size_t i = 0; i < length; ++i) {
    if (tolower(static_cast<unsigned char>(a[i])) != tolower(static_cast<unsigned char>(b[i]))) return false;
  }
  return true;
}

// Return -1 for no match, -2 for genuinely different matching servers.
// Repeated advertisements of the same endpoint are not extra servers.
inline int selectServer(const ServerAdvertisement* candidates, size_t count, const char* configuredHost) {
  int selected = -1;
  for (size_t i = 0; i < count; ++i) {
    const auto& candidate = candidates[i];
    if (!candidate.mqttPort || !candidate.address[0] || strcmp(candidate.address, "0.0.0.0") == 0) continue;
    if (configuredHost[0] && !sameHost(configuredHost, candidate.address) && !sameHost(configuredHost, candidate.hostname)) continue;
    if (selected >= 0) {
      const auto& previous = candidates[selected];
      if (strcmp(previous.address, candidate.address) != 0 || previous.mqttPort != candidate.mqttPort) return -2;
    } else selected = int(i);
  }
  return selected;
}

template <class Responder>
int queryServers(Responder& responder) {
  return responder.queryService(DISCOVERY_SERVICE, "tcp", 1000);
}

// WiFiManager 2.0.17 may already have destroyed its HTTP server inside
// process() after a successful Wi-Fi save. stopConfigPortal() is not
// idempotent in that version: never call it on an inactive portal.
// Keep setup available if the new Wi-Fi connection failed.
template <class Manager>
bool finishSavedPortal(Manager& manager, bool wifiConnected) {
  if (wifiConnected && manager.getConfigPortalActive()) {
    manager.stopConfigPortal();
  }
  return manager.getConfigPortalActive();
}

} // namespace TrainMeetNetwork
