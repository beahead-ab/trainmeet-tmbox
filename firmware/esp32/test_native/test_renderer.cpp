// The renderer runs here with no ESP32, no display and no network, which is
// what §21 asks for: every geometry the box supports, asserted in CI.

#include "check.h"
#include "fixtures.h"
#include "renderer.h"

using namespace tmbox;

namespace {

const Geometry ALL[] = {GEOMETRY_16X2, GEOMETRY_20X2, GEOMETRY_16X4, GEOMETRY_20X4};

std::string name_of(const Geometry& geometry) {
  return std::to_string(geometry.cols) + "x" + std::to_string(geometry.rows);
}

void every_frame_is_exactly_the_display() {
  // A frame that is short by one character leaves the previous frame's tail on
  // the glass. Every screen, every geometry, exact.
  const StationConfig config = fixtures::charlottendal();
  const Snapshot snapshot = fixtures::two_movements();
  const Screen screens[] = {
      Screen::Identity, Screen::NoNetwork, Screen::SetupPortal, Screen::SeekingServer,
      Screen::ServerGone, Screen::AwaitingAssignment, Screen::StationOverview,
      Screen::MovementDetail, Screen::TrackPicker, Screen::ConnectionPicker,
      Screen::ClearanceInbox,
      Screen::LineInbox, Screen::Sending, Screen::CommandAccepted, Screen::CommandRejected,
  };
  for (const Geometry& geometry : ALL) {
    for (const Screen screen : screens) {
      ViewState view;
      view.screen = screen;
      view.device_code = "TMBOX-A7K2C3";
      view.selected_movement = 0;
      const Frame frame = render(geometry, view, config, snapshot);
      check::truthy(frame.size() == geometry.rows,
                    name_of(geometry) + " gav " + std::to_string(frame.size()) + " rader");
      for (const std::string& line : frame) {
        check::truthy(line.size() == geometry.cols,
                      name_of(geometry) + " rad [" + line + "] var "
                          + std::to_string(line.size()) + " tecken");
      }
    }
  }
}

void the_arrow_says_which_way_the_train_goes() {
  const StationConfig config = fixtures::charlottendal();
  const Snapshot snapshot = fixtures::two_movements();

  ViewState leaving;
  leaving.screen = Screen::MovementDetail;
  leaving.selected_movement = 0;
  check::equal("421>[1B]   09:20", render(GEOMETRY_16X2, leaving, config, snapshot)[0],
               "avgang ska peka ut fran stationen");

  ViewState arriving;
  arriving.screen = Screen::MovementDetail;
  arriving.selected_movement = 1;
  check::equal("[2A]<428   09:41", render(GEOMETRY_16X2, arriving, config, snapshot)[0],
               "ankomst ska peka in mot stationen");
}

void the_action_line_comes_from_the_server() {
  // The box must never offer a key the server has not allowed.
  const StationConfig config = fixtures::charlottendal();
  Snapshot snapshot = fixtures::two_movements();
  ViewState view;
  view.screen = Screen::MovementDetail;
  view.selected_movement = 0;

  check::equal("A=UPP           ", render(GEOMETRY_16X2, view, config, snapshot)[1],
               "bara uppstallt ar tillatet fran borjan");

  snapshot.movements[0].allowed_actions = {"train.crew_ready.set", "train.track.change"};
  check::equal("A=FORARE  B=ANDRA", render(GEOMETRY_20X2, view, config, snapshot)[1].substr(0, 17),
               "forare plus sparbyte");

  snapshot.movements[0].allowed_actions = {};
  check::equal("INGET TILLATET  ", render(GEOMETRY_16X2, view, config, snapshot)[1],
               "inget tillatet ska sagas rent ut");
}

void hash_never_settles_an_operative_decision() {
  // §6's safety rule, asserted where an operator can actually see it.
  const StationConfig config = fixtures::charlottendal();
  Snapshot snapshot = fixtures::two_movements();
  snapshot.clearances = {{"clr-1", "movement-421-cda", "connection-cda-vst", "waiting",
                          "st-cda", "st-vst"}};
  ViewState view;
  view.screen = Screen::ClearanceInbox;
  for (const Geometry& geometry : ALL) {
    const Frame frame = render(geometry, view, config, snapshot);
    for (const std::string& line : frame) {
      check::truthy(line.find("#=") == std::string::npos,
                    name_of(geometry) + " erbjod # for ett beslut: [" + line + "]");
    }
  }
  check::equal("A=KLART  B=EJ   ", render(GEOMETRY_16X2, view, config, snapshot)[1],
               "klarering besvaras med A eller B");
}

void an_unassigned_box_asks_to_be_connected() {
  ViewState view;
  view.screen = Screen::AwaitingAssignment;
  view.device_code = "TMBOX-A7K2C3";
  const Frame frame = render(GEOMETRY_16X2, view, StationConfig{}, Snapshot{});
  check::equal("KOPPLA BOXEN    ", frame[0], "otilldelad box ska be om koppling");
  check::equal("TMBOX-A7K2C3    ", frame[1], "och visa koden administratoren skriver in");
}

void the_meeting_clock_sits_in_the_same_place() {
  const StationConfig config = fixtures::charlottendal();
  const Snapshot snapshot = fixtures::two_movements();
  ViewState view;
  view.screen = Screen::StationOverview;
  for (const Geometry& geometry : ALL) {
    const std::string head = render(geometry, view, config, snapshot)[0];
    check::equal("09:00", head.substr(head.size() - 5),
                 name_of(geometry) + " ska ha klockan langst till hoger");
  }
}

void four_rows_show_what_two_rows_must_be_browsed_for() {
  const StationConfig config = fixtures::charlottendal();
  const Snapshot snapshot = fixtures::two_movements();
  ViewState view;
  view.screen = Screen::StationOverview;
  const Frame tall = render(GEOMETRY_20X4, view, config, snapshot);
  check::truthy(tall[2].find("421>[1B]") != std::string::npos,
                "fyra rader ska lista forsta rorelsen direkt");
  check::truthy(tall[3].find("[2A]<428") != std::string::npos,
                "och den andra");
}

void swedish_folds_when_the_display_cannot_show_it() {
  check::equal("SPAR", transliterate("SPÅR"), "A-ring ska bli A");
  check::equal("BEGAR", transliterate("BEGÄR"), "A-umlaut ska bli A");
  check::equal("FORARE", transliterate("FÖRARE"), "O-umlaut ska bli O");

  Geometry swedish = GEOMETRY_16X2;
  swedish.supports_swedish = true;
  ViewState view;
  view.screen = Screen::CommandRejected;
  view.reason = "SPÅR UPPTAGET";
  const Frame kept = render(swedish, view, StationConfig{}, Snapshot{});
  check::truthy(kept[1].find("SPÅR") != std::string::npos,
                "en display med CGRAM ska behalla prickarna");
}

void a_clearance_request_names_the_neighbour() {
  // A request has to say which line the train is taking, so the screen that
  // sends it has to show the choice.
  const StationConfig config = fixtures::charlottendal();
  const Snapshot snapshot = fixtures::two_movements();
  ViewState view;
  view.screen = Screen::ConnectionPicker;
  view.selected_movement = 0;
  check::equal("BEGAR MOT    VST", render(GEOMETRY_16X2, view, config, snapshot)[0],
               "grannens kod ska sta hogerstalld");
  check::equal("A=BEGAR  C=NASTA", render(GEOMETRY_16X2, view, config, snapshot)[1],
               "och tangenterna fa en hel rad, okapade");
  view.selected_connection = 1;
  check::truthy(render(GEOMETRY_16X2, view, config, snapshot)[0].find("KUN") != std::string::npos,
                "nasta granne ska ga att valja");
}

void a_case_is_shown_in_operator_language() {
  // A protocol enum on the glass is a leak, not information: the person
  // reading it answers with A or B, and reads Swedish doing it.
  const StationConfig config = fixtures::charlottendal();
  Snapshot snapshot = fixtures::two_movements();
  snapshot.clearances = {{"clr-1", "movement-421-cda", "connection-cda-vst", "waiting",
                          "st-vst", "st-cda"}};
  ViewState view;
  view.screen = Screen::ClearanceInbox;

  check::equal("KLARERING VANTAR", render(GEOMETRY_16X2, view, config, snapshot)[0],
               "status ska skrivas ut pa svenska");

  // The station's code, never the id it carries internally.
  check::equal("FRAN VST            ", render(GEOMETRY_20X4, view, config, snapshot)[2],
               "motstationen ska visas med sin kod");

  snapshot.clearances[0].status = "rejected";
  check::equal("KLARERING EJ KLART", render(GEOMETRY_20X2, view, config, snapshot)[0].substr(0, 18),
               "avslag ska sagas rent ut");
}

void an_empty_station_says_so() {
  const StationConfig config = fixtures::charlottendal();
  ViewState view;
  view.screen = Screen::StationOverview;
  const Frame frame = render(GEOMETRY_16X2, view, config, Snapshot{});
  check::equal("INGA TAG IDAG   ", frame[1], "en dag utan tag ska sagas rent ut");
}

}  // namespace

int main() {
  every_frame_is_exactly_the_display();
  the_arrow_says_which_way_the_train_goes();
  the_action_line_comes_from_the_server();
  hash_never_settles_an_operative_decision();
  an_unassigned_box_asks_to_be_connected();
  the_meeting_clock_sits_in_the_same_place();
  four_rows_show_what_two_rows_must_be_browsed_for();
  swedish_folds_when_the_display_cannot_show_it();
  a_clearance_request_names_the_neighbour();
  a_case_is_shown_in_operator_language();
  an_empty_station_says_so();
  return check::report();
}
