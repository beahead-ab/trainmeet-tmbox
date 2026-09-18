#pragma once

namespace TrainMeetNetwork {

// The server and ESP32 use _tmbox._tcp. This is NOT the MQTT topic prefix
// (tambox/v1), which is deliberately unchanged for the passive v1 client.
constexpr char DISCOVERY_SERVICE[] = "tmbox";

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
