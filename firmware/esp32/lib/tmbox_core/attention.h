#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "model.h"

namespace tmbox {

/// Why the box wants the dispatcher to look up.
///
/// The vocabulary is the phone app's (`TamboxNotificationEvent.Kind`), because
/// a dispatcher who carries both must not learn two ideas of what counts as
/// news.
enum class Attention : std::uint8_t {
  None = 0,
  ConnectionLost,      // the server stopped answering
  IncomingRequest,     // someone wants to send a train here
  RequestDenied,       // a request we sent came back refused
  RequestApproved,     // a request we sent came back granted
  IncomingTrain,       // a line was declared clear towards us
  ConnectionRestored,  // the server answers again
};

struct AttentionEvent {
  Attention kind;
  std::string subject;  // clearance/message id, so a log can name the cause

  AttentionEvent() : kind(Attention::None) {}
  AttentionEvent(Attention kind_, const std::string& subject_)
      : kind(kind_), subject(subject_) {}
};

/// What deserves a sound, and — more importantly — what does not.
///
/// A box that pips at everything gets ignored, and an ignored box is worse
/// than a silent one. So three rules hold, and the tests exist to keep them
/// holding:
///
///   1. Only a *change* is news. A clearance that was already waiting last
///      time stays silent, however long it waits.
///   2. The first snapshot after boot or reassignment is never news. The box
///      is catching up with a station that has been running without it; the
///      dispatcher standing there did not just cause any of it.
///   3. A change the dispatcher caused is not news to the dispatcher. This
///      one needs no guard of its own: the server only lets the receiver
///      answer a clearance (`not_receiver`) and only the sender cancel it
///      (`not_sender`), so "who caused it" and "which way it points" are the
///      same fact. The direction checks below carry the rule.
///
/// The controller decides *whether*; the hardware profile decides *how*. With
/// no buzzer defined the sink is a no-op and nothing here changes.
class AttentionController {
 public:
  /// Compare against what we saw last time and report what changed. Call this
  /// for every snapshot the box receives.
  std::vector<AttentionEvent> observe(const Snapshot& snapshot);

  /// Link transitions. Losing the server is news; regaining it is only news if
  /// we had it to begin with, so a cold start does not announce itself.
  std::vector<AttentionEvent> observe_link(bool online);

  /// Reassignment. The box is now a different station and remembers nothing.
  void forget();

  /// One buzzer, one sound: the event worth playing when several arrive at
  /// once. Returns `Attention::None` for an empty list.
  static Attention loudest(const std::vector<AttentionEvent>& events);

 private:
  bool seeded_ = false;
  bool online_ = false;
  bool has_been_online_ = false;
  std::map<std::string, std::string> clearances_;  // id -> status
  std::set<std::string> messages_;                 // ids seen
};

}  // namespace tmbox
