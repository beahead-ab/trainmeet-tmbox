#include "attention.h"

namespace tmbox {
namespace {

// Descending: the first match is the one the buzzer plays. Losing the server
// outranks everything, because every other event is a claim about state the
// box can no longer confirm.
const Attention kPriority[] = {
    Attention::ConnectionLost,   Attention::IncomingRequest,
    Attention::RequestDenied,    Attention::RequestApproved,
    Attention::IncomingTrain,    Attention::ConnectionRestored,
};

bool settled(const std::string& status) {
  return status == "approved" || status == "rejected";
}

}  // namespace

void AttentionController::forget() {
  seeded_ = false;
  clearances_.clear();
  messages_.clear();
  // The link is not forgotten: reassignment does not disconnect anything, and
  // pretending otherwise would announce a restore that never happened.
}

std::vector<AttentionEvent> AttentionController::observe(const Snapshot& snapshot) {
  std::vector<AttentionEvent> events;

  // Rebuilt rather than updated, so a clearance that has left the snapshot
  // stops costing memory — the box runs for a whole meet on one heap.
  std::map<std::string, std::string> clearances;
  std::set<std::string> messages;

  for (std::size_t i = 0; i < snapshot.clearances.size(); ++i) {
    const Clearance& clearance = snapshot.clearances[i];
    clearances[clearance.clearance_id] = clearance.status;
  }
  for (std::size_t i = 0; i < snapshot.line_messages.size(); ++i) {
    messages.insert(snapshot.line_messages[i].message_id);
  }

  if (!seeded_) {
    // Rule 2: catching up is not news.
    seeded_ = true;
    clearances_.swap(clearances);
    messages_.swap(messages);
    return events;
  }

  for (std::size_t i = 0; i < snapshot.clearances.size(); ++i) {
    const Clearance& clearance = snapshot.clearances[i];
    const std::string& id = clearance.clearance_id;

    std::map<std::string, std::string>::const_iterator before = clearances_.find(id);
    const bool is_new = before == clearances_.end();

    if (is_new) {
      // Someone wants to send a train here. A waiting clearance pointing the
      // other way is our own request still in flight — not news yet.
      if (clearance.status == "waiting" && clearance.to_station_id == snapshot.station_id) {
        events.push_back(AttentionEvent(Attention::IncomingRequest, id));
      }
      continue;
    }

    // Rule 1: only a transition counts, and only into a settled answer. A
    // cancellation or an expiry takes something away; it does not need a
    // sound to tell the dispatcher to stop waiting.
    if (before->second == clearance.status || !settled(clearance.status)) {
      continue;
    }
    if (clearance.from_station_id != snapshot.station_id) {
      continue;  // someone else's answer, passing through our snapshot
    }
    events.push_back(AttentionEvent(
        clearance.status == "approved" ? Attention::RequestApproved : Attention::RequestDenied,
        id));
  }

  for (std::size_t i = 0; i < snapshot.line_messages.size(); ++i) {
    const LineMessage& message = snapshot.line_messages[i];
    const std::string& id = message.message_id;
    if (messages_.find(id) != messages_.end()) {
      continue;
    }
    if (message.from_station_id == snapshot.station_id) {
      continue;  // our own announcement, echoed back
    }
    events.push_back(AttentionEvent(Attention::IncomingTrain, id));
  }

  clearances_.swap(clearances);
  messages_.swap(messages);
  return events;
}

std::vector<AttentionEvent> AttentionController::observe_link(bool online) {
  std::vector<AttentionEvent> events;
  if (online == online_) {
    return events;
  }
  online_ = online;
  if (online) {
    if (has_been_online_) {
      events.push_back(AttentionEvent(Attention::ConnectionRestored, std::string()));
    }
    has_been_online_ = true;
  } else {
    // No `has_been_online_` guard here: reaching this branch means `online_`
    // was true, and the only place that sets it also sets `has_been_online_`.
    // A mutation test found the guard could never be false.
    events.push_back(AttentionEvent(Attention::ConnectionLost, std::string()));
  }
  return events;
}

Attention AttentionController::loudest(const std::vector<AttentionEvent>& events) {
  const std::size_t count = sizeof(kPriority) / sizeof(kPriority[0]);
  for (std::size_t rank = 0; rank < count; ++rank) {
    for (std::size_t i = 0; i < events.size(); ++i) {
      if (events[i].kind == kPriority[rank]) {
        return kPriority[rank];
      }
    }
  }
  return Attention::None;
}

}  // namespace tmbox
