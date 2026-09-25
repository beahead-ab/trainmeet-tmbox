#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace TrainMeetNetwork {
constexpr char DISCOVERY_SERVICE[] = "tmbox";
constexpr size_t MAX_SERVERS = 16;
struct Server { std::string id, host; uint16_t port; };
enum class DiscoveryStatus { Ready, Missing, Ambiguous };
struct Selection {
  int index;
  DiscoveryStatus status;
  Selection(int selected = -1, DiscoveryStatus state = DiscoveryStatus::Missing) : index(selected), status(state) {}
};

inline bool validServerId(const std::string& id) {
  if (id.empty() || id.size() > 96) return false;
  for (char c : id)
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) return false;
  return true;
}

// A remembered identity may move to a new IP, but never fall back to another
// server. A new box must see exactly one endpoint. Conflicting advertisements
// (even with the same ID) require the administrator to resolve the ambiguity.
inline Selection selectServer(const std::vector<Server>& servers, const std::string& remembered) {
  Selection result;
  if (servers.size() > MAX_SERVERS) return {-1, DiscoveryStatus::Ambiguous};
  for (size_t i = 0; i < servers.size(); ++i) {
    const auto& server = servers[i];
    if (server.host.empty() || server.host == "0.0.0.0" || !server.port) continue;
    if (!remembered.empty() && server.id != remembered) continue;
    if (result.index >= 0) {
      const auto& first = servers[result.index];
      if (first.id != server.id || first.host != server.host || first.port != server.port)
        return {-1, DiscoveryStatus::Ambiguous};
    } else result.index = int(i);
  }
  if (result.index >= 0) {
    if (!validServerId(servers[result.index].id)) result.index = -1;
    else result.status = DiscoveryStatus::Ready;
  }
  return result;
}

inline std::string serverIdFromTxt(const char* txt) {
  if (!txt) return {};
  std::string value(txt);
  if (value.size() > 1300) return {};
  for (size_t start = 0; start < value.size();) {
    const size_t end = value.find(';', start);
    const auto item = value.substr(start, end == std::string::npos ? end : end-start);
    if (item.compare(0, 10, "server_id=") == 0) {
      const auto id = item.substr(10);
      return validServerId(id) ? id : std::string();
    }
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return {};
}

// Separate from legacy host settings and the language cache in ESP8266 EEPROM.
struct ServerBindingRecord { uint32_t magic; char id[97]; uint8_t reserved[3]; uint32_t checksum; };
inline uint32_t bindingChecksum(const ServerBindingRecord& record) {
  uint32_t hash = 2166136261u;
  const auto* bytes = reinterpret_cast<const uint8_t*>(&record);
  for (size_t i=0; i<offsetof(ServerBindingRecord, checksum); ++i) hash = (hash ^ bytes[i]) * 16777619u;
  return hash;
}
inline ServerBindingRecord bindingRecord(const std::string& id) {
  ServerBindingRecord record{};
  record.magic = 0x544d5349;
  if (validServerId(id)) std::memcpy(record.id, id.data(), id.size());
  record.checksum = bindingChecksum(record);
  return record;
}
inline std::string bindingId(const ServerBindingRecord& record) {
  if (record.magic != 0x544d5349 || record.id[96] != 0 || record.checksum != bindingChecksum(record)) return {};
  const std::string id(record.id);
  return validServerId(id) ? id : std::string();
}

template <class Manager>
bool finishSavedPortal(Manager& manager, bool wifiConnected) {
  if (wifiConnected && manager.getConfigPortalActive()) manager.stopConfigPortal();
  return manager.getConfigPortalActive();
}
} // namespace TrainMeetNetwork
