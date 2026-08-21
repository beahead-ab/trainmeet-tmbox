#pragma once

#include <string>
#include <vector>

#include "geometry.h"
#include "model.h"
#include "text.h"

namespace tmbox {

/// Which screen the box is showing. The box browses its own cache locally, so
/// the screen is local state; only a complete command ever leaves the box.
enum class Screen {
  Identity,        // the printed box code, before anything else is known
  NoNetwork,
  SetupPortal,
  SeekingServer,
  ServerGone,
  AwaitingAssignment,
  StationOverview,
  MovementDetail,
  TrackPicker,
  ConnectionPicker,
  ClearanceInbox,
  LineInbox,
  Sending,
  CommandAccepted,
  CommandRejected,
};

/// Everything the renderer needs that is not in config or snapshot.
struct ViewState {
  Screen screen = Screen::Identity;
  std::string device_code;
  std::string access_point_name;
  /// Index into the snapshot's movements, or -1 for the station overview.
  int selected_movement = -1;
  /// Index into the config's tracks, when the track picker is open.
  int selected_track = 0;
  /// Index into the config's connections, when clearance is being requested.
  int selected_connection = 0;
  /// Index into the snapshot's open cases, when an inbox is open.
  int selected_case = 0;
  /// Reason text for CommandRejected.
  std::string reason;
};

/// Turns state into exactly `rows` lines of exactly `cols` characters.
///
/// Pure by construction: no Arduino types, no display driver, no clock. That
/// is what lets every geometry be rendered and asserted in CI, on a machine
/// with no hardware attached.
Frame render(const Geometry& geometry,
             const ViewState& view,
             const StationConfig& config,
             const Snapshot& snapshot);

}  // namespace tmbox
