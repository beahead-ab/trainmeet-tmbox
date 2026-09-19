#pragma once
#include <stdint.h>
#include <string.h>

// A TCP/MQTT connection alone does not prove that the server has recorded
// this device. Only a fresh reply to its hello permits HTTP code enrollment.
struct EnrollmentReadiness {
  bool received = false;
  void clear() { received = false; }
  bool ready(bool connected) const { return connected && received; }
  void observe(bool retained, int protocol, const char* actualId,
               const char* expectedId, const char* status) {
    if (!retained && protocol == 1 && actualId && expectedId && *expectedId &&
        strcmp(actualId, expectedId) == 0 && status &&
        (strcmp(status, "assigned") == 0 || strcmp(status, "waiting_for_assignment") == 0)) {
      received = true;
    }
  }
};

// No Arduino dependency: authorization lifetime and input source selection
// are tested on the host as well as compiled for the NodeMCU.
struct WebTestSession {
  static constexpr uint32_t IDLE_MS = 10 * 60 * 1000;
  bool paired = false, enabled = false;
  uint32_t activityAt = 0;
  void pair(uint32_t now) { paired = true; enabled = false; activityAt = now; }
  bool valid(uint32_t now) const { return paired && uint32_t(now - activityAt) < IDLE_MS; }
  void touch(uint32_t now) { activityAt = now; }
  bool enable(uint32_t now) {
    if (!valid(now)) return false;
    enabled = true; touch(now); return true;
  }
  void clear() { paired = false; enabled = false; }
  bool permits(bool virtualKey, bool hardwarePresent, uint32_t now) const {
    return virtualKey ? (enabled && valid(now)) : (!enabled && hardwarePresent);
  }
};

struct PairingThrottle {
  unsigned failures = 0;
  uint32_t failedAt = 0;
  bool blocked(uint32_t now) const { return failures >= 5 && uint32_t(now - failedAt) < 60000; }
  void failed(uint32_t now) {
    if (uint32_t(now - failedAt) >= 60000) failures = 0;
    if (failures < 5) ++failures;
    failedAt = now;
  }
};

inline bool hasWebTestCookie(const char* cookies, const char* token) {
  if (!cookies || !token || strlen(token) != 32) return false;
  while (*cookies) {
    while (*cookies == ' ' || *cookies == ';') ++cookies;
    const char* end = strchr(cookies, ';');
    if (!end) end = cookies + strlen(cookies);
    if (end - cookies == 40 && strncmp(cookies, "tm_test=", 8) == 0 &&
        strncmp(cookies + 8, token, 32) == 0) return true;
    cookies = end;
  }
  return false;
}
