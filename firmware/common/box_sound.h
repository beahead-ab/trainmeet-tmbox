#pragma once
#include <stdint.h>

// What a box makes a sound for, and which tone. Shared by ESP8266 and ESP32;
// no Arduino dependency, so the host tests compile it as it is.
//
// The ESP32 decides from the traffic it sees (tmbox_core AttentionController).
// A box on the server's terminal profile sees only a picture, so the server
// says in each picture what is news (docs/protocol/terminal16, section 6a:
// `attention {epoch, seq, kind}`) and the box only decides when to believe it.
// The rules are the ESP32's, and so are the tones:
//   1. Only a change is news. Catching up after a (re)connection is not.
//   2. A sound from before the connection is never played afterwards.
//   3. The box's own actions are silent; the server leaves them out.
//   4. Losing the server outranks everything, because every other event is a
//      claim about state the box can no longer confirm.
// The ESP8266 follows the same policy (issue #44).
namespace TrainMeetSound {

// Descending importance: the first one is the most important.
enum class Kind : uint8_t {
  None,
  ConnectionLost,
  IncomingRequest,
  RequestDenied,
  RequestApproved,
  IncomingTrain,
  ConnectionRestored,
};

struct Tone {
  uint16_t hz;
  uint16_t ms;
};

// The same table as the ESP32's signalAttention.
inline Tone toneFor(Kind kind) {
  switch (kind) {
    case Kind::ConnectionLost:     return {700, 600};
    case Kind::ConnectionRestored: return {1400, 120};
    case Kind::IncomingRequest:    return {2200, 250};
    case Kind::RequestDenied:      return {900, 250};
    case Kind::RequestApproved:    return {2600, 150};
    case Kind::IncomingTrain:      return {1800, 150};
    case Kind::None:               break;
  }
  return {0, 0};
}

// Bigger is more important. None is below everything.
inline uint8_t weight(Kind kind) {
  switch (kind) {
    case Kind::ConnectionLost:     return 6;
    case Kind::IncomingRequest:    return 5;
    case Kind::RequestDenied:      return 4;
    case Kind::RequestApproved:    return 3;
    case Kind::IncomingTrain:      return 2;
    case Kind::ConnectionRestored: return 1;
    case Kind::None:               break;
  }
  return 0;
}

inline Kind louder(Kind a, Kind b) { return weight(b) > weight(a) ? b : a; }

inline bool same(const char* a, const char* b) {
  if (!a || !b) return false;
  while (*a && *a == *b) { ++a; ++b; }
  return *a == *b;
}

// The server's name for an event. Anything else - null, a name from a newer
// server - makes no sound.
inline Kind fromServer(const char* name) {
  if (same(name, "incoming_request")) return Kind::IncomingRequest;
  if (same(name, "request_denied"))   return Kind::RequestDenied;
  if (same(name, "request_approved")) return Kind::RequestApproved;
  if (same(name, "incoming_train"))   return Kind::IncomingTrain;
  return Kind::None;
}

// Follows the server's series. The first value after a (re)connection is only
// remembered, and so is the first value of a new epoch: the server has started
// over (or a new meet, or a new day) and its numbers mean nothing next to the
// old ones.
class SeriesWatch {
 public:
  Kind observe(const char* epoch, uint32_t seq, const char* kind) {
    const uint32_t id = fingerprint(epoch);
    if (!seeded_ || id != epoch_) {
      seeded_ = true; epoch_ = id; seq_ = seq;
      return Kind::None;
    }
    if (seq == seq_) return Kind::None;
    seq_ = seq;
    return seq ? fromServer(kind) : Kind::None;
  }
  // A new connection: the next picture is the starting point again.
  void forget() { seeded_ = false; }

 private:
  static uint32_t fingerprint(const char* text) {
    uint32_t hash = 2166136261u;
    for (; text && *text; ++text) hash = (hash ^ uint8_t(*text)) * 16777619u;
    return hash;
  }
  bool seeded_ = false;
  uint32_t epoch_ = 0, seq_ = 0;
};

// The box's own view of its link to the server. Losing it is announced, and
// getting it back only if it was there before: a box that starts up is not
// "restored".
class LinkWatch {
 public:
  Kind observe(bool online) {
    if (online == online_) return Kind::None;
    online_ = online;
    if (online) {
      const bool back = hasBeenOnline_;
      hasBeenOnline_ = true;
      return back ? Kind::ConnectionRestored : Kind::None;
    }
    return Kind::ConnectionLost;
  }
  // The operator took the link away on purpose (the setup portal): no sound,
  // and what follows is a first connection again.
  void forget() { online_ = false; hasBeenOnline_ = false; }

 private:
  bool online_ = false, hasBeenOnline_ = false;
};

}  // namespace TrainMeetSound
