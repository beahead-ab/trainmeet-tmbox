#pragma once
#include <stdint.h>

// Assignment is requested once per connection/recovery, never as a heartbeat.
// Missing replies are retried (not traffic commands). Live pushes satisfy a
// pending state request; retained broker data must not call these methods.
class ServerSync {
 public:
  enum Request { None, Assignment, State };
  void reset() { known = false; pending = None; }
  Request next(uint32_t now, bool needsState) const {
    if (pending != None) return uint32_t(now - sentAt) >= 5000 ? pending : None;
    if (!known) return Assignment;
    return needsState || uint32_t(now - seenAt) >= 10000 ? State : None;
  }
  void sent(Request request, uint32_t now) { pending = request; sentAt = now; }
  void assignmentReceived(uint32_t now) {
    known = true; pending = None; seenAt = now;
  }
  void stateReceived(uint32_t now) {
    if (pending == State) pending = None;
    seenAt = now;
  }
 private:
  bool known = false;
  Request pending = None;
  uint32_t sentAt = 0, seenAt = 0;
};
