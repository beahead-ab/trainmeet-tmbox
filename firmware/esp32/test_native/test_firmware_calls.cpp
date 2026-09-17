// Every call the firmware makes into the core, made the same way, so a
// signature that drifts fails here rather than in a board build. It does not
// test behaviour - the other suites do that - it tests that TrainMeetTMBox.ino
// still compiles against the headers it uses.

#include <string>
#include <vector>

#include "check.h"
#include "attention.h"
#include "navigation.h"
#include "renderer.h"

using namespace tmbox;

namespace {

/// Mirrors the geometry the firmware builds from its hardware profile.
const Geometry FIRMWARE_GEOMETRY(2, 16, false);

void the_attention_call_sites_still_compile() {
  Snapshot snapshot;
  AttentionController attention;

  // connectMqtt / the retry path
  std::vector<AttentionEvent> events = attention.observe_link(true);
  events = attention.observe_link(false);

  // handleSnapshot
  events = attention.observe(snapshot);

  // signalAttention
  const Attention loudest = AttentionController::loudest(events);
  check::truthy(loudest == Attention::None, "en tom ogonblicksbild ger ingen signal");

  // disconnectMqtt / omtilldelning
  attention.forget();
}

void the_firmware_call_sites_still_compile() {
  StationConfig config;
  Snapshot snapshot;
  LocalNavigationState navigation;

  // setup() shows the box code, then the network screens run, then an
  // assignment arrives and the box waits for its data.
  navigation.show(Screen::Identity, 0);
  navigation.show(Screen::LoadingStation, 500);

  // handleConfig
  config = StationConfig();
  config.station_id = "st-cda";
  config.code = "CDA";
  config.name = "Charlottendal";
  Track track;
  track.id = "track-cda-1a";
  track.display_label = "1A";
  config.tracks.push_back(track);
  Connection connection;
  connection.connection_id = "connection-cda-vst";
  connection.other_station_code = "VST";
  connection.track_type = "single";
  config.connections.push_back(connection);

  // handleSnapshot
  snapshot = Snapshot();
  snapshot.station_id = "st-cda";
  snapshot.clock.time = "09:00";
  snapshot.clock.running = true;
  Movement movement;
  movement.id = "movement-421-cda";
  movement.train_number = "421";
  movement.departure_time = "09:20";
  movement.assigned_track_id = "track-cda-1a";
  movement.crew_ready = false;
  movement.allowed_actions.push_back("train.position.set");
  snapshot.movements.push_back(movement);
  Clearance clearance;
  clearance.clearance_id = "clr-1";
  snapshot.clearances.push_back(clearance);
  LineMessage message;
  message.message_id = "msg-1";
  snapshot.line_messages.push_back(message);
  navigation.reconcile(config, snapshot, 1000);
  // handleSnapshot leaves the waiting screen once there is something to show.
  if (navigation.view().screen == Screen::LoadingStation
      || navigation.view().screen == Screen::AwaitingAssignment) {
    navigation.show(Screen::StationOverview, 1000);
  }
  check::truthy(navigation.view().screen == Screen::StationOverview,
                "boxen ska lamna vantelaget nar data finns");

  // loop(): a key press, and whatever it produced
  const KeyResult result = navigation.press('C', 2000, config, snapshot);
  check::truthy(result.outcome == Outcome::Redraw, "C ska oppna rorelsen");
  const Command& command = result.command;
  check::truthy(command.empty() || !command.action.empty(), "ett kommando har en handling");

  // sendCommand() reads exactly these
  (void)command.movement_id.empty();
  (void)command.track_id.empty();
  (void)command.connection_id.empty();
  (void)command.clearance_id.empty();
  (void)command.message_id.empty();
  (void)command.train_number.empty();
  (void)command.has_approved;
  (void)command.approved;

  // handleAck(): a lookup answer, then a flash
  std::vector<LookupMatch> matches;
  LookupMatch match;
  match.movement_id = "movement-421-cda";
  match.train_number = "421";
  matches.push_back(match);
  navigation.apply_lookup(snapshot, matches, 3000);
  navigation.view().reason = "unknown_train_number";
  navigation.show(Screen::CommandRejected, 4000);

  // drawScreen()
  const Frame frame = render(FIRMWARE_GEOMETRY, navigation.view(), config, snapshot);
  check::truthy(frame.size() == FIRMWARE_GEOMETRY.rows, "en ruta ar sa hog som displayen");
  for (const std::string& line : frame) {
    check::truthy(line.size() == FIRMWARE_GEOMETRY.cols, "och sa bred");
    (void)line.c_str();  // what the firmware hands the LCD
  }

  // publishHello()
  (void)FIRMWARE_GEOMETRY.rows;
  (void)FIRMWARE_GEOMETRY.cols;
  (void)FIRMWARE_GEOMETRY.supports_swedish;
}

}  // namespace

int main() {
  the_firmware_call_sites_still_compile();
  the_attention_call_sites_still_compile();
  return check::report();
}
