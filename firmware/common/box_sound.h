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

// Some common frequencies.
#define NOTE_SILENT      1
#define NOTE_C0         16  // Octave 0
#define NOTE_CS0        17
#define NOTE_D0         18
#define NOTE_DS0        19
#define NOTE_E0         21
#define NOTE_F0         22
#define NOTE_FS0        23
#define NOTE_G0         24
#define NOTE_GS0        26
#define NOTE_A0         28
#define NOTE_AS0        29
#define NOTE_B0         31
#define NOTE_C1         33  // Octave 1
#define NOTE_CS1        35
#define NOTE_D1         37
#define NOTE_DS1        39
#define NOTE_E1         41
#define NOTE_F1         44
#define NOTE_FS1        46
#define NOTE_G1         49
#define NOTE_GS1        52
#define NOTE_A1         55
#define NOTE_AS1        58
#define NOTE_B1         62
#define NOTE_C2         65  // Octave 2
#define NOTE_CS2        69
#define NOTE_D2         73
#define NOTE_DS2        78
#define NOTE_E2         82
#define NOTE_F2         87
#define NOTE_FS2        92
#define NOTE_G2         98
#define NOTE_GS2       104
#define NOTE_A2        110
#define NOTE_AS2       117
#define NOTE_B2        123
#define NOTE_C3        131  // Octave 3
#define NOTE_CS3       139
#define NOTE_D3        147
#define NOTE_DS3       156
#define NOTE_E3        165
#define NOTE_F3        175
#define NOTE_FS3       185
#define NOTE_G3        196
#define NOTE_GS3       308
#define NOTE_A3        220
#define NOTE_AS3       233
#define NOTE_B3        245
#define NOTE_C4        262  // Octave 4
#define NOTE_CS4       277
#define NOTE_D4        294
#define NOTE_DS4       311
#define NOTE_E4        330
#define NOTE_F4        350
#define NOTE_FS4       370
#define NOTE_G4        392
#define NOTE_GS4       415
#define NOTE_A4        440
#define NOTE_AS4       466
#define NOTE_B4        494
#define NOTE_C5        523  // Octave 5
#define NOTE_CS5       554
#define NOTE_D5        587
#define NOTE_DS5       622
#define NOTE_E5        659
#define NOTE_F5        698
#define NOTE_FS5       740
#define NOTE_G5        784
#define NOTE_GS5       831
#define NOTE_A5        880
#define NOTE_AS5       932
#define NOTE_B5        988
#define NOTE_C6       1046  // Octave 6
#define NOTE_CS6      1109
#define NOTE_D6       1175
#define NOTE_DS6      1244
#define NOTE_E6       1319
#define NOTE_F6       1397
#define NOTE_FS6      1480
#define NOTE_G6       1568
#define NOTE_GS6      1661
#define NOTE_A6       1760
#define NOTE_AS6      1865
#define NOTE_B6       1976
#define NOTE_C7       2093  // Octave 7
#define NOTE_CS7      2217
#define NOTE_D7       2349
#define NOTE_DS7      2489
#define NOTE_E7       2637
#define NOTE_F7       2794
#define NOTE_FS7      2960
#define NOTE_G7       3136
#define NOTE_GS7      3322
#define NOTE_A7       3520
#define NOTE_AS7      3729
#define NOTE_B7       3951
#define NOTE_C8       4186  // Octave 8
#define NOTE_CS8      4435
#define NOTE_D8       4699
#define NOTE_DS8      4978
#define NOTE_E8       5274
#define NOTE_F8       5588
#define NOTE_FS8      5920
#define NOTE_G8       6272
#define NOTE_GS8      6645
#define NOTE_A8       7040
#define NOTE_AS8      7459
#define NOTE_B8       7902

#define DURATION_1_16   62  // 1/16 note
#define DURATION_1_8   125  // 1/8 note
#define DURATION_1_4   250  // 1/4 note
#define DURATION_1_2   500  // 1/2 note
#define DURATION_3_4   750  // 3/4 note
#define DURATION_1_1  1000  // 1/1 note

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
    case Kind::ConnectionLost:     return {NOTE_F5, DURATION_1_2};
    case Kind::ConnectionRestored: return {NOTE_F6, DURATION_1_8};
    case Kind::IncomingRequest:    return {NOTE_CS7, DURATION_1_4};
    case Kind::RequestDenied:      return {NOTE_A5, DURATION_1_4};
    case Kind::RequestApproved:    return {NOTE_E7, DURATION_1_8};
    case Kind::IncomingTrain:      return {NOTE_A6, DURATION_1_8};
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
