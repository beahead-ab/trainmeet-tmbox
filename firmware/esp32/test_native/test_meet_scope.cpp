#include "check.h"
#include "meet_scope.h"
#include <algorithm>

using namespace tmbox;

WireScope scope(uint64_t generation, const char* publication = "published") {
  WireScope result;
  result.present = true; result.generation = generation; result.publication = publication;
  return result;
}

void every_order_requires_a_complete_matching_triplet() {
  int order[] = {0, 1, 2};
  do {
    MeetScope state;
    for (int i = 0; i < 3; ++i) {
      check::truthy(state.accept(static_cast<ScopePart>(order[i]), scope(7), "CDA"), "valid packet accepted");
      check::truthy(state.ready() == (i == 2), "all three authoritative packets required");
    }
    check::truthy(state.value().generation == 7, "command scope is the displayed snapshot generation");
  } while (std::next_permutation(order, order + 3));
}

void changed_meet_discards_every_old_topic_and_delayed_packets_never_downgrade() {
  MeetScope state;
  for (int i = 0; i < 3; ++i) state.accept(static_cast<ScopePart>(i), scope(7), "CDA");
  state.accept(ScopePart::Snapshot, scope(8, "new"), "LEK");
  check::truthy(state.reset_seen() && !state.ready(), "new generation requires clearing navigation and in-flight commands");
  check::truthy(!state.accept(ScopePart::Config, scope(7), "CDA"), "late retained old config rejected");
  check::truthy(state.value().generation == 8 && !state.ready(), "old packet cannot restore old scope");
  for (int i = 0; i < 3; ++i) state.accept(static_cast<ScopePart>(i), scope(8, "new"), "LEK");
  check::truthy(state.ready(), "fresh complete triplet restores operation");
  state.accept(ScopePart::Assignment, scope(9, "another"), "");
  check::truthy(state.value().generation == 9 && !state.ready(), "unassignment advances the scope without enabling commands");
  check::truthy(!state.accept(ScopePart::Assignment, scope(8, "new"), "LEK"), "late assignment cannot undo unassignment");
}

void mismatch_and_station_reassignment_fail_closed() {
  MeetScope state;
  for (int i = 0; i < 3; ++i) state.accept(static_cast<ScopePart>(i), scope(7), "CDA");
  check::truthy(!state.accept(ScopePart::Snapshot, scope(7, "other"), "CDA"), "same generation different publication is invalid");
  check::truthy(!state.ready(), "mixed publications cannot send");
  for (int i = 0; i < 3; ++i) state.accept(static_cast<ScopePart>(i), scope(7), "CDA");
  state.accept(ScopePart::Assignment, scope(7), "LEK");
  check::truthy(state.reset_seen() && !state.ready(), "new station invalidates cached station and pending command");
  check::truthy(!state.accept(ScopePart::Config, scope(7), "CDA"), "old station cannot configure the new assignment");
  check::truthy(!state.ready(), "no writes after station mismatch");
}

void legacy_compatibility_is_not_a_silent_downgrade() {
  MeetScope state;
  for (int i = 0; i < 3; ++i) state.accept(static_cast<ScopePart>(i), WireScope(), "CDA");
  check::truthy(state.ready() && !state.value().present, "old servers remain usable without invented scope");
  state.accept(ScopePart::Assignment, scope(1), "CDA");
  check::truthy(!state.accept(ScopePart::Snapshot, WireScope(), "CDA"), "old retained data cannot downgrade a modern connection");
  check::truthy(!state.ready(), "mixed legacy and modern cache is blocked");
  WireScope malformed = scope(1); malformed.valid = false;
  check::truthy(!state.accept(ScopePart::Config, malformed, "CDA"), "malformed generation blocked");
  state.reset();
  for (int i = 0; i < 3; ++i) state.accept(static_cast<ScopePart>(i), WireScope(), "CDA");
  check::truthy(state.ready(), "a new connection can intentionally reach an older server");
}

int main() {
  every_order_requires_a_complete_matching_triplet();
  changed_meet_discards_every_old_topic_and_delayed_packets_never_downgrade();
  mismatch_and_station_reassignment_fail_closed();
  legacy_compatibility_is_not_a_silent_downgrade();
  return check::report();
}
