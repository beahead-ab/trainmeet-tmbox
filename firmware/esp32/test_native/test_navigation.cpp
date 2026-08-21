// The navigation state machine and command builder, run with no ESP32, no
// keypad and no broker. What a key press means is decided here; whether it is
// allowed was decided by the server.

#include "check.h"
#include "fixtures.h"
#include "navigation.h"

using namespace tmbox;

namespace {

constexpr std::uint32_t LATER = 10000;  // well past the input lock

struct Box {
  LocalNavigationState nav;
  StationConfig config = fixtures::charlottendal();
  Snapshot snapshot = fixtures::two_movements();
  std::uint32_t now = LATER;

  KeyResult press(char key) {
    KeyResult result = nav.press(key, now, config, snapshot);
    now += LocalNavigationState::INPUT_LOCK_MS;  // let the lock lapse
    return result;
  }
  Screen screen() const { return nav.view().screen; }
};

void c_opens_the_first_movement_and_cycles() {
  Box box;
  box.nav.show(Screen::StationOverview, 0);
  check::truthy(box.press('C').outcome == Outcome::Redraw, "C ska oppna forsta rorelsen");
  check::truthy(box.screen() == Screen::MovementDetail, "och byta skarm");
  check::truthy(box.nav.view().selected_movement == 0, "till index 0");
  box.press('C');
  check::truthy(box.nav.view().selected_movement == 1, "nasta C ska ga vidare");
  box.press('C');
  check::truthy(box.nav.view().selected_movement == 0, "och runda tillbaka");
}

void star_goes_back_from_anywhere() {
  Box box;
  for (const Screen screen : {Screen::MovementDetail, Screen::TrackPicker,
                              Screen::ClearanceInbox, Screen::LineInbox}) {
    box.nav.show(screen, 0);
    box.nav.view().selected_movement = 0;
    box.press('*');
    const bool back = box.screen() == Screen::StationOverview
                      || box.screen() == Screen::MovementDetail;
    check::truthy(back, "* ska leda tillbaka");
  }
}

void a_sends_only_what_the_server_allows() {
  Box box;
  box.nav.show(Screen::MovementDetail, 0);
  box.nav.view().selected_movement = 0;

  const KeyResult allowed = box.press('A');
  check::truthy(allowed.outcome == Outcome::Send, "A ska skicka nar servern tillater");
  check::equal("train.position.set", allowed.command.action, "forsta tillatna handlingen");
  check::equal("movement-421-cda", allowed.command.movement_id, "for ratt rorelse");

  box.snapshot.movements[0].allowed_actions = {};
  const KeyResult denied = box.press('A');
  check::truthy(denied.outcome == Outcome::Ignored,
                "A ska tiga nar servern inte tillater nagot");
}

void the_box_never_invents_permission_for_a_track_change() {
  Box box;
  box.nav.show(Screen::MovementDetail, 0);
  box.nav.view().selected_movement = 0;
  box.snapshot.movements[0].allowed_actions = {"train.position.set"};
  check::truthy(box.press('B').outcome == Outcome::Ignored,
                "B ska inte oppna sparvaljaren utan tillatelse");

  box.snapshot.movements[0].allowed_actions = {"train.track.change"};
  check::truthy(box.press('B').outcome == Outcome::Redraw, "med tillatelse ska den oppnas");
  check::truthy(box.screen() == Screen::TrackPicker, "till sparvaljaren");

  const KeyResult chosen = box.press('A');
  check::truthy(chosen.outcome == Outcome::Send, "A ska valja sparet");
  check::equal("train.track.change", chosen.command.action, "ratt handling");
  check::equal("track-cda-1a", chosen.command.track_id, "forsta sparet i katalogen");
}

void hash_opens_the_inbox_but_settles_nothing() {
  Box box;
  box.snapshot.clearances = {{"clr-1", "movement-421-cda", "connection-cda-vst",
                              "waiting", "st-vst", "st-cda"}};
  box.nav.show(Screen::StationOverview, 0);

  const KeyResult opened = box.press('#');
  check::truthy(opened.outcome == Outcome::Redraw, "# ska oppna inkorgen");
  check::truthy(opened.command.empty(), "utan att skicka nagot");
  check::truthy(box.screen() == Screen::ClearanceInbox, "till klareringsarendet");

  // The rule that matters: # may move the eye, never the decision.
  const KeyResult again = box.press('#');
  check::truthy(again.command.empty(), "# far aldrig avgora ett arende");

  const KeyResult granted = box.press('A');
  check::truthy(granted.outcome == Outcome::Send, "A avgor arendet");
  check::equal("clearance.response", granted.command.action, "ratt handling");
  check::truthy(granted.command.has_approved && granted.command.approved, "A betyder klart");

  const KeyResult refused = box.press('B');
  check::truthy(refused.command.has_approved && !refused.command.approved,
                "B betyder ej klart");
}

void a_line_message_can_only_be_acknowledged() {
  Box box;
  box.snapshot.line_messages = {{"msg-1", "connection-cda-vst", "delivered_to_device", "st-vst"}};
  box.nav.show(Screen::LineInbox, 0);
  const KeyResult acked = box.press('A');
  check::equal("line.available.acknowledge", acked.command.action, "bara kvittering");
  check::equal("msg-1", acked.command.message_id, "for ratt meddelande");
  // One-sided information carries no decision, so there is nothing to refuse.
  check::truthy(box.press('B').outcome == Outcome::Ignored, "B ska inte gora nagot har");
}

void input_is_locked_briefly_after_a_screen_change() {
  // §5: a press meant for the previous screen must not be read against the new
  // one. Without this, C then A lands the A on a train the operator never saw.
  LocalNavigationState nav;
  StationConfig config = fixtures::charlottendal();
  Snapshot snapshot = fixtures::two_movements();

  nav.show(Screen::MovementDetail, 1000);
  nav.view().selected_movement = 0;
  check::truthy(nav.locked(1000), "last direkt efter bytet");
  check::truthy(nav.press('A', 1200, config, snapshot).outcome == Outcome::Ignored,
                "ett tryck inom sparren ska ignoreras");
  check::truthy(!nav.locked(1600), "sparren ska slappa efter en halv sekund");
  check::truthy(nav.press('A', 1600, config, snapshot).outcome == Outcome::Send,
                "och slappa fram tryck darefter");
}

void a_vanished_train_drops_to_the_overview() {
  // A new snapshot replaces the cache wholesale. Sliding to a neighbour would
  // put a different movement under the same key the operator is about to press.
  LocalNavigationState nav;
  StationConfig config = fixtures::charlottendal();
  Snapshot snapshot = fixtures::two_movements();
  nav.show(Screen::MovementDetail, 0);
  nav.view().selected_movement = 1;

  snapshot.movements.pop_back();
  nav.reconcile(config, snapshot, LATER);
  check::truthy(nav.view().selected_movement == -1, "valet ska slappas");
  check::truthy(nav.view().screen == Screen::StationOverview, "och vyn falla tillbaka");
}

void an_emptied_inbox_does_not_strand_the_operator() {
  LocalNavigationState nav;
  StationConfig config = fixtures::charlottendal();
  Snapshot snapshot = fixtures::two_movements();
  snapshot.clearances = {{"clr-1", "m", "c", "waiting", "a", "b"}};
  nav.show(Screen::ClearanceInbox, 0);

  snapshot.clearances.clear();
  nav.reconcile(config, snapshot, LATER);
  check::truthy(nav.view().screen == Screen::StationOverview,
                "ett avgjort arende ska inte lamna en tom inkorg pa skarmen");
}

}  // namespace

int main() {
  c_opens_the_first_movement_and_cycles();
  star_goes_back_from_anywhere();
  a_sends_only_what_the_server_allows();
  the_box_never_invents_permission_for_a_track_change();
  hash_opens_the_inbox_but_settles_nothing();
  a_line_message_can_only_be_acknowledged();
  input_is_locked_briefly_after_a_screen_change();
  a_vanished_train_drops_to_the_overview();
  an_emptied_inbox_does_not_strand_the_operator();
  return check::report();
}
