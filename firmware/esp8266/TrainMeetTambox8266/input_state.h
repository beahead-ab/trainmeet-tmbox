#pragma once
#include <stdint.h>

// No Arduino dependency: debounce, multi-key rejection and stale-input rules
// are also exercised by the host tests. Unsigned subtraction survives millis wrap.
struct KeyEvent { char pressed = 0; bool reset = false; };
class KeyState {
 public:
  void requireRelease() { blocked = true; candidate = 0; stable = 0; }
  KeyEvent update(uint16_t mask, bool valid, uint32_t now) {
    if (!valid || (mask && (mask & (mask - 1)))) requireRelease();
    if (!valid) return {};
    if (blocked) {
      if (mask) { releasedAt = now; return {}; }
      if (uint32_t(now - releasedAt) < 35) return {};
      blocked = false;
    }
    if (candidate != mask) { candidate = mask; changedAt = now; }
    if (uint32_t(now - changedAt) < 35) return {};
    if (stable != mask) {
      // Switching directly between two keys requires a full release first.
      if (stable && mask) { requireRelease(); return {}; }
      stable = mask; heldAt = now; holdSent = false;
      if (stable) {
        const char keys[] = "123A456B789C*0#D";
        for (int i = 0; i < 16; ++i) if (stable == (1u << i)) return {keys[i], false};
      }
    }
    if (stable == (1u << 12) && !holdSent && uint32_t(now - heldAt) >= 5000) {
      holdSent = true; return {0, true};
    }
    return {};
  }
 private:
  uint16_t candidate = 0, stable = 0;
  uint32_t changedAt = 0, heldAt = 0, releasedAt = 0;
  bool blocked = true, holdSent = false;
};

class InputLease {
 public:
  bool fresh = false, waiting = false;
  uint32_t lastSnapshot = 0, sentAt = 0;
  void clear() { fresh = false; waiting = false; }
  void snapshot(uint32_t now, bool retained) {
    if (!retained) { fresh = true; lastSnapshot = now; }
  }
  void sent(uint32_t now) { waiting = true; sentAt = now; }
  void acknowledged() { waiting = false; fresh = false; }
  bool expired(uint32_t now) const { return fresh && uint32_t(now - lastSnapshot) >= 30000; }
  bool timedOut(uint32_t now) const { return waiting && uint32_t(now - sentAt) >= 5000; }
  bool allowed(uint32_t now) const { return fresh && !waiting && !expired(now); }
};
