#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

// Fixed-size and independent of Arduino: also the on-flash format.
namespace TrainMeetWiFi {
constexpr size_t MAX_NETWORKS = 5;
constexpr uint32_t ATTEMPT_MS = 15000;
struct Network { char ssid[33]; char password[65]; };
struct Record {
  uint32_t magic;
  Network networks[MAX_NETWORKS];
  uint8_t lastSuccess; // slot + 1; zero means no successful network yet
  uint8_t reserved;
  uint32_t checksum;
};
constexpr uint32_t MAGIC = 0x544d5701; // TMW, format 1
inline uint32_t checksum(const Record& r) {
  uint32_t hash = 2166136261u;
  const auto* bytes = reinterpret_cast<const uint8_t*>(&r);
  for (size_t i = 0; i < offsetof(Record, checksum); ++i) hash = (hash ^ bytes[i]) * 16777619u;
  return hash;
}
inline void seal(Record& r) { r.magic = MAGIC; r.checksum = checksum(r); }
inline bool valid(const Record& r) {
  if (r.magic != MAGIC || r.checksum != checksum(r)) return false;
  for (const auto& n : r.networks)
    if (!std::memchr(n.ssid, 0, sizeof n.ssid) || !std::memchr(n.password, 0, sizeof n.password)) return false;
  return true;
}
inline bool set(Network& n, const char* ssid, const char* password) {
  if (!ssid || !password || std::strlen(ssid) > 32 || std::strlen(password) > 64) return false;
  n = Network{};
  std::strcpy(n.ssid, ssid);
  if (*ssid) std::strcpy(n.password, password);
  return true;
}
// The former reserved byte keeps the existing 500-byte format compatible.
inline int lastSuccessful(const Record& r) {
  const int slot = int(r.lastSuccess) - 1;
  return slot >= 0 && slot < int(MAX_NETWORKS) && r.networks[slot].ssid[0] ? slot : -1;
}
inline bool rememberSuccessful(Record& r, int slot) {
  if (slot < 0 || slot >= int(MAX_NETWORKS) || !r.networks[slot].ssid[0] || lastSuccessful(r) == slot) return false;
  r.lastSuccess = uint8_t(slot + 1); seal(r); return true;
}
inline void carrySuccessful(Record& next, const Record& old) {
  next.lastSuccess = 0;
  const int previous = lastSuccessful(old);
  if (previous < 0) return;
  for (size_t i = 0; i < MAX_NETWORKS; ++i)
    if (!std::strcmp(next.networks[i].ssid, old.networks[previous].ssid) &&
        !std::strcmp(next.networks[i].password, old.networks[previous].password)) {
      next.lastSuccess = uint8_t(i + 1); return;
    }
}
// A blank password keeps the old password by SSID, even after reordering.
// An explicit open-network checkbox clears it. New SSIDs may be open, too.
inline bool edit(Network& n, const Record& old, const char* ssid, const char* password, bool open) {
  if (!ssid || !password) return false;
  if (!open && !*password && *ssid)
    for (const auto& previous : old.networks)
      if (!std::strcmp(previous.ssid, ssid)) return set(n, ssid, previous.password);
  return set(n, ssid, open ? "" : password);
}
// The ordinary WiFiManager form explicitly selects a new first choice.
// Keep every other network; only the fifth is replaced when the list is full.
inline bool prefer(Record& r, const char* ssid, const char* password) {
  Network n{};
  if (!ssid || !*ssid || !set(n, ssid, password)) return false;
  Record preferred{};
  preferred.networks[0] = n;
  size_t next = 1;
  for (const auto& previous : r.networks)
    if (next < MAX_NETWORKS && previous.ssid[0] && std::strcmp(previous.ssid, ssid))
      preferred.networks[next++] = previous;
  carrySuccessful(preferred, r);
  seal(preferred); r = preferred; return true;
}

// Try the last successful slot first, then other slots in the saved order.
// Each configured slot gets one asynchronous attempt; unsigned subtraction
// keeps the deadline correct across millis() wraparound.
class Sequence {
 public:
  enum { Waiting = -1, Portal = -2 };
  void restart() { slot = -1; attempted = 0; online = false; exhausted = false; }
  int currentSlot() const { return slot; }
  int tick(const Record& r, bool connected, uint32_t now) {
    if (connected) { online = true; return Waiting; }
    if (online) restart();
    if (exhausted) return Portal;
    if (slot >= 0 && uint32_t(now - started) < ATTEMPT_MS) return Waiting;
    int next = attempted == 0 ? lastSuccessful(r) : -1;
    if (next < 0) {
      for (size_t i = 0; i < MAX_NETWORKS; ++i)
        if (r.networks[i].ssid[0] && !(attempted & (1u << i))) { next = int(i); break; }
    }
    if (next < 0) { exhausted = true; return Portal; }
    slot = next; attempted |= uint8_t(1u << slot);
    started = now; return slot;
  }
 private:
  int slot = -1;
  uint8_t attempted = 0;
  uint32_t started = 0;
  bool online = false, exhausted = false;
};
} // namespace TrainMeetWiFi
