#pragma once

#include <cstdint>
#include <string>

namespace tmbox {

enum class ScopePart { Assignment = 0, Config = 1, Snapshot = 2 };

struct WireScope {
  bool present = false;
  bool valid = true;
  uint64_t generation = 0;
  std::string publication;
};

// Three independent retained MQTT topics must describe the same station and
// generation before any local selection can become an operative command.
// No knowledge of trains, Cloud or traffic rules belongs here.
class MeetScope {
 public:
  bool accept(ScopePart part, const WireScope& incoming, const std::string& station) {
    reset_seen_ = false;
    if (!incoming.valid) return reject();
    if (incoming.present) {
      if (!incoming.generation || incoming.publication.empty()) return reject();
      if (scope_.present && incoming.generation < scope_.generation) return reject();
      if (!scope_.present || incoming.generation > scope_.generation) {
        invalidate();
        scope_ = incoming;
      } else if (incoming.publication != scope_.publication) {
        return reject();
      }
    } else if (scope_.present) {
      // Never silently downgrade a modern connection because an old retained
      // packet has no scope. Only a new MQTT connection resets compatibility.
      return reject();
    }
    // An unassignment can announce a newer generation too. Remember its
    // scope before clearing the station so delayed old packets stay invalid.
    if (station.empty()) return reject();

    const int index = static_cast<int>(part);
    if (part == ScopePart::Assignment) {
      for (int i = 0; i < 3; ++i) {
        if (have_[i] && stations_[i] != station) { invalidate(); break; }
      }
    } else if (have_[0] && stations_[0] != station) {
      return reject();
    }
    have_[index] = true;
    stations_[index] = station;
    return true;
  }

  bool ready() const {
    return have_[0] && have_[1] && have_[2]
        && stations_[0] == stations_[1] && stations_[1] == stations_[2];
  }
  const WireScope& value() const { return scope_; }
  bool reset_seen() const { return reset_seen_; }

  void invalidate() {
    for (int i = 0; i < 3; ++i) { have_[i] = false; stations_[i].clear(); }
    reset_seen_ = true;
  }
  void reset() { invalidate(); scope_ = WireScope(); }

 private:
  bool reject() { invalidate(); return false; }
  WireScope scope_;
  bool have_[3] = {false, false, false};
  std::string stations_[3];
  bool reset_seen_ = false;
};

}  // namespace tmbox
