#include "language_menu.h"
#include "check.h"
#include "renderer.h"
#include "fixtures.h"
using namespace tmbox;
int main() {
  LanguageMenu menu;
  menu.begin("sv"); check::truthy(!menu.open, "no menu before server options");
  menu.options = {{"sv", "Svenska"}, {"da", "Dansk"}, {"nb", "Norsk"}, {"en", "English"}, {"de", "Deutsch"}};
  menu.begin("en");
  check::truthy(menu.open && menu.name() == "English", "current server choice selected");
  check::truthy(menu.press('C').empty() && menu.name() == "Deutsch", "browse local only");
  menu.press('C'); check::truthy(menu.name() == "Svenska", "wrap around");
  check::truthy(menu.press('#') == "sv", "only explicit confirmation saves");
  menu.sent("choice-1", 100);
  check::truthy(menu.press('C').empty() && menu.name() == "Svenska", "busy ignores navigation");
  check::truthy(!menu.reply("old", true) && menu.saving, "old reply ignored");
  menu.reply("choice-1", false);
  check::truthy(menu.failed && menu.open && !menu.saving, "failure remains retryable");
  menu.sent("choice-2", 200);
  menu.reply("choice-2", true);
  check::truthy(!menu.open && !menu.saving && !menu.failed, "only server confirmation closes");
  menu.begin("de"); menu.press('*');
  check::truthy(!menu.open, "cancel sends nothing");
  menu.begin("sv"); menu.sent("timeout", 0xfffffff0u); menu.tick(4990);
  check::truthy(menu.failed && !menu.saving && menu.open, "timeout across millis wrap");
  check::truthy(!menu.reply("timeout", true), "late ack does not dismiss retry dialog");
  menu.options.clear();
  check::truthy(menu.name().empty() && menu.press('#').empty(), "empty catalogue safe");
  // Server copy is data; no locale-specific screen branches or traffic logic.
  for (const auto& label : {"KOPPLA BOXEN", "ASSIGN BOX", "TILDEL BOKS", "TILDEL BOKS", "BOX ZUORDNEN"}) {
    for (const Geometry& geometry : {GEOMETRY_16X2, GEOMETRY_20X2, GEOMETRY_16X4, GEOMETRY_20X4}) {
      StationConfig config = fixtures::charlottendal();
      config.messages["KOPPLA BOXEN"] = label;
      ViewState view; view.screen = Screen::AwaitingAssignment; view.device_code = "TMBOX-ABC123";
      const Frame frame = render(geometry, view, config, fixtures::two_movements());
      check::truthy(frame.size() == geometry.rows && frame[0].size() == geometry.cols, "locale respects geometry");
      check::truthy(frame[0].find(label) == 0, "server label is rendered");
      check::truthy(frame[1].find("TMBOX-ABC123") == 0, "identity never translated");
    }
  }
  return check::report();
}
