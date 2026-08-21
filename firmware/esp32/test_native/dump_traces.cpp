// Writes what the navigation state machine does with a key sequence, so any
// other implementation of it - today the web simulator - can be held to the
// same answers instead of to a reading of the spec.

#include <iostream>

#include "fixtures.h"
#include "navigation.h"

using namespace tmbox;

namespace {

const char* screen_name(Screen screen) {
  switch (screen) {
    case Screen::Identity: return "Identity";
    case Screen::NoNetwork: return "NoNetwork";
    case Screen::SetupPortal: return "SetupPortal";
    case Screen::SeekingServer: return "SeekingServer";
    case Screen::ServerGone: return "ServerGone";
    case Screen::AwaitingAssignment: return "AwaitingAssignment";
    case Screen::StationOverview: return "StationOverview";
    case Screen::MovementDetail: return "MovementDetail";
    case Screen::TrackPicker: return "TrackPicker";
    case Screen::ConnectionPicker: return "ConnectionPicker";
    case Screen::ClearanceInbox: return "ClearanceInbox";
    case Screen::LineInbox: return "LineInbox";
    case Screen::Sending: return "Sending";
    case Screen::CommandAccepted: return "CommandAccepted";
    case Screen::CommandRejected: return "CommandRejected";
  }
  return "?";
}

const char* outcome_name(Outcome outcome) {
  switch (outcome) {
    case Outcome::Ignored: return "Ignored";
    case Outcome::Redraw: return "Redraw";
    case Outcome::Send: return "Send";
  }
  return "?";
}

void run(const char* name, const std::string& keys, Snapshot snapshot,
         const std::vector<std::string>& allowed, std::uint32_t step) {
  const StationConfig config = fixtures::charlottendal();
  if (!allowed.empty() && !snapshot.movements.empty()) {
    snapshot.movements[0].allowed_actions = allowed;
  }
  LocalNavigationState nav;
  nav.show(Screen::StationOverview, 0);
  std::uint32_t now = 10000;

  std::cout << "\n[" << name << "]\n";
  for (const char key : keys) {
    const KeyResult result = nav.press(key, now, config, snapshot);
    now += step;
    std::cout << key << " -> " << outcome_name(result.outcome)
              << " screen=" << screen_name(nav.view().screen)
              << " move=" << nav.view().selected_movement
              << " track=" << nav.view().selected_track
              << " conn=" << nav.view().selected_connection
              << " case=" << nav.view().selected_case;
    if (result.outcome == Outcome::Send) {
      const Command& command = result.command;
      std::cout << " action=" << command.action;
      if (!command.movement_id.empty()) std::cout << " movement=" << command.movement_id;
      if (!command.track_id.empty()) std::cout << " track_id=" << command.track_id;
      if (!command.connection_id.empty()) std::cout << " connection=" << command.connection_id;
      if (!command.clearance_id.empty()) std::cout << " clearance=" << command.clearance_id;
      if (!command.message_id.empty()) std::cout << " message=" << command.message_id;
      if (command.has_approved) std::cout << " approved=" << (command.approved ? "1" : "0");
    }
    std::cout << "\n";
  }
}

}  // namespace

int main() {
  std::cout << "# Genererad av dump_traces.cpp - redigera inte for hand.\n";

  Snapshot plain = fixtures::two_movements();

  Snapshot with_cases = fixtures::two_movements();
  with_cases.clearances = {{"clr-1", "movement-421-cda", "connection-cda-vst", "waiting",
                            "st-vst", "st-cda"}};
  with_cases.line_messages = {{"msg-1", "connection-cda-vst", "delivered_to_device", "st-vst"}};

  // A press per half second: the input lock has always lapsed, so these
  // traces show what each key means.
  const std::uint32_t UNHURRIED = LocalNavigationState::INPUT_LOCK_MS;
  // A press every 100 ms: faster than an operator, but exactly what a stuck
  // key or a bounced contact produces. The lock is the rule that stops the
  // second press landing on a screen the first one just changed.
  const std::uint32_t HURRIED = 100;

  run("browse-and-position", "CCA*", plain, {"train.position.set"}, UNHURRIED);
  run("track-change-refused-then-allowed", "CBA", plain, {"train.position.set"}, UNHURRIED);
  run("track-picker-picks-second", "CBCA", plain, {"train.track.change"}, UNHURRIED);
  run("hash-opens-clearance-and-a-settles", "#A", with_cases, {}, UNHURRIED);
  run("hash-then-b-refuses", "#B", with_cases, {}, UNHURRIED);
  run("line-message-only-acknowledges", "##AB", with_cases, {}, UNHURRIED);
  run("star-always-returns", "C#*", with_cases, {}, UNHURRIED);
  run("nothing-allowed-stays-silent", "CA", plain, {"train.track.change"}, UNHURRIED);

  // The lock, exercised where it matters. Without it the second C lands on a
  // screen the first C just changed, and A after it settles a train the
  // operator never saw.
  run("clearance-request-picks-the-neighbour", "CACA", plain,
      {"clearance.request"}, UNHURRIED);
  run("hurried-presses-are-swallowed", "CCA", plain, {"train.position.set"}, HURRIED);
  run("hurried-clearance-answer-is-swallowed", "#A", with_cases, {}, HURRIED);
  return 0;
}
