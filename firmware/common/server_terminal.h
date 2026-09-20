#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoMqttClient.h>

// Shared by ESP8266 and ESP32. No routes, train states or action names.
// The server supplies pixels, text and key meanings; only digits stay here.
class ServerTerminal {
 public:
  JsonDocument frame;
  String prefix, boot, digits, pending, nonce, message;
  uint32_t sequence = 0, seen = 0, ping = 0, sent = 0, guard = 0;
  bool started = false, fresh = false, dirty = true;
  MqttClient* client = nullptr;

  bool publish(const char* leaf, JsonDocument& body) {
    if (!client || !client->connected()) return false;
    body["boot"] = boot;
    String encoded; serializeJson(body, encoded);
    if (!client->beginMessage((prefix + leaf).c_str(), encoded.length(), false, 1)) return false;
    client->print(encoded); return client->endMessage() == 1;
  }
  void reset() { fresh = started = false; dirty = true; digits = pending = ""; frame.clear(); }
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
      uint32_t(millis() - seen) < 15000 && int32_t(millis() - guard) >= 0; }
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
      pending = ""; message = doc["message"] | "";
      guard = millis() + 500;
    }
    if (context() != String(doc["frame"]["entry"]["context"] | "")) digits = "";
    if (!digits.length() && String(frame["keys"]["#"]["label"] | "") != String(doc["frame"]["keys"]["#"]["label"] | "")) guard = millis() + 500;
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
    if (digits.length()) {
      if (key == '*') { digits = ""; dirty = true; return true; }
      if (key == 'B') { digits.remove(digits.length()-1); dirty = true; return true; }
      if (key == 'A') digits = "";
      else if (key != '#') return false;
    }
    String name(key);
    if (!digits.length() && !frame["keys"][name].is<JsonObjectConst>()) return false;
    JsonDocument command;
    pending = boot + "-" + String(++sequence);
    command["command_id"] = pending; command["view_token"] = token(); command["key"] = name;
    if (key == '#' && digits.length()) { command["train_number"] = digits; command["entry_context"] = context(); }
    sent = millis(); dirty = true;
    if (!publish("command", command)) { pending = ""; fresh = false; return false; }
    return true;
  }
  bool tick() {
    if (!started) return false;
    const uint32_t now = millis();
    if (!client || !client->connected() || uint32_t(now-seen) >= 15000 || (pending.length() && uint32_t(now-sent) >= 5000)) {
      fresh = false; digits = pending = ""; dirty = true; return false;
    }
    if (uint32_t(now-ping) >= 5000) {
      ping = now; nonce = boot + "-p" + String(++sequence);
      JsonDocument request; request["nonce"] = nonce; publish("presence", request);
    }
    return true;
  }
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
    for (JsonObjectConst glyph : view["lcd"]["glyphs"].as<JsonArrayConst>()) {
      uint8_t bits[8]; for (uint8_t i=0;i<8;++i) bits[i]=glyph["rows"][i].as<uint8_t>();
      lcd.createChar(glyph["slot"].as<uint8_t>(), bits);
    }
    for (uint8_t r=0;r<rows;++r) {
      lcd.setCursor(0,r);
      for (uint8_t c=0;c<cols;++c) {
        uint8_t value = (r<2 && c<16) ? view["lcd"]["cells"][r][c].as<uint8_t>() : ' ';
        if (digits.length() && r==0 && c>=5 && c<10) value = (c-5<digits.length()) ? digits[c-5] : '_';
        lcd.write(value);
      }
    }
    dirty = false;
  }
};
