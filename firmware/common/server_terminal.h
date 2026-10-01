#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoMqttClient.h>

// Shared by ESP8266 and ESP32. No routes, train states or action names.
// The server supplies pixels, text and key meanings; only digits stay here.
//
// A slow answer is not a dead server. Until 0.7.1 a command without an ack
// within five seconds dropped the whole session: the box disconnected,
// searched for the server again and sent a new hello. Measured on 2026-09-30,
// that was how a slow server turned into every box falling over at once, and
// every reconnect added more work for the server to catch up on. Now a command
// waits as long as the server keeps answering presence. Only silence - no
// alive or frame for fifteen seconds - ends a session.
class ServerTerminal {
 public:
  static constexpr uint32_t WAITING_SHOWN_MS = 1500;
  // A lost ack must not lock the keypad for as long as the session lives. The
  // command is given up, the session is not; digits stay for another try, and
  // the server's view token decides whether that try still applies.
  static constexpr uint32_t COMMAND_GIVE_UP_MS = 30000;
  static constexpr uint32_t UNANSWERED_SHOWN_MS = 3000;
  // A press meant for the previous screen must not act on the new one. After
  // a screen change - a new view token or new key meanings - keys that act on
  // traffic wait this long. Browsing, digits and train search never wait;
  // until 0.7.2 every key waited after every answer, which made browsing
  // slow. A key without the server's "acts" flag counts as acting.
  static constexpr uint32_t GUARD_MS = 500;

  JsonDocument frame;
  String prefix, boot, digits, pending, nonce, message;
  // Drawn on the second row while a command waits, so an operator does not
  // take a slow answer for a missed key, and briefly after one is given up.
  // Set by the sketch in its own language; empty means nothing is drawn.
  String waitingText, unansweredText;
  uint32_t sequence = 0, seen = 0, ping = 0, sent = 0, guard = 0, unansweredAt = 0;
  bool started = false, fresh = false, dirty = true, waitingDrawn = false, unanswered = false;
  MqttClient* client = nullptr;

  bool publish(const char* leaf, JsonDocument& body) {
    if (!client || !client->connected()) return false;
    body["boot"] = boot;
    String encoded; serializeJson(body, encoded);
    if (!client->beginMessage((prefix + leaf).c_str(), encoded.length(), false, 1)) return false;
    client->print(encoded); return client->endMessage() == 1;
  }
  void reset() {
    fresh = started = false; dirty = true; digits = pending = ""; frame.clear();
    waitingDrawn = unanswered = false;
  }
  bool waiting() const { return pending.length() && uint32_t(millis() - sent) >= WAITING_SHOWN_MS; }
  void begin(MqttClient& mqtt, const String& id, const String& code, const char* model, const char* version, const String& connectionId) {
    reset(); client = &mqtt; prefix = "tmbox/terminal/device/" + id + "/";
    boot = connectionId; started = true; seen = ping = millis();
    for (const char* leaf : {"frame", "ack", "alive"}) mqtt.subscribe(prefix + leaf, 1);
    JsonDocument hello;
    hello["device_code"] = code; hello["model"] = model; hello["firmware_version"] = version;
    hello["hardware_version"] = "server-16x2";
    publish("hello", hello);
  }
  bool ready() const { return started && fresh && client && client->connected() && pending.length() == 0 &&
      uint32_t(millis() - seen) < 15000; }
  bool guarded(const String& name) const {
    return int32_t(millis() - guard) < 0 && !digits.length() && (frame["keys"][name]["acts"] | true);
  }
  bool screenChanged(JsonVariantConst next) const {
    if (!fresh || token() != String(next["view_token"] | "")) return true;
    String before, after; serializeJson(frame["keys"], before); serializeJson(next["keys"], after);
    return before != after;
  }
  bool hasEntry() const { return frame["entry"].is<JsonObjectConst>(); }
  String token() const { return frame["view_token"] | ""; }
  String context() const { return frame["entry"]["context"] | ""; }
  bool validate(JsonVariantConst next) {
    if (String(next["profile"] | "") != "server-16x2" || next["rows"] != 2 || next["cols"] != 16) return false;
    const JsonVariantConst views[] = {next, next["entry"]};
    for (JsonVariantConst view : views) {
      if (view.isNull()) continue;
      JsonArrayConst cells = view["lcd"]["cells"].as<JsonArrayConst>();
      if (cells.size() != 2 || view["lcd"]["glyphs"].size() > 8) return false;
      for (JsonArrayConst row : cells) {
        if (row.size() != 16) return false;
        for (JsonVariantConst cell : row) if (!cell.is<int>() || cell.as<int>() < 0 || cell.as<int>() > 126) return false;
      }
      for (JsonObjectConst glyph : view["lcd"]["glyphs"].as<JsonArrayConst>()) {
        if (!glyph["slot"].is<int>() || glyph["slot"].as<int>() < 0 || glyph["slot"].as<int>() > 7 || glyph["rows"].size() != 8) return false;
        for (JsonVariantConst bits : glyph["rows"].as<JsonArrayConst>())
          if (!bits.is<int>() || bits.as<int>() < 0 || bits.as<int>() > 31) return false;
      }
    }
    if (!next["entry"].isNull() && (next["entry"]["row"] != 0 || next["entry"]["column"] != 5 || next["entry"]["max_length"] != 5)) return false;
    return true;
  }
  void receive(const String& topic, const String& payload, bool retained) {
    if (!started || retained || !topic.startsWith(prefix) || payload.length() > 8192) return;
    JsonDocument doc;
    if (deserializeJson(doc, payload, DeserializationOption::NestingLimit(10)) || String(doc["boot"] | "") != boot) return;
    if (topic.endsWith("/alive")) {
      if (String(doc["nonce"] | "") == nonce) seen = millis();
      return;
    }
    if (!validate(doc["frame"])) return;
    if (fresh && String(frame["entry"]["context"] | "") == String(doc["frame"]["entry"]["context"] | "")) {
      const uint32_t revision = doc["frame"]["revision"] | 0;
      const uint32_t localRevision = frame["revision"] | 0;
      if (revision < localRevision || (revision == localRevision &&
          (doc["frame"]["view_revision"] | 0UL) < (frame["view_revision"] | 0UL))) return;
    }
    if (topic.endsWith("/ack")) {
      if (String(doc["command_id"] | "") != pending) return;
      const String status = doc["status"] | "";
      if (status == "accepted" || status == "duplicate") digits = "";
      pending = ""; waitingDrawn = false; message = doc["message"] | "";
    }
    if (context() != String(doc["frame"]["entry"]["context"] | "")) digits = "";
    if (screenChanged(doc["frame"])) guard = millis() + GUARD_MS;
    frame.set(doc["frame"]); seen = millis(); fresh = true; dirty = true;
  }
  bool replaceDigits(const String& entryContext, const String& number) {
    if (!ready() || !hasEntry() || context() != entryContext || !number.length() || number.length() > 5) return false;
    for (unsigned i=0; i<number.length(); ++i) if (number[i] < '0' || number[i] > '9') return false;
    digits = number; return true;
  }
  bool press(char key) {
    if (!ready()) return false;
    if (key >= '0' && key <= '9') {
      if (!hasEntry() || digits.length() >= 5) return false;
      digits += key; dirty = true; return true;
    }
    String name(key);
    if (guarded(name)) return false;
    if (digits.length()) {
      if (key == '*') { digits = ""; dirty = true; return true; }
      if (key == 'B') { digits.remove(digits.length()-1); dirty = true; return true; }
      if (key == 'A') digits = "";
      else if (key != '#') return false;
    }
    if (!digits.length() && !frame["keys"][name].is<JsonObjectConst>()) return false;
    JsonDocument command;
    pending = boot + "-" + String(++sequence);
    command["command_id"] = pending; command["view_token"] = token(); command["key"] = name;
    if (key == '#' && digits.length()) { command["train_number"] = digits; command["entry_context"] = context(); }
    sent = millis(); dirty = true; waitingDrawn = false; unanswered = false;
    if (!publish("command", command)) { pending = ""; fresh = false; return false; }
    return true;
  }
  bool tick() {
    if (!started) return false;
    const uint32_t now = millis();
    if (!client || !client->connected() || uint32_t(now-seen) >= 15000) {
      fresh = false; digits = pending = ""; dirty = true; return false;
    }
    if (pending.length()) {
      const uint32_t age = uint32_t(now - sent);
      if (age >= COMMAND_GIVE_UP_MS) {
        pending = ""; waitingDrawn = false; unanswered = true; unansweredAt = now;
        dirty = true;
      } else if (age >= WAITING_SHOWN_MS && !waitingDrawn) {
        waitingDrawn = true; dirty = true;
      }
    }
    if (unanswered && uint32_t(now - unansweredAt) >= UNANSWERED_SHOWN_MS) { unanswered = false; dirty = true; }
    if (uint32_t(now-ping) >= 5000) {
      ping = now; nonce = boot + "-p" + String(++sequence);
      JsonDocument request; request["nonce"] = nonce; publish("presence", request);
    }
    return true;
  }
  static const String& empty() { static const String none; return none; }
  String line(uint8_t row) const { return frame["lines"][row] | ""; }
  String allowed() const {
    String result;
    for (JsonPairConst item : frame["keys"].as<JsonObjectConst>()) result += item.key().c_str();
    if (hasEntry()) result += "0123456789";
    return result;
  }
  template <class LCD> void draw(LCD& lcd, uint8_t cols=16, uint8_t rows=2) {
    if (!fresh || !dirty) return;
    JsonVariantConst view = digits.length() ? frame["entry"].as<JsonVariantConst>() : frame.as<JsonVariantConst>();
    const String& overlay = waiting() ? waitingText : (unanswered ? unansweredText : empty());
    for (JsonObjectConst glyph : view["lcd"]["glyphs"].as<JsonArrayConst>()) {
      uint8_t bits[8]; for (uint8_t i=0;i<8;++i) bits[i]=glyph["rows"][i].as<uint8_t>();
      lcd.createChar(glyph["slot"].as<uint8_t>(), bits);
    }
    for (uint8_t r=0;r<rows;++r) {
      lcd.setCursor(0,r);
      for (uint8_t c=0;c<cols;++c) {
        uint8_t value = (r<2 && c<16) ? view["lcd"]["cells"][r][c].as<uint8_t>() : ' ';
        if (digits.length() && r==0 && c>=5 && c<10) value = (c-5<digits.length()) ? digits[c-5] : '_';
        if (r == 1 && c < 16 && overlay.length()) value = c < overlay.length() ? uint8_t(overlay[c]) : ' ';
        lcd.write(value);
      }
    }
    dirty = false;
  }
};
