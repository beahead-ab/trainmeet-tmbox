// Exercise the real shared ESP8266/ESP32 adapter. No traffic engine is mocked
// into the firmware: the frames below are synthetic protocol inputs only.
#include "../firmware/common/server_terminal.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error( \
  std::string(__func__) + ":" + std::to_string(__LINE__) + " " #condition); } while (0)

JsonDocument screen(const char* context = "meet-1:station-MUN:reset-1", int revision = 1, int view = 1) {
  JsonDocument frame;
  frame["profile"] = "server-16x2";
  frame["rows"] = 2; frame["cols"] = 16;
  frame["revision"] = revision; frame["view_revision"] = view;
  frame["view_token"] = "token-" + std::to_string(revision) + "-" + std::to_string(view);
  frame["entry"]["context"] = context;
  frame["entry"]["row"] = 0; frame["entry"]["column"] = 5; frame["entry"]["max_length"] = 5;
  for (JsonObject object : {frame.as<JsonObject>(), frame["entry"].as<JsonObject>()}) {
    auto cells = object["lcd"]["cells"].to<JsonArray>();
    for (const char* line : {"                ", "Nr# C/D    12:34"}) {
      auto row = cells.add<JsonArray>();
      for (int c = 0; c < 16; ++c) row.add(static_cast<int>(line[c]));
    }
    object["lcd"]["glyphs"].to<JsonArray>();
  }
  for (const char* key : {"#", "*", "A", "B", "C", "D"}) frame["keys"][key]["label"] = key;
  return frame;
}

struct Fixture {
  MqttClient mqtt;
  ServerTerminal terminal;
  Fixture() {
    hostMillis = 1000;
    terminal.begin(mqtt, "device-1", "TBX-123456", "host-test", "0.7.0", "boot-1");
    receive(screen()); hostMillis += 600;
    CHECK(terminal.ready());
  }
  void receive(const JsonDocument& frame, const char* leaf = "frame", const String& command = "", const char* status = "accepted",
               const char* boot = "boot-1", bool retained = false) {
    JsonDocument envelope;
    envelope["boot"] = boot; envelope["frame"] = frame;
    envelope["command_id"] = command; envelope["status"] = status;
    String payload; serializeJson(envelope, payload);
    terminal.receive(terminal.prefix + leaf, payload, retained);
  }
  size_t commands() const {
    size_t count = 0;
    for (const auto& m : mqtt.messages) if (m.topic.endsWith("/command")) ++count;
    return count;
  }
  JsonDocument last() const {
    JsonDocument doc; CHECK(!deserializeJson(doc, mqtt.messages.back().body)); return doc;
  }
  String answered;
  // Time passes in quarter seconds; a live server answers each new presence.
  // False as soon as the terminal gives up its session.
  bool wait(uint32_t ms, bool serverAnswers = true) {
    for (uint32_t step = 0; step < ms; step += 250) {
      hostMillis += 250;
      if (!terminal.tick()) return false;
      if (serverAnswers && terminal.nonce.length() && terminal.nonce != answered) {
        answered = terminal.nonce;
        JsonDocument alive; alive["boot"] = "boot-1"; alive["nonce"] = terminal.nonce;
        String payload; serializeJson(alive, payload);
        terminal.receive(terminal.prefix + "alive", payload, false);
      }
    }
    return true;
  }
};

struct LCD {
  std::array<std::array<uint8_t, 20>, 4> cells{};
  std::array<std::array<uint8_t, 8>, 8> glyphs{};
  int row = 0, column = 0, writes = 0, glyphWrites = 0;
  void createChar(uint8_t slot, uint8_t* bits) {
    CHECK(slot < 8); std::copy(bits, bits + 8, glyphs[slot].begin()); ++glyphWrites;
  }
  void setCursor(uint8_t c, uint8_t r) { CHECK(c < 20 && r < 4); row = r; column = c; }
  void write(uint8_t value) { CHECK(row < 4 && column < 20); cells[row][column++] = value; ++writes; }
};

void hello_and_presence_are_not_assignments() {
  Fixture f;
  CHECK(f.mqtt.subscriptions.size() == 3);
  JsonDocument hello; CHECK(!deserializeJson(hello, f.mqtt.messages.front().body));
  CHECK(hello["hardware_version"] == "server-16x2"); CHECK(hello["firmware_version"] == "0.7.0");
  CHECK(hello["boot"] == "boot-1");
  hostMillis = 6100; CHECK(f.terminal.tick());
  CHECK(f.mqtt.messages.size() == 2); CHECK(f.mqtt.messages.back().topic.endsWith("/presence"));
  CHECK(f.commands() == 0);
}
void digits_stay_local_until_confirm() {
  Fixture f;
  for (char key : std::string("0093")) CHECK(f.terminal.press(key));
  CHECK(f.commands() == 0); CHECK(f.terminal.digits == "0093");
  CHECK(f.terminal.press('#')); CHECK(f.commands() == 1);
  auto command = f.last(); CHECK(command["train_number"] == "0093");
  CHECK(command["entry_context"] == f.terminal.context()); CHECK(command["view_token"] == f.terminal.token());
  CHECK(command["key"] == "#"); CHECK(command["boot"] == "boot-1");
  CHECK(!f.mqtt.messages.back().retained); CHECK(f.mqtt.messages.back().qos == 1);
  CHECK(!f.terminal.press('#')); CHECK(f.commands() == 1);
}
void local_edit_cancel_and_queue() {
  Fixture f;
  CHECK(f.terminal.press('9')); CHECK(f.terminal.press('3')); CHECK(f.terminal.press('B'));
  CHECK(f.terminal.digits == "9"); CHECK(f.commands() == 0);
  CHECK(!f.terminal.press('C')); CHECK(!f.terminal.press('D'));
  CHECK(f.terminal.press('*')); CHECK(f.terminal.digits.empty()); CHECK(f.commands() == 0);
  CHECK(f.terminal.press('9')); CHECK(f.terminal.press('A')); CHECK(f.terminal.digits.empty());
  CHECK(f.commands() == 1); CHECK(f.last()["key"] == "A"); CHECK(f.last()["train_number"].isNull());
}
void length_and_entry_context_are_checked() {
  Fixture f;
  CHECK(f.terminal.replaceDigits(f.terminal.context(), "12345")); CHECK(!f.terminal.press('6'));
  CHECK(!f.terminal.replaceDigits("old-context", "93"));
  for (const char* invalid : {"", "123456", "9x", "-93", " 93"}) CHECK(!f.terminal.replaceDigits(f.terminal.context(), invalid));
  CHECK(f.commands() == 0); CHECK(f.terminal.digits == "12345");
}
void placement_refresh_keeps_unsubmitted_digits() {
  Fixture f;
  CHECK(f.terminal.press('9')); CHECK(f.terminal.press('3'));
  auto next = screen("meet-1:station-MUN:reset-1", 1, 2);
  next["lcd"]["cells"][0][0] = static_cast<int>('M');
  f.receive(next);
  CHECK(f.terminal.digits == "93"); CHECK(f.commands() == 0);
  CHECK(f.terminal.token() == "token-1-2"); CHECK(f.terminal.frame["lcd"]["cells"][0][0] == 'M');
  CHECK(f.terminal.press('#')); CHECK(f.last()["view_token"] == "token-1-2");
}
void older_views_and_revisions_do_not_roll_back() {
  Fixture f;
  f.receive(screen("meet-1:station-MUN:reset-1", 3, 5));
  CHECK(f.terminal.press('9'));
  f.receive(screen("meet-1:station-MUN:reset-1", 3, 4));
  f.receive(screen("meet-1:station-MUN:reset-1", 2, 99));
  CHECK(f.terminal.token() == "token-3-5"); CHECK(f.terminal.digits == "9");
}
void changed_station_meet_or_reset_clears_input() {
  for (const char* context : {"meet-1:station-CDA:reset-1", "meet-2:station-MUN:reset-1", "meet-1:station-MUN:reset-2"}) {
    Fixture f; CHECK(f.terminal.press('9'));
    const auto old = f.terminal.context(); f.receive(screen(context, 0, 0));
    CHECK(f.terminal.digits.empty()); CHECK(!f.terminal.replaceDigits(old, "93")); CHECK(f.commands() == 0);
  }
}
// The keys as the server marks them: '#' offers a traffic action, the rest
// only change what is shown.
JsonDocument marked(int view = 1) {
  auto frame = screen("meet-1:station-MUN:reset-1", 1, view);
  for (const char* key : {"*", "A", "B", "C", "D"}) frame["keys"][key]["acts"] = false;
  frame["keys"]["#"]["acts"] = true;
  return frame;
}
void accepted_and_duplicate_ack_clear_input_and_guard_double_press() {
  for (const char* status : {"accepted", "duplicate"}) {
    Fixture f; CHECK(f.terminal.press('9')); CHECK(f.terminal.press('#'));
    f.receive(screen("meet-1:station-MUN:reset-1", 1, 2), "ack", f.terminal.pending, status);
    CHECK(f.terminal.pending.empty()); CHECK(f.terminal.digits.empty()); CHECK(!f.terminal.press('#'));
    hostMillis += 501; CHECK(f.terminal.press('#')); CHECK(f.commands() == 2);
  }
}
// Until 0.7.2 every key waited half a second after every answer, so browsing
// with C and D cost the round trip plus 500 ms per press.
void browsing_answers_at_once() {
  Fixture f; f.receive(marked(1)); hostMillis += 600;
  for (int view = 2; view <= 4; ++view) {
    CHECK(f.terminal.press('D'));
    f.receive(marked(view), "ack", f.terminal.pending, "accepted");
  }
  CHECK(f.terminal.press('C')); CHECK(f.commands() == 4);
}
void a_new_screen_guards_only_acting_keys() {
  Fixture f; f.receive(marked(1)); hostMillis += 600;
  CHECK(f.terminal.press('D')); f.receive(marked(2), "ack", f.terminal.pending, "accepted");
  CHECK(!f.terminal.press('#')); CHECK(f.commands() == 1);
  hostMillis += 499; CHECK(!f.terminal.press('#'));
  hostMillis += 1; CHECK(f.terminal.press('#')); CHECK(f.commands() == 2);
}
// An incoming request opens on its own, and '#' suddenly means "give
// clearance". That push is a screen change like any other.
void a_pushed_screen_change_guards_too() {
  Fixture f; f.receive(marked(1)); hostMillis += 600;
  auto request = marked(1); request["view_token"] = "token-request"; f.receive(request);
  CHECK(!f.terminal.press('#')); CHECK(f.terminal.press('D')); CHECK(f.commands() == 1);
  auto meanings = marked(5); meanings["view_token"] = "same"; f.receive(meanings, "ack", f.terminal.pending, "accepted");
  hostMillis += 600;
  auto relabelled = marked(5); relabelled["view_token"] = "same"; relabelled["keys"]["#"]["label"] = "Ge klart";
  f.receive(relabelled); CHECK(!f.terminal.press('#'));
}
void an_unchanged_screen_or_a_new_minute_does_not_guard() {
  Fixture f; f.receive(marked(1)); hostMillis += 600;
  CHECK(f.terminal.press('#')); f.receive(marked(1), "ack", f.terminal.pending, "rejected");
  CHECK(f.terminal.press('#')); f.receive(marked(1), "ack", f.terminal.pending, "rejected");
  auto minute = marked(1); minute["lcd"]["cells"][1][15] = static_cast<int>('5'); f.receive(minute);
  CHECK(f.terminal.press('#')); CHECK(f.commands() == 3);
}
void digits_and_search_are_never_guarded() {
  Fixture f; f.receive(marked(1)); hostMillis += 600;
  f.receive(marked(2));
  CHECK(f.terminal.press('9')); CHECK(f.terminal.press('3')); CHECK(f.terminal.press('#'));
  CHECK(f.last()["train_number"] == "93"); CHECK(f.commands() == 1);
}
void a_key_without_the_flag_counts_as_acting() {
  Fixture f; f.receive(screen("meet-1:station-MUN:reset-1", 1, 2));
  CHECK(!f.terminal.press('D')); CHECK(!f.terminal.press('#'));
  CHECK(f.terminal.press('9')); CHECK(f.terminal.press('A')); CHECK(f.commands() == 1);
}
void rejected_ack_retains_correctable_input() {
  Fixture f; CHECK(f.terminal.press('9')); CHECK(f.terminal.press('#'));
  f.receive(screen(), "ack", f.terminal.pending, "rejected");
  CHECK(f.terminal.pending.empty()); CHECK(f.terminal.digits == "9");
  hostMillis += 501; CHECK(f.terminal.press('B')); CHECK(f.commands() == 1);
}
void unrelated_ack_boot_and_retained_frame_are_ignored() {
  Fixture f; CHECK(f.terminal.press('#')); const auto pending = f.terminal.pending;
  f.receive(screen(), "ack", "wrong-command"); CHECK(f.terminal.pending == pending);
  f.receive(screen("wrong-station"), "frame", "", "accepted", "old-boot");
  f.receive(screen("wrong-station"), "frame", "", "accepted", "boot-1", true);
  CHECK(f.terminal.context() == "meet-1:station-MUN:reset-1"); CHECK(f.terminal.pending == pending);
}
void disconnect_and_reconnect_never_replay_commands() {
  Fixture f; CHECK(f.terminal.press('9')); CHECK(f.terminal.press('#'));
  f.mqtt.online = false; CHECK(!f.terminal.tick()); CHECK(!f.terminal.press('#'));
  CHECK(f.terminal.digits.empty()); CHECK(f.terminal.pending.empty());
  f.mqtt.online = true;
  f.terminal.begin(f.mqtt, "device-1", "TBX-123456", "host-test", "0.7.0", "boot-2");
  f.receive(screen(), "frame"); CHECK(!f.terminal.fresh);
  f.receive(screen(), "frame", "", "accepted", "boot-2");
  CHECK(f.terminal.fresh); CHECK(f.commands() == 1);
}
// Until 0.7.1 five seconds without an ack dropped the whole session. Against a
// slow server that turned into every box reconnecting at once, which only gave
// the server more to catch up on (measured 2026-09-30).
void a_slow_answer_keeps_the_session() {
  Fixture f; CHECK(f.terminal.press('9')); CHECK(f.terminal.press('#'));
  const auto pending = f.terminal.pending;
  CHECK(f.wait(20000)); CHECK(f.terminal.fresh);
  CHECK(f.terminal.pending == pending); CHECK(f.terminal.digits == "9");
  CHECK(!f.terminal.press('#')); CHECK(f.commands() == 1);
  f.receive(screen(), "ack", pending, "accepted");
  CHECK(f.terminal.pending.empty()); CHECK(f.terminal.digits.empty()); CHECK(f.commands() == 1);
}
void silence_still_ends_the_session_while_waiting() {
  Fixture f; CHECK(f.terminal.press('#'));
  CHECK(!f.wait(15000, false)); CHECK(!f.terminal.ready());
  CHECK(f.terminal.pending.empty()); CHECK(f.commands() == 1);
}
void a_lost_answer_gives_up_the_command_not_the_session() {
  Fixture f; CHECK(f.terminal.press('9')); CHECK(f.terminal.press('#'));
  const auto lost = f.terminal.pending;
  CHECK(f.wait(ServerTerminal::COMMAND_GIVE_UP_MS - 500)); CHECK(f.terminal.pending == lost);
  CHECK(f.wait(500)); CHECK(f.terminal.fresh);
  CHECK(f.terminal.pending.empty()); CHECK(f.terminal.digits == "9");
  CHECK(f.commands() == 1);                       // never retried by the box itself
  CHECK(f.wait(500)); CHECK(f.terminal.press('#')); CHECK(f.commands() == 2);
  f.receive(screen(), "ack", lost, "accepted");   // the late answer to the lost one
  CHECK(f.terminal.pending.length()); CHECK(f.terminal.digits == "9");
}
void presence_requires_matching_nonce() {
  Fixture f; hostMillis = 6100; CHECK(f.terminal.tick());
  JsonDocument alive; alive["boot"] = "boot-1"; alive["nonce"] = "wrong";
  String payload; serializeJson(alive, payload);
  const auto seen = f.terminal.seen;
  f.terminal.receive(f.terminal.prefix + "alive", payload, false); CHECK(f.terminal.seen == seen);
  alive["nonce"] = f.terminal.nonce; serializeJson(alive, payload);
  f.terminal.receive(f.terminal.prefix + "alive", payload, false); CHECK(f.terminal.seen == hostMillis);
  hostMillis += 15000; CHECK(!f.terminal.tick()); CHECK(f.commands() == 0);
}
void publish_failure_disables_input() {
  for (bool failBegin : {false, true}) {
    Fixture f; f.mqtt.beginOK = !failBegin; f.mqtt.endOK = failBegin;
    CHECK(!f.terminal.press('#')); CHECK(!f.terminal.ready()); CHECK(f.commands() == 0);
  }
}
void no_entry_no_digits_and_only_server_keys() {
  Fixture f; auto frame = screen(); frame.remove("entry"); frame["keys"].remove("A");
  f.receive(frame); hostMillis += ServerTerminal::GUARD_MS;  // new keys: a screen change
  CHECK(!f.terminal.press('9')); CHECK(!f.terminal.press('A')); CHECK(!f.terminal.press('Z'));
  CHECK(f.terminal.press('D')); CHECK(f.last()["key"] == "D"); CHECK(f.last()["action"].isNull());
}
void malformed_frames_cannot_replace_display() {
  Fixture f;
  for (int fault = 0; fault < 9; ++fault) {
    auto frame = screen("new-context");
    switch (fault) {
      case 0: frame["profile"] = "legacy"; break;
      case 1: frame["cols"] = 20; break;
      case 2: frame["lcd"]["cells"][0].as<JsonArray>().remove(15); break;
      case 3: frame["lcd"]["cells"][0][0] = 256; break;
      case 4: frame["lcd"]["cells"][0][0] = "Å"; break;
      case 5: frame["entry"]["column"] = 6; break;
      case 6: frame["lcd"]["glyphs"].add<JsonObject>()["slot"] = 8; break;
      case 7: frame["rows"] = 4; break;
      case 8: frame["entry"]["lcd"]["cells"][0][0] = -1; break;
    }
    f.receive(frame); CHECK(f.terminal.context() == "meet-1:station-MUN:reset-1");
  }
}
void raw_lcd_glyphs_and_clock_survive_local_input() {
  Fixture f; auto frame = screen();
  for (JsonObject object : {frame.as<JsonObject>(), frame["entry"].as<JsonObject>()}) {
    for (int slot = 0; slot < 8; ++slot) {
      auto glyph = object["lcd"]["glyphs"].add<JsonObject>(); glyph["slot"] = slot;
      auto rows = glyph["rows"].to<JsonArray>(); for (int r = 0; r < 8; ++r) rows.add(slot + r);
      object["lcd"]["cells"][1][slot] = slot;
    }
  }
  f.receive(frame); LCD lcd; f.terminal.draw(lcd);
  CHECK(lcd.glyphWrites == 8); CHECK(lcd.cells[1][0] == 0); CHECK(lcd.glyphs[7][7] == 14);
  CHECK(f.terminal.press('9')); CHECK(f.terminal.press('3')); f.terminal.draw(lcd);
  CHECK(lcd.cells[0][5] == '9' && lcd.cells[0][6] == '3' && lcd.cells[0][7] == '_');
  CHECK(lcd.cells[1][11] == '1' && lcd.cells[1][15] == '4');
  CHECK(f.commands() == 0);
  frame["entry"]["lcd"]["glyphs"][0]["rows"][0] = 31;
  frame["view_revision"] = 2; f.receive(frame); f.terminal.draw(lcd);
  CHECK(lcd.glyphs[0][0] == 31); CHECK(f.terminal.digits == "93");
  frame["lcd"]["glyphs"].add<JsonObject>(); CHECK(!f.terminal.validate(frame.as<JsonVariantConst>()));
}
void larger_physical_display_does_not_invent_another_profile() {
  Fixture f; LCD lcd; for (auto& row : lcd.cells) row.fill('X');
  f.terminal.draw(lcd, 20, 4); CHECK(lcd.writes == 80);
  for (int r = 0; r < 4; ++r) for (int c = 0; c < 20; ++c)
    if (r >= 2 || c >= 16) CHECK(lcd.cells[r][c] == ' ');
  f.terminal.draw(lcd, 20, 4); CHECK(lcd.writes == 80);
}

std::string row(const LCD& lcd, int r) {
  return std::string(lcd.cells[r].begin(), lcd.cells[r].begin() + 16);
}
void waiting_is_shown_until_the_answer() {
  Fixture f; f.terminal.waitingText = "VANTAR PA SVAR"; LCD lcd;
  f.terminal.draw(lcd); const auto idle = row(lcd, 1);
  CHECK(idle == "Nr# C/D    12:34");
  CHECK(f.terminal.press('#')); f.terminal.draw(lcd); CHECK(row(lcd, 1) == idle);
  CHECK(f.wait(1000)); f.terminal.draw(lcd); CHECK(row(lcd, 1) == idle);
  CHECK(f.wait(750)); f.terminal.draw(lcd); CHECK(row(lcd, 1) == "VANTAR PA SVAR  ");
  const auto writes = lcd.writes; CHECK(f.wait(2000)); f.terminal.draw(lcd);
  CHECK(lcd.writes == writes);                    // drawn once, not every loop
  f.receive(screen(), "ack", f.terminal.pending, "accepted"); f.terminal.draw(lcd);
  CHECK(row(lcd, 1) == idle);
}
void no_text_no_overlay() {
  Fixture f; LCD lcd; CHECK(f.terminal.press('#'));
  CHECK(f.wait(5000)); f.terminal.draw(lcd); CHECK(row(lcd, 1) == "Nr# C/D    12:34");
}
void a_given_up_command_is_said_briefly() {
  Fixture f; f.terminal.unansweredText = "INGET SVAR"; LCD lcd;
  CHECK(f.terminal.press('#')); CHECK(f.wait(ServerTerminal::COMMAND_GIVE_UP_MS));
  f.terminal.draw(lcd); CHECK(row(lcd, 1) == "INGET SVAR      ");
  CHECK(f.wait(ServerTerminal::UNANSWERED_SHOWN_MS)); f.terminal.draw(lcd);
  CHECK(row(lcd, 1) == "Nr# C/D    12:34");
}

// The box's own text uses the server's 5x8 glyphs (terminal16_glyphs.py).
const std::array<uint8_t, 8> RING_A{4, 10, 4, 14, 17, 31, 17, 17};      // Å
const std::array<uint8_t, 8> DOTS_A{10, 0, 14, 17, 31, 17, 17, 0};      // Ä
const std::array<uint8_t, 8> DOTS_O{10, 0, 14, 17, 17, 17, 14, 0};      // Ö
const std::array<uint8_t, 8> RIGHT{16, 24, 28, 30, 28, 24, 16, 0};      // ▶
std::string cells(std::initializer_list<std::string> parts) {
  std::string out; for (const auto& part : parts) out += part; return out;
}
const std::string SLOT0(1, '\0'), SLOT1(1, '\1'), SLOT2(1, '\2');

void own_text_is_drawn_with_the_servers_glyphs() {
  // Until 0.7.4 the box folded Ö to O itself. One glyph, defined once, in
  // the first free slot; the line is sixteen characters, not bytes.
  TrainMeetLcd::Screen screen(2, 16);
  screen.line(0, "SÖKER SERVER"); screen.line(1, "FÖRSÖKER IGEN");
  LCD lcd; screen.draw(lcd);
  CHECK(row(lcd, 0) == cells({"S", SLOT0, "KER SERVER    "}));
  CHECK(row(lcd, 1) == cells({"F", SLOT0, "RS", SLOT0, "KER IGEN   "}));
  CHECK(lcd.glyphWrites == 1); CHECK(lcd.glyphs[0] == DOTS_O);
  screen.line(1, "BE ADMIN HJÄLPA");
  CHECK(screen.cells[0][1] == 0);  // the first row keeps its slot
  LCD again; screen.draw(again); CHECK(again.glyphs[1] == DOTS_A);
  CHECK(row(again, 1) == cells({"BE ADMIN HJ", SLOT1, "LPA "}));
}
void own_text_never_redefines_a_slot_in_use() {
  TrainMeetLcd::Screen full(1, 16);
  for (uint8_t slot = 0; slot < 8; ++slot) { const uint8_t bits[8] = {slot}; full.reserve(slot, bits); }
  full.line(0, "NÄT SAKNAS");
  CHECK(std::string(full.cells[0], full.cells[0] + 16) == "NAT SAKNAS      ");  // folded, not drawn over
  TrainMeetLcd::Screen many(1, 16);
  many.line(0, "ÅÄÖåäöÆØÜ");                       // nine different glyphs, eight slots
  CHECK(many.cells[0][7] == 7); CHECK(many.cells[0][8] == 'U');
  TrainMeetLcd::Screen broken(1, 4);
  broken.line(0, "A\xC3");                        // a cut UTF-8 sequence is one cell
  CHECK(std::string(broken.cells[0], broken.cells[0] + 4) == "A?  ");
  TrainMeetLcd::Screen decomposed(1, 4);
  decomposed.line(0, "A\xCC\x8A" "B");                // A + combining ring: never a cell of its own
  CHECK(std::string(decomposed.cells[0], decomposed.cells[0] + 4) == "AB  ");
}
void folded_keys_and_fitted_lines() {
  // The server's catalog is keyed on the folded Swedish.
  char key[48];
  TrainMeetLcd::foldText("FÖRSÖKER IGEN", key, sizeof key); CHECK(std::string(key) == "FORSOKER IGEN");
  TrainMeetLcd::foldText("VÄNTAR PÅ SVAR", key, sizeof key); CHECK(std::string(key) == "VANTAR PA SVAR");
  char line[64];
  TrainMeetLcd::fitText("SÖKER SERVER", 16, line, sizeof line);
  CHECK(std::string(line) == "SÖKER SERVER    ");   // sixteen characters, seventeen bytes
  TrainMeetLcd::fitText("HÅRDVARUTEST OCH MER", 16, line, sizeof line);
  CHECK(std::string(line) == "HÅRDVARUTEST OCH");
}
void the_waiting_overlay_draws_its_own_letters() {
  // Before 0.7.5 the overlay's UTF-8 bytes went straight to the display:
  // VÄNTAR PÅ SVAR became V, 0xC3, 0x84, NTAR P, 0xC3, 0x85, SVAR.
  Fixture f; f.terminal.waitingText = "VÄNTAR PÅ SVAR"; LCD lcd;
  auto frame = screen("meet-1:station-MUN:reset-1", 1, 2);
  for (JsonObject object : {frame.as<JsonObject>(), frame["entry"].as<JsonObject>()}) {
    int slot = 0;
    for (const auto& bitmap : {RIGHT, DOTS_A}) {
      auto glyph = object["lcd"]["glyphs"].add<JsonObject>(); glyph["slot"] = slot;
      auto rows = glyph["rows"].to<JsonArray>(); for (uint8_t bits : bitmap) rows.add(bits);
      object["lcd"]["cells"][0][slot] = slot; ++slot;
    }
  }
  f.receive(frame); CHECK(f.wait(600)); f.terminal.draw(lcd);  // past the guard after a new screen
  CHECK(f.terminal.press('#')); CHECK(f.wait(1750)); f.terminal.draw(lcd);
  // Ä shares the frame's identical slot 1, Å takes the first free one; the
  // frame's first row and glyphs are untouched.
  CHECK(row(lcd, 1) == cells({"V", SLOT1, "NTAR P", SLOT2, " SVAR  "}));
  CHECK(lcd.glyphs[0] == RIGHT); CHECK(lcd.glyphs[1] == DOTS_A); CHECK(lcd.glyphs[2] == RING_A);
  CHECK(lcd.cells[0][0] == 0 && lcd.cells[0][1] == 1);
}

int main() {
  using Test = void (*)();
  const Test tests[] = {hello_and_presence_are_not_assignments, digits_stay_local_until_confirm,
    local_edit_cancel_and_queue, length_and_entry_context_are_checked, placement_refresh_keeps_unsubmitted_digits,
    older_views_and_revisions_do_not_roll_back, changed_station_meet_or_reset_clears_input,
    accepted_and_duplicate_ack_clear_input_and_guard_double_press, browsing_answers_at_once,
    a_new_screen_guards_only_acting_keys, a_pushed_screen_change_guards_too,
    an_unchanged_screen_or_a_new_minute_does_not_guard, digits_and_search_are_never_guarded,
    a_key_without_the_flag_counts_as_acting, rejected_ack_retains_correctable_input,
    unrelated_ack_boot_and_retained_frame_are_ignored, disconnect_and_reconnect_never_replay_commands,
    a_slow_answer_keeps_the_session, silence_still_ends_the_session_while_waiting,
    a_lost_answer_gives_up_the_command_not_the_session, waiting_is_shown_until_the_answer, no_text_no_overlay,
    a_given_up_command_is_said_briefly, presence_requires_matching_nonce, publish_failure_disables_input,
    no_entry_no_digits_and_only_server_keys, malformed_frames_cannot_replace_display,
    raw_lcd_glyphs_and_clock_survive_local_input, larger_physical_display_does_not_invent_another_profile,
    own_text_is_drawn_with_the_servers_glyphs, own_text_never_redefines_a_slot_in_use, folded_keys_and_fitted_lines,
    the_waiting_overlay_draws_its_own_letters};
  try {
    for (const auto test : tests) test();
    std::cout << "PASS " << std::size(tests) << " shared terminal contract scenarios\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return EXIT_FAILURE; }
}
