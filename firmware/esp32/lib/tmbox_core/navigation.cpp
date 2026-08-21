#include "navigation.h"

namespace tmbox {
namespace {

/// The A key's action for a movement, in the order the flow reaches them.
/// Only actions the server has already allowed are considered.
const char* const PRIMARY_ORDER[] = {
    "train.position.set", "train.crew_ready.set", "clearance.request",
    "train.departed",     "train.approaching",    "train.arrived",
};

std::string primary_action(const Movement& movement) {
  for (const char* action : PRIMARY_ORDER) {
    if (movement.allows(action)) return action;
  }
  return std::string();
}

int next_index(int current, std::size_t count) {
  if (count == 0) return -1;
  return static_cast<int>((static_cast<std::size_t>(current + 1)) % count);
}

}  // namespace

bool LocalNavigationState::locked(std::uint32_t now_ms) const {
  if (!ever_shown_) return false;
  return now_ms - screen_changed_at_ < INPUT_LOCK_MS;
}

void LocalNavigationState::show(Screen screen, std::uint32_t now_ms) {
  if (view_.screen == screen) return;
  view_.screen = screen;
  screen_changed_at_ = now_ms;
  ever_shown_ = true;
}

void LocalNavigationState::reconcile(const StationConfig& config,
                                    const Snapshot& snapshot,
                                    std::uint32_t now_ms) {
  const auto movements = static_cast<int>(snapshot.movements.size());
  if (view_.selected_movement >= movements) {
    // The train we were looking at is gone from the day's cache. Falling back
    // to the overview is the only honest answer; silently sliding to a
    // neighbouring train would put a different movement under the same key.
    view_.selected_movement = -1;
    show(Screen::StationOverview, now_ms);
  }
  if (view_.selected_case >= static_cast<int>(snapshot.clearances.size())
      && view_.screen == Screen::ClearanceInbox) {
    view_.selected_case = 0;
    if (snapshot.clearances.empty()) show(Screen::StationOverview, now_ms);
  }
  if (view_.selected_case >= static_cast<int>(snapshot.line_messages.size())
      && view_.screen == Screen::LineInbox) {
    view_.selected_case = 0;
    if (snapshot.line_messages.empty()) show(Screen::StationOverview, now_ms);
  }
  if (view_.selected_track >= static_cast<int>(config.tracks.size())) {
    view_.selected_track = 0;
  }
  if (view_.selected_connection >= static_cast<int>(config.connections.size())) {
    view_.selected_connection = 0;
  }
}

KeyResult LocalNavigationState::press(char key,
                                      std::uint32_t now_ms,
                                      const StationConfig& config,
                                      const Snapshot& snapshot) {
  if (locked(now_ms)) return KeyResult();

  // `*` always means back, from anywhere. An operator who is lost should never
  // have to work out where they are first.
  if (key == '*') {
    if (view_.screen == Screen::TrackPicker || view_.screen == Screen::ConnectionPicker) {
      show(Screen::MovementDetail, now_ms);
      return KeyResult(Outcome::Redraw);
    }
    view_.selected_movement = -1;
    view_.selected_case = 0;
    show(Screen::StationOverview, now_ms);
    return KeyResult(Outcome::Redraw);
  }

  switch (view_.screen) {
    case Screen::StationOverview: {
      if (key == 'C' && !snapshot.movements.empty()) {
        view_.selected_movement = 0;
        show(Screen::MovementDetail, now_ms);
        return KeyResult(Outcome::Redraw);
      }
      // `#` opens whatever is waiting to be read. It moves the eye, never a
      // decision - answering still needs A or B on the case itself.
      if (key == '#') {
        if (!snapshot.clearances.empty()) {
          view_.selected_case = 0;
          show(Screen::ClearanceInbox, now_ms);
          return KeyResult(Outcome::Redraw);
        }
        if (!snapshot.line_messages.empty()) {
          view_.selected_case = 0;
          show(Screen::LineInbox, now_ms);
          return KeyResult(Outcome::Redraw);
        }
      }
      return KeyResult();
    }

    case Screen::MovementDetail: {
      if (view_.selected_movement < 0
          || static_cast<std::size_t>(view_.selected_movement) >= snapshot.movements.size()) {
        return KeyResult();
      }
      const Movement& movement = snapshot.movements[view_.selected_movement];
      if (key == 'C') {
        view_.selected_movement = next_index(view_.selected_movement, snapshot.movements.size());
        screen_changed_at_ = now_ms;  // a different train is a new screen
        return KeyResult(Outcome::Redraw);
      }
      if (key == 'A') {
        const std::string action = primary_action(movement);
        if (action.empty()) return KeyResult();
        if (action == "clearance.request") {
          // A request has to name the line the train is taking. The box does
          // not guess it: the operator picks the neighbour, the same way a
          // track change picks the track.
          if (config.connections.empty()) return KeyResult();
          view_.selected_connection = 0;
          show(Screen::ConnectionPicker, now_ms);
          return KeyResult(Outcome::Redraw);
        }
        Command command;
        command.action = action;
        command.movement_id = movement.id;
        return KeyResult(Outcome::Send, command);
      }
      if (key == 'B' && movement.allows("train.track.change")) {
        view_.selected_track = 0;
        show(Screen::TrackPicker, now_ms);
        return KeyResult(Outcome::Redraw);
      }
      return KeyResult();
    }

    case Screen::TrackPicker: {
      if (config.tracks.empty()) return KeyResult();
      if (key == 'C') {
        view_.selected_track = next_index(view_.selected_track, config.tracks.size());
        screen_changed_at_ = now_ms;
        return KeyResult(Outcome::Redraw);
      }
      if (key == 'A' && view_.selected_movement >= 0
          && static_cast<std::size_t>(view_.selected_movement) < snapshot.movements.size()) {
        const Movement& movement = snapshot.movements[view_.selected_movement];
        if (!movement.allows("train.track.change")) return KeyResult();
        Command command;
        command.action = "train.track.change";
        command.movement_id = movement.id;
        command.track_id = config.tracks[view_.selected_track].id;
        return KeyResult(Outcome::Send, command);
      }
      return KeyResult();
    }

    case Screen::ConnectionPicker: {
      if (config.connections.empty()) return KeyResult();
      if (key == 'C') {
        view_.selected_connection =
            next_index(view_.selected_connection, config.connections.size());
        screen_changed_at_ = now_ms;
        return KeyResult(Outcome::Redraw);
      }
      if (key == 'A' && view_.selected_movement >= 0
          && static_cast<std::size_t>(view_.selected_movement) < snapshot.movements.size()) {
        const Movement& movement = snapshot.movements[view_.selected_movement];
        if (!movement.allows("clearance.request")) return KeyResult();
        Command command;
        command.action = "clearance.request";
        command.movement_id = movement.id;
        command.connection_id = config.connections[view_.selected_connection].connection_id;
        return KeyResult(Outcome::Send, command);
      }
      return KeyResult();
    }

    case Screen::ClearanceInbox: {
      if (snapshot.clearances.empty()) return KeyResult();
      const std::size_t index =
          static_cast<std::size_t>(view_.selected_case) < snapshot.clearances.size()
              ? static_cast<std::size_t>(view_.selected_case)
              : 0;
      if (key == 'C') {
        view_.selected_case = next_index(view_.selected_case, snapshot.clearances.size());
        screen_changed_at_ = now_ms;
        return KeyResult(Outcome::Redraw);
      }
      // A grants, B refuses. Both are operative, so neither may sit on `#`.
      if (key == 'A' || key == 'B') {
        Command command;
        command.action = "clearance.response";
        command.clearance_id = snapshot.clearances[index].clearance_id;
        command.approved = key == 'A';
        command.has_approved = true;
        return KeyResult(Outcome::Send, command);
      }
      return KeyResult();
    }

    case Screen::LineInbox: {
      if (snapshot.line_messages.empty()) return KeyResult();
      const std::size_t index =
          static_cast<std::size_t>(view_.selected_case) < snapshot.line_messages.size()
              ? static_cast<std::size_t>(view_.selected_case)
              : 0;
      if (key == 'C') {
        view_.selected_case = next_index(view_.selected_case, snapshot.line_messages.size());
        screen_changed_at_ = now_ms;
        return KeyResult(Outcome::Redraw);
      }
      if (key == 'A') {
        Command command;
        command.action = "line.available.acknowledge";
        command.message_id = snapshot.line_messages[index].message_id;
        return KeyResult(Outcome::Send, command);
      }
      return KeyResult();
    }

    default:
      return KeyResult();
  }
}

}  // namespace tmbox
