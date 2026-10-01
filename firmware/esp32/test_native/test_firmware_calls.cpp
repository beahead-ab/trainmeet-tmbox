// Every call the firmware makes into the core, made the same way, so a
// signature that drifts fails here rather than in a board build. It does not
// test behaviour - the other suites do that - it tests that TrainMeetTMBox.ino
// still compiles against the headers it uses.

#include <map>
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
  AttentionController attention;

  // connectMqtt, when a connection attempt fails
  const std::vector<AttentionEvent> events = attention.observe_link(false);

  // signalAttention
  const Attention loudest = AttentionController::loudest(events);
  (void)loudest;
  check::truthy(AttentionController::loudest({}) == Attention::None, "inga handelser ger ingen signal");
}

// Since 0.7.4 the 16x2 terminal draws everything the server sends. What is
// left of the core is the box's own screens before and between sessions.
void the_firmware_call_sites_still_compile() {
  StationConfig config;
  const Snapshot snapshot;
  LocalNavigationState navigation;

  // showScreen(): setup, the portal, the network and the search for a server
  navigation.show(Screen::Identity, 0);
  navigation.show(Screen::SetupPortal, 100);
  navigation.show(Screen::NoNetwork, 200);
  navigation.show(Screen::ServerGone, 300);
  navigation.show(Screen::SeekingServer, 400);

  // drawScreen(): copy from the server's last ui, then the frame
  config.language = "en";
  const std::map<std::string, std::string> messages{{"SOKER SERVER", "SEEKING SERVER"}};
  if (config.messages != messages) config.messages = messages;
  const Frame frame = render(FIRMWARE_GEOMETRY, navigation.view(), config, snapshot);
  check::truthy(frame.size() == FIRMWARE_GEOMETRY.rows, "en ruta ar sa hog som displayen");
  for (const std::string& line : frame) {
    check::truthy(line.size() == FIRMWARE_GEOMETRY.cols, "och sa bred");
    (void)line.c_str();  // what the firmware hands the LCD
  }
  check::truthy(frame[0].rfind("SEEKING SERVER", 0) == 0, "texten fran servern anvands");
}

}  // namespace

int main() {
  the_firmware_call_sites_still_compile();
  the_attention_call_sites_still_compile();
  return check::report();
}
