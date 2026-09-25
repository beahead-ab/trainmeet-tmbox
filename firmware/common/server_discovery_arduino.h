#pragma once
#include "server_discovery.h"

namespace TrainMeetNetwork {
// ESP8266 exposes TXT only on its dynamic-query API. Release each bounded
// query after copying the same mDNS service records used by ESP32.
inline std::vector<Server> discoverServers() {
  std::vector<Server> result;
#ifdef ESP8266
  const auto query = MDNS.installServiceQuery(DISCOVERY_SERVICE, "tcp",
    [](const MDNSResponder::MDNSServiceInfo&, MDNSResponder::AnswerType, bool) {});
  if (!query) return result;
  const uint32_t started = millis();
  while (uint32_t(millis() - started) < 1000) { MDNS.update(); delay(10); }
  const size_t count = MDNS.answerCount(query);
  for (size_t i=0; i<count && i<=MAX_SERVERS; ++i)
    result.push_back({serverIdFromTxt(MDNS.answerTxts(query, i)),
      MDNS.answerIP4Address(query, i, 0).toString().c_str(), MDNS.answerPort(query, i)});
  MDNS.removeServiceQuery(query);
#else
  const int count = MDNS.queryService(DISCOVERY_SERVICE, "tcp");
  for (int i=0; i<count && i<=int(MAX_SERVERS); ++i)
    result.push_back({MDNS.txt(i, "server_id").c_str(), MDNS.IP(i).toString().c_str(), MDNS.port(i)});
#endif
  return result;
}
} // namespace TrainMeetNetwork
