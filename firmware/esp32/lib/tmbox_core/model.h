#pragma once

#include <string>
#include <vector>

namespace tmbox {

/// The fields the firmware decodes are exactly these. The server's own tests
/// (tests/test_firmware_wire.py in trainmeet-server) assert that its payloads
/// carry them, so this struct and that test move together or not at all.
struct Movement {
  std::string id;
  std::string train_number;
  std::string arrival_time;
  std::string departure_time;
  std::string departure;  // none | positioned | ready | departed
  std::string arrival;    // none | approaching | arrived
  std::string assigned_track_id;
  bool crew_ready = false;
  std::vector<std::string> allowed_actions;

  bool allows(const std::string& action) const;
  /// A row with no departure time is something arriving here, not leaving.
  bool is_arrival() const { return departure_time.empty(); }
};

struct Clearance {
  std::string clearance_id;
  std::string movement_id;
  std::string connection_id;
  std::string status;  // waiting | approved | rejected | cancelled | expired
  std::string from_station_id;
  std::string to_station_id;
};

struct LineMessage {
  std::string message_id;
  std::string connection_id;
  std::string status;
  std::string from_station_id;
};

struct Track {
  std::string id;
  std::string display_label;
};

struct Connection {
  std::string connection_id;
  std::string other_station_code;
  std::string track_type;
};

struct Clock {
  std::string time;
  bool running = false;
};

/// The `config` topic, cached whole. Changes only on publish.
struct StationConfig {
  std::string station_id;
  std::string code;
  std::string name;
  std::vector<Track> tracks;
  std::vector<Connection> connections;

  const Track* track_by_id(const std::string& id) const;
};

/// The `snapshot` topic, cached whole and replaced on every receipt.
struct Snapshot {
  std::string station_id;
  std::vector<Movement> movements;
  std::vector<Clearance> clearances;
  std::vector<LineMessage> line_messages;
  Clock clock;
};

}  // namespace tmbox
