#include <cassert>
#include <iostream>
#include "../firmware/esp8266/TrainMeetTambox8266/input_state.h"
#include "../firmware/esp8266/TrainMeetTambox8266/hardware_profile.h"
#include "../firmware/esp8266/TrainMeetTambox8266/device_settings.h"
#include "../firmware/esp8266/TrainMeetTambox8266/network_setup.h"
#include "../firmware/esp8266/TrainMeetTambox8266/web_test_state.h"
#include "../firmware/esp8266/TrainMeetTambox8266/server_sync.h"

void testServerSync() {
  ServerSync sync;
  assert(sync.next(100, false) == ServerSync::Assignment);
  sync.sent(ServerSync::Assignment, 100);
  assert(sync.next(5099, true) == ServerSync::None);
  assert(sync.next(5100, false) == ServerSync::Assignment); // Lost hello/reply.
  sync.sent(ServerSync::Assignment, 5100);
  sync.assignmentReceived(5200);
  assert(sync.next(5200, true) == ServerSync::State);
  sync.sent(ServerSync::State, 5200);
  sync.stateReceived(5300);
  InputLease lease;
  lease.snapshot(5300, false);
  // A minute of idle operation: six liveness checks, zero assignments or
  // replacement snapshots. Both assigned and waiting devices follow this.
  for (uint32_t now = 15300; now <= 65300; now += 10000) {
    assert(sync.next(now - 1, false) == ServerSync::None);
    assert(sync.next(now, false) == ServerSync::State);
    sync.sent(ServerSync::State, now);
    sync.stateReceived(now);
    assert(lease.heartbeat(now));
    assert(lease.allowed(now));
  }
  assert(sync.next(65301, true) == ServerSync::State); // Explicit refresh, not hello.
  sync.sent(ServerSync::State, 65301);
  assert(sync.next(70301, false) == ServerSync::State); // Lost status response.
  sync.assignmentReceived(70302); // Admin pushes a new assignment immediately.
  assert(sync.next(70302, true) == ServerSync::State);
  sync.reset(); // Disconnect or gateway restart.
  assert(sync.next(70303, false) == ServerSync::Assignment);
  sync.sent(ServerSync::Assignment, 0xfffffff0u);
  assert(sync.next(10, false) == ServerSync::None);
  assert(sync.next(5000, false) == ServerSync::Assignment);
  sync.assignmentReceived(0xfffffff0u);
  assert(sync.next(10, false) == ServerSync::None);
  assert(sync.next(10000, false) == ServerSync::State);

  lease.clear(); assert(!lease.heartbeat(1));
  lease.snapshot(10, false);
  lease.sent(20); assert(!lease.heartbeat(30)); // Does not ACK a traffic command.
  lease.acknowledged(); assert(!lease.heartbeat(40));
  lease.snapshot(50, false); assert(!lease.heartbeat(30050)); // Cannot revive stale state.
  lease.clear(); assert(!lease.heartbeat(30051));
}

void testWebInput() {
  LocalTrainEntry entry;
  assert(!entry.replace("context", "93"));
  entry.sync(true, "context", "");
  assert(entry.replace("context", "93"));
  assert(entry.value == "93");
  for (const auto& bad : {"", "123456", "9X", "-93"}) assert(!entry.replace("context", bad));
  assert(!entry.replace("stale-context", "12"));
  assert(entry.value == "93");
  entry.clear();
  assert(!entry.replace("context", "93"));
  EnrollmentReadiness registration;
  assert(!registration.ready(true));
  registration.observe(true, 1, "box-1", "box-1", "assigned");
  assert(!registration.ready(true)); // Retained replay is not a current hello.
  registration.observe(false, 1, "box-2", "box-1", "assigned");
  registration.observe(false, 2, "box-1", "box-1", "assigned");
  registration.observe(false, 1, "box-1", "box-1", "unknown");
  registration.observe(false, 1, "", "", "assigned");
  registration.observe(false, 1, nullptr, "box-1", "assigned");
  assert(!registration.ready(true));
  registration.observe(false, 1, "box-1", "box-1", "waiting_for_assignment");
  assert(registration.ready(true)); // Registration does not assign a station.
  assert(!registration.ready(false));
  registration.clear();
  assert(!registration.ready(true)); // Server changes/reconnects require new hello.
  registration.observe(false, 1, "box-1", "box-1", "assigned");
  assert(registration.ready(true));

  WebTestSession test;
  assert(!test.enable(100));
  assert(!test.permits(true, false, 100));
  assert(!test.permits(false, false, 100));
  assert(test.permits(false, true, 100));
  test.pair(100);
  assert(!test.permits(true, true, 100)); // Pairing alone is not test mode.
  assert(test.enable(200));
  assert(test.permits(true, false, 200)); // No hardware in explicit test mode.
  assert(!test.permits(false, true, 200)); // One input source at a time.
  assert(test.valid(600199));
  assert(!test.valid(600200));
  assert(!test.permits(true, true, 600200));
  test.clear();
  assert(!test.permits(true, true, 600201));
  assert(test.permits(false, true, 600201));
  test.pair(0xfffffff0u); assert(test.enable(10));
  assert(test.valid(600009)); assert(!test.valid(600010));
  test.clear(); // A new boot never restores an enabled test session.
  assert(!test.paired && !test.enabled);

  const char token[] = "0123456789abcdef0123456789abcdef";
  assert(hasWebTestCookie((std::string("tm_test=") + token).c_str(), token));
  assert(hasWebTestCookie((std::string("x=1; tm_test=") + token + "; y=2").c_str(), token));
  assert(!hasWebTestCookie((std::string("other_tm_test=") + token).c_str(), token));
  assert(!hasWebTestCookie((std::string("tm_test=") + token + "x").c_str(), token));
  assert(!hasWebTestCookie("tm_test=", ""));
  assert(!hasWebTestCookie("tm_test=wrong", token));
  assert(!hasWebTestCookie(" ; ", token));
  PairingThrottle throttle;
  for (unsigned i = 0; i < 5; ++i) { assert(!throttle.blocked(100+i)); throttle.failed(100+i); }
  assert(throttle.blocked(105)); assert(throttle.blocked(60103));
  assert(!throttle.blocked(60104));
  throttle.failed(60104); assert(!throttle.blocked(60104));
}

struct FakePortal {
  bool active;
  unsigned stops = 0;
  bool getConfigPortalActive() const { return active; }
  void stopConfigPortal() {
    assert(active); // In WiFiManager 2.0.17 a second shutdown dereferences null.
    active = false; ++stops;
  }
};

void testNetworkSetup() {
  using namespace TrainMeetNetwork;
  std::vector<Server> servers{{"one", "192.168.0.200", 1883}, {"two", "192.168.0.160", 1884}};
  assert(selectServer({}, "").status == DiscoveryStatus::Missing);
  assert(selectServer(servers, "").status == DiscoveryStatus::Ambiguous);
  assert(selectServer(servers, "one").index == 0);
  assert(selectServer(servers, "two").index == 1);
  assert(selectServer(servers, "absent").index == -1);
  servers.erase(servers.begin());
  assert(selectServer(servers, "").index == 0);
  assert(servers[0].port == 1884);
  servers[0].host = "192.168.0.99"; // DHCP change, same installation.
  assert(selectServer(servers, "two").index == 0);
  servers.push_back(servers[0]);
  assert(selectServer(servers, "two").index == 0); // Duplicate advertisement.
  servers.back().host = "192.168.0.100";
  assert(selectServer(servers, "two").status == DiscoveryStatus::Ambiguous);
  assert(selectServer({{"", "192.168.0.160", 1883}}, "").index == -1);
  assert(selectServer({{"one", "0.0.0.0", 1883}, {"two", "192.168.0.160", 0}}, "").index == -1);
  assert(selectServer(std::vector<Server>(MAX_SERVERS + 1, {"one", "192.168.0.160", 1883}), "one").index == -1);
  assert(serverIdFromTxt("protocol=2;server_id=abcd-1234") == "abcd-1234");
  assert(serverIdFromTxt("server_id=one;protocol=2") == "one");
  assert(serverIdFromTxt(nullptr).empty());
  assert(serverIdFromTxt("server_id=<invalid>").empty());
  auto record = bindingRecord("one");
  assert(bindingId(record) == "one");
  record.id[0] = 'x'; assert(bindingId(record).empty());
  assert(bindingId(bindingRecord("")).empty());
  assert(bindingId(bindingRecord(std::string(97, 'x'))).empty());
  // process() already closed the portal after a successful Wi-Fi connection.
  FakePortal automatic{false};
  assert(!TrainMeetNetwork::finishSavedPortal(automatic, true));
  assert(automatic.stops == 0);
  // A settings-only save while connected closes an active portal once.
  FakePortal manual{true};
  assert(!TrainMeetNetwork::finishSavedPortal(manual, true));
  assert(!TrainMeetNetwork::finishSavedPortal(manual, true));
  assert(manual.stops == 1);
  // Wrong password / connection failure must leave setup available.
  FakePortal failed{true};
  assert(TrainMeetNetwork::finishSavedPortal(failed, false));
  assert(failed.stops == 0);
  assert(!TrainMeetNetwork::finishSavedPortal(failed, true));
  assert(failed.stops == 1);
  FakePortal exited{false};
  assert(!TrainMeetNetwork::finishSavedPortal(exited, false));
  assert(exited.stops == 0);
}

int main() {
  LocalTrainEntry entry;
  assert(!entry.digit('4') && !entry.canSubmit());
  entry.sync(true, "meet|panel|A|owner|", "");
  assert(entry.active && !entry.canSubmit());
  for (char digit : std::string("00421")) assert(entry.digit(digit));
  assert(entry.canSubmit() && entry.value == "00421");
  assert(!entry.digit('9') && !entry.digit('A'));
  entry.sync(true, "meet|panel|A|owner|", ""); // Clock/unrelated revisions do not erase digits.
  assert(entry.value == "00421");
  entry.sync(true, "meet|panel|B|owner|", "");
  assert(entry.value.empty()); // Never move a draft to a different connection.
  assert(entry.digit('7'));
  entry.sync(false, "old-server-or-wrong-owner", "");
  assert(!entry.active && entry.value.empty());
  entry.sync(true, "new-meet", "123456"); assert(!entry.active);
  entry.sync(true, "new-meet", "x"); assert(!entry.active);
  entry.sync(true, "new-meet", "42"); assert(entry.value == "42");
  entry.clear(); assert(!entry.active && !entry.canSubmit());
  testNetworkSetup();
  testWebInput();
  testServerSync();
  const char expected[] = "123A456B789C*0#D";
  for (unsigned index = 0; index < 16; ++index) {
    KeyState state;
    state.update(0, true, 40);
    assert(state.update(1u << index, true, 50).pressed == 0);
    assert(state.update(1u << index, true, 84).pressed == 0);
    assert(state.update(1u << index, true, 85).pressed == expected[index]);
    assert(state.update(1u << index, true, 200).pressed == 0);
    state.update(0, true, 210); state.update(0, true, 250);
    state.update(1u << index, true, 260);
    assert(state.update(1u << index, true, 300).pressed == expected[index]);
  }
  KeyState held;
  held.update(1, true, 100); assert(!held.update(1, true, 6000).pressed);
  held.update(0, true, 6010); held.update(0, true, 6050);
  held.update(1, true, 6060); assert(held.update(1, true, 6100).pressed == '1');
  held.requireRelease(); assert(!held.update(1, true, 9000).pressed);

  KeyState multiple;
  multiple.update(0, true, 40); multiple.update(3, true, 50);
  assert(!multiple.update(1, true, 500).pressed);
  multiple.update(0, true, 510); multiple.update(0, true, 550);
  multiple.update(1, true, 560); assert(multiple.update(1, true, 600).pressed == '1');
  multiple.update(0, false, 610); assert(!multiple.update(1, true, 1000).pressed);

  KeyState reset;
  reset.update(0, true, 40); reset.update(1u << 12, true, 50);
  assert(reset.update(1u << 12, true, 90).pressed == '*');
  assert(!reset.update(1u << 12, true, 5089).reset);
  assert(reset.update(1u << 12, true, 5090).reset);
  assert(!reset.update(1u << 12, true, 9999).reset);

  InputLease lease;
  assert(!lease.allowed(100));
  lease.snapshot(100, true); assert(!lease.allowed(100));
  lease.snapshot(100, false); assert(lease.allowed(100));
  lease.sent(110); assert(!lease.allowed(110));
  lease.snapshot(120, false); assert(!lease.allowed(120)); // Snapshot is not an acknowledgement.
  lease.acknowledged(); assert(!lease.allowed(130));
  lease.snapshot(140, false); assert(lease.allowed(140));
  assert(!lease.expired(30139)); assert(lease.expired(30140));
  lease.clear(); assert(!lease.allowed(30141));
  lease.snapshot(0xfffffff0u, false); assert(lease.allowed(10)); // millis wrap.
  lease.sent(0xfffffff0u); assert(!lease.timedOut(10)); assert(lease.timedOut(5000));
  unsigned pins = 0;
  for (unsigned i = 0; i < 4; ++i) {
    // Default PCF8574 wiring: columns on P0..P3, rows on P4..P7.
    assert(TAMBOX_KEYPAD_ROWS[i] == i + 4);
    assert(TAMBOX_KEYPAD_COLS[i] == i);
    assert(TAMBOX_KEYPAD_ROWS[i] < 8 && TAMBOX_KEYPAD_COLS[i] < 8);
    pins |= 1u << TAMBOX_KEYPAD_ROWS[i]; pins |= 1u << TAMBOX_KEYPAD_COLS[i];
  }
  assert(pins == 0xff);
  assert(TAMBOX_SDA == 4 && TAMBOX_SCL == 5);
  Settings settings{}; assert(!validSettings(settings));
  settings.magic = 0x544d3836; settings.port = 1883;
  settings.checksum = settingsChecksum(settings); assert(validSettings(settings));
  settings.port = 1884; assert(!validSettings(settings));
  settings.checksum = settingsChecksum(settings); assert(validSettings(settings));
  settings.host[63] = 'x'; settings.checksum = settingsChecksum(settings); assert(!validSettings(settings));
  std::cout << "ESP8266: all 16 keys, debounce, held-key/reset, I2C failure, stale input, ACK and millis wrap passed\n";
  std::cout << "ESP8266: discovery contract and portal shutdown/retry cases passed\n";
  std::cout << "ESP8266: web pairing, idle expiry, input exclusivity and cookie/throttle checks passed\n";
}
