#include <cassert>
#include <iostream>
#include "../firmware/esp8266/TrainMeetTambox8266/input_state.h"
#include "../firmware/esp8266/TrainMeetTambox8266/hardware_profile.h"
#include "../firmware/esp8266/TrainMeetTambox8266/device_settings.h"

int main() {
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
}
