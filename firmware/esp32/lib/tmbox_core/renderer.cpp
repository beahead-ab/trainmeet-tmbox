#include "renderer.h"

namespace tmbox {
namespace {

/// The A key carries one operative action at a time. Which one is decided by
/// the server's `allowed_actions`, never by the box: the order here only picks
/// between actions the server has already permitted.
struct Primary {
  std::string action;
  std::string label;
};

const Primary PRIMARY_ORDER[] = {
    {"train.position.set", "UPP"},
    {"train.crew_ready.set", "FORARE"},
    {"clearance.request", "BEGAR"},
    {"train.departed", "AVGATT"},
    {"train.approaching", "NARMAR"},
    {"train.arrived", "ANKOMMIT"},
};

std::string clock_text(const Snapshot& snapshot) {
  if (snapshot.clock.time.empty()) return "--:--";
  return snapshot.clock.time;
}

std::string track_label(const StationConfig& config, const Movement& movement) {
  const Track* track = config.track_by_id(movement.assigned_track_id);
  return track != nullptr ? track->display_label : std::string();
}

/// `421>[1A]` leaving, `[2A]<428` arriving.
std::string movement_mark(const StationConfig& config, const Movement& movement) {
  const std::string track = track_label(config, movement);
  return movement.is_arrival() ? arrival_mark(movement.train_number, track)
                               : departure_mark(movement.train_number, track);
}

std::string movement_time(const Movement& movement) {
  return movement.is_arrival() ? movement.arrival_time : movement.departure_time;
}

Primary primary_action_for(const Movement& movement) {
  for (const Primary& candidate : PRIMARY_ORDER) {
    if (movement.allows(candidate.action)) return candidate;
  }
  return {"", ""};
}

}  // namespace

Frame render(const Geometry& geometry,
             const ViewState& view,
             const StationConfig& config,
             const Snapshot& snapshot) {
  std::vector<std::string> lines;

  switch (view.screen) {
    case Screen::Identity:
      lines = {"TRAINMEET TMBOX", view.device_code};
      break;
    case Screen::NoNetwork:
      lines = {"NAT SAKNAS", "FORSOKER IGEN"};
      break;
    case Screen::SetupPortal:
      lines = {"INSTALLERA WIFI", view.access_point_name};
      break;
    case Screen::SeekingServer:
      lines = {"SOKER SERVER", view.device_code};
      break;
    case Screen::ServerGone:
      lines = {"SERVER BORTA", "FORSOKER IGEN"};
      break;
    case Screen::AwaitingAssignment:
      // The firmware says KOPPLA BOXEN for any assignment status but assigned,
      // and shows the code the administrator has to type into the server.
      lines = {"KOPPLA BOXEN", view.device_code};
      break;
    case Screen::Sending:
      lines = {"SKICKAR...", ""};
      break;
    case Screen::CommandAccepted:
      lines = {"KOMMANDO OK", ""};
      break;
    case Screen::CommandRejected:
      lines = {"KOMMANDO NEKAT", view.reason};
      break;

    case Screen::StationOverview: {
      const std::string label = config.code.empty() ? "TMBOX" : config.code;
      const std::string clock = clock_text(snapshot);
      // The clock sits hard right so it lands in the same place on every
      // geometry; an operator learns where to look once.
      std::string head = label;
      if (geometry.cols > label.size() + clock.size()) {
        head.append(geometry.cols - label.size() - clock.size(), ' ');
      } else {
        head.append(1, ' ');
      }
      head += clock;
      lines.push_back(head);
      lines.push_back(snapshot.movements.empty()
                          ? "INGA TAG IDAG"
                          : std::to_string(snapshot.movements.size()) + " TAG  C=BLADDRA");
      if (geometry.tall()) {
        // Four rows have room to show what is coming without browsing.
        for (std::size_t index = 0;
             index < snapshot.movements.size() && lines.size() < geometry.rows; ++index) {
          const Movement& movement = snapshot.movements[index];
          lines.push_back(movement_time(movement) + " " + movement_mark(config, movement));
        }
      }
      break;
    }

    case Screen::MovementDetail: {
      if (view.selected_movement < 0
          || static_cast<std::size_t>(view.selected_movement) >= snapshot.movements.size()) {
        lines = {"INGET TAG VALT", "*=TILLBAKA"};
        break;
      }
      const Movement& movement = snapshot.movements[view.selected_movement];
      const std::string mark = movement_mark(config, movement);
      const std::string time = movement_time(movement);
      std::string head = mark;
      if (geometry.cols > mark.size() + time.size()) {
        head.append(geometry.cols - mark.size() - time.size(), ' ');
      } else {
        head.append(1, ' ');
      }
      head += time;
      lines.push_back(head);

      const Primary primary = primary_action_for(movement);
      std::string actions;
      if (!primary.label.empty()) actions = "A=" + primary.label;
      if (movement.allows("train.track.change")) {
        if (!actions.empty()) actions += "  ";
        actions += "B=ANDRA";
      }
      if (actions.empty()) actions = "INGET TILLATET";
      lines.push_back(actions);

      if (geometry.tall()) {
        lines.push_back(std::string("FORARE ") + (movement.crew_ready ? "PA PLATS" : "SAKNAS"));
        lines.push_back("C=NASTA  *=TILLBAKA");
      }
      break;
    }

    case Screen::TrackPicker: {
      lines.push_back("VALJ SPAR");
      if (config.tracks.empty()) {
        lines.push_back("INGA SPAR");
        break;
      }
      const std::size_t index =
          view.selected_track >= 0 && static_cast<std::size_t>(view.selected_track) < config.tracks.size()
              ? static_cast<std::size_t>(view.selected_track)
              : 0;
      lines.push_back(config.tracks[index].display_label + "   A=VALJ  C=NASTA");
      if (geometry.tall()) {
        lines.push_back(std::to_string(index + 1) + " AV "
                        + std::to_string(config.tracks.size()));
        lines.push_back("*=TILLBAKA");
      }
      break;
    }

    case Screen::ClearanceInbox: {
      if (snapshot.clearances.empty()) {
        lines = {"INGA ARENDEN", "*=TILLBAKA"};
        break;
      }
      const std::size_t index =
          view.selected_case >= 0 && static_cast<std::size_t>(view.selected_case) < snapshot.clearances.size()
              ? static_cast<std::size_t>(view.selected_case)
              : 0;
      const Clearance& clearance = snapshot.clearances[index];
      lines.push_back("KLARERING " + clearance.status);
      // A settles it and B refuses it; # never leaves an operative decision.
      lines.push_back("A=KLART  B=EJ");
      if (geometry.tall()) {
        lines.push_back("FRAN " + clearance.from_station_id);
        lines.push_back(std::to_string(index + 1) + " AV "
                        + std::to_string(snapshot.clearances.size()) + "  *=TILLBAKA");
      }
      break;
    }

    case Screen::LineInbox: {
      if (snapshot.line_messages.empty()) {
        lines = {"INGA MEDDELANDEN", "*=TILLBAKA"};
        break;
      }
      const std::size_t index =
          view.selected_case >= 0 && static_cast<std::size_t>(view.selected_case) < snapshot.line_messages.size()
              ? static_cast<std::size_t>(view.selected_case)
              : 0;
      const LineMessage& message = snapshot.line_messages[index];
      lines.push_back("LINJEN LEDIG");
      // One-sided information: it carries no decision, so the only answer is
      // that it was shown.
      lines.push_back("A=KVITTERA");
      if (geometry.tall()) {
        lines.push_back("FRAN " + message.from_station_id);
        lines.push_back("*=TILLBAKA");
      }
      break;
    }
  }

  return frame_of(geometry, lines);
}

}  // namespace tmbox
