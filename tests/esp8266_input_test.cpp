#include <cassert>
#include <iostream>
#include "../firmware/esp8266/TrainMeetTambox8266/input_state.h"
#include "../firmware/esp8266/TrainMeetTambox8266/hardware_profile.h"
#include "../firmware/esp8266/TrainMeetTambox8266/device_settings.h"
#include "../firmware/esp8266/TrainMeetTambox8266/network_setup.h"
#include "../firmware/esp8266/TrainMeetTambox8266/web_test_state.h"

void testWebInput() {
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

struct FakeMDNS {
  int answers = 0;
  int queryService(const char* service, const char* protocol, unsigned timeout) {
    assert(std::string(service) == "tmbox");
    assert(std::string(protocol) == "tcp");
    assert(timeout == 1000);
    return answers;
  }
};

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
  FakeMDNS mdns;
  for (int count : {0, 1, 2}) {
    mdns.answers = count;
    assert(TrainMeetNetwork::queryServers(mdns) == count);
  }
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
  testNetworkSetup();
  testWebInput();
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
