#include "../firmware/common/wifi_profiles.h"
#include <cassert>
#include <string>
#include <iostream>
using namespace TrainMeetWiFi;

int main() {
  static_assert(sizeof(Record) == 500, "stable format, fits before the server binding");
  Record r{};
  assert(!valid(r)); // erased or pre-upgrade storage
  seal(r); assert(valid(r));
  assert(prefer(r, "Home", "home-password")); // migration
  const Record reboot = r; assert(valid(reboot));
  assert(std::string(reboot.networks[0].ssid) == "Home");
  r.networks[0].password[0] ^= 1; assert(!valid(r)); r = reboot;
  Record bad = r; std::memset(bad.networks[0].ssid, 'x', 33); seal(bad); assert(!valid(bad));
  bad = r; std::memset(bad.networks[0].password, 'x', 65); seal(bad); assert(!valid(bad));

  Network n{};
  assert(set(n, std::string(32, 's').c_str(), std::string(64, 'p').c_str()));
  assert(!set(n, std::string(33, 's').c_str(), "password"));
  assert(!set(n, "ssid", std::string(65, 'p').c_str()));
  assert(edit(n, r, "Home", "", false)); assert(std::string(n.password) == "home-password");
  assert(edit(n, r, "Home", "changed-password", false)); assert(std::string(n.password) == "changed-password");
  assert(edit(n, r, "Home", "", true)); assert(!n.password[0]);
  assert(edit(n, r, "Meeting", "", false)); assert(!n.password[0]); // new open network
  assert(edit(n, r, "", "old-password", false)); assert(!n.ssid[0] && !n.password[0]);

  assert(prefer(r, "Two", "password2")); assert(prefer(r, "Three", "password3"));
  assert(prefer(r, "Four", "password4")); assert(prefer(r, "Five", "password5"));
  assert(prefer(r, "Three", "updated-password"));
  assert(std::string(r.networks[0].ssid) == "Three");
  assert(std::string(r.networks[1].ssid) == "Five");
  assert(std::string(r.networks[4].ssid) == "Home"); // duplicate update kept every other network
  assert(prefer(r, "Six", "password6"));
  assert(std::string(r.networks[4].ssid) == "Two"); assert(valid(r)); // full list drops the fifth

  Record holes{};
  set(holes.networks[4], "Keep me", "password"); seal(holes);
  assert(prefer(holes, "New first", "password"));
  assert(std::string(holes.networks[1].ssid) == "Keep me"); // spare slots must not discard the last row

  Sequence sequence;
  assert(sequence.tick(r, false, 0) == 0);
  assert(sequence.tick(r, false, ATTEMPT_MS - 1) == Sequence::Waiting);
  assert(sequence.tick(r, false, ATTEMPT_MS) == 1);
  assert(sequence.tick(r, true, ATTEMPT_MS + 1) == Sequence::Waiting); // second network works
  assert(sequence.tick(r, true, 100000) == Sequence::Waiting); // no periodic switch while online
  assert(sequence.tick(r, false, 100001) == 0); // lost link starts from first choice
  for (int i = 1; i < 5; ++i) assert(sequence.tick(r, false, 100001 + i * ATTEMPT_MS) == i);
  assert(sequence.tick(r, false, 100001 + 5 * ATTEMPT_MS) == Sequence::Portal);
  assert(sequence.tick(r, false, 999999) == Sequence::Portal);

  r = Record{}; seal(r); sequence.restart();
  assert(sequence.tick(r, false, 0) == Sequence::Portal); // no saved networks
  set(r.networks[1], "Meeting", "password"); set(r.networks[4], "Workshop", "");
  sequence.restart();
  assert(sequence.tick(r, false, UINT32_MAX - 100) == 1); // skip empty first slot
  assert(sequence.tick(r, false, 100) == Sequence::Waiting);
  assert(sequence.tick(r, false, ATTEMPT_MS - 101) == 4); // timer rollover
  assert(sequence.tick(r, false, 2 * ATTEMPT_MS - 101) == Sequence::Portal);
  sequence.restart(); assert(sequence.tick(r, false, 10) == 1); // explicit exit/save retries
  std::cout << "Wi-Fi profiles: storage, editing, priority, fallback, reconnect and rollover passed\n";
}
