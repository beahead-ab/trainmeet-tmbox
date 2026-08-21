// Writes the canonical frames every implementation of this renderer must
// produce. The C++ renderer on the box is the original; anything else that
// draws a TMBox screen - today the web simulator - is checked against this
// file rather than against a reading of the spec.

#include <iostream>

#include "fixtures.h"
#include "renderer.h"

using namespace tmbox;

namespace {

struct Case {
  const char* name;
  Screen screen;
  int movement;
};

const Case CASES[] = {
    {"identity", Screen::Identity, -1},
    {"awaiting-assignment", Screen::AwaitingAssignment, -1},
    {"station-overview", Screen::StationOverview, -1},
    {"movement-departure", Screen::MovementDetail, 0},
    {"movement-arrival", Screen::MovementDetail, 1},
    {"track-picker", Screen::TrackPicker, 0},
    {"clearance-inbox", Screen::ClearanceInbox, 0},
    {"line-inbox", Screen::LineInbox, 0},
    {"command-accepted", Screen::CommandAccepted, -1},
    {"command-rejected", Screen::CommandRejected, -1},
};

struct NamedGeometry {
  const char* name;
  Geometry geometry;
};

const NamedGeometry GEOMETRIES[] = {
    {"16x2", GEOMETRY_16X2},
    {"20x2", GEOMETRY_20X2},
    {"16x4", GEOMETRY_16X4},
    {"20x4", GEOMETRY_20X4},
};

}  // namespace

int main() {
  const StationConfig config = fixtures::charlottendal();
  Snapshot snapshot = fixtures::two_movements();
  snapshot.clearances = {{"clr-1", "movement-421-cda", "connection-cda-vst", "waiting",
                          "st-vst", "st-cda"}};
  snapshot.line_messages = {{"msg-1", "connection-cda-vst", "delivered_to_device", "st-vst"}};

  std::cout << "# Genererad av dump_golden.cpp - redigera inte for hand.\n"
            << "# Varje rad ar exakt sa bred som geometrin sager.\n";
  for (const NamedGeometry& named : GEOMETRIES) {
    for (const Case& item : CASES) {
      ViewState view;
      view.screen = item.screen;
      view.device_code = "TMBOX-A7K2C3";
      view.selected_movement = item.movement;
      view.reason = "spar_upptaget";
      const Frame frame = render(named.geometry, view, config, snapshot);
      std::cout << "\n[" << named.name << " " << item.name << "]\n";
      for (const std::string& line : frame) std::cout << "|" << line << "|\n";
    }
  }
  return 0;
}
