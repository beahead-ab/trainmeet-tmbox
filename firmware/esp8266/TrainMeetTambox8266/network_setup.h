#pragma once
#include <cstring>

namespace TrainMeetNetwork {

// The server and ESP32 use _tmbox._tcp. This is NOT the MQTT topic prefix
// (tambox/v1), which is deliberately unchanged for the passive v1 client.
constexpr char DISCOVERY_SERVICE[] = "tmbox";

template <class Responder>
int queryServers(Responder& responder) {
  return responder.queryService(DISCOVERY_SERVICE, "tcp", 1000);
}

// No operator chooses a host or port. Keep a known server when possible;
// otherwise use stable ordering of valid advertisements, not reply timing.
template <class Responder, class Host>
int selectServer(Responder& responder, int count, const Host& previous) {
  int selected = -1;
  for (int i = 0; i < count; ++i) {
    const auto host = responder.IP(i).toString();
    if (host == "0.0.0.0" || !responder.port(i)) continue;
    if (host == previous) return i;
    if (selected < 0 || std::strcmp(host.c_str(), responder.IP(selected).toString().c_str()) < 0) selected = i;
  }
  return selected;
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
