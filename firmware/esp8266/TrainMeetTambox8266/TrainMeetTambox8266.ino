/* TrainMeet TMBox for NodeMCU ESP8266 + PCF8574 keypad + 16x2 I2C LCD.
 * Arduino IDE: open this entire folder, not only the .ino file.
 * The local TrainMeet Server owns every traffic decision. No Cloud runtime.
 */
#include <Arduino.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <ArduinoMqttClient.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <LiquidCrystal_PCF8574.h>
#include <WiFiManager.h>
#include <stddef.h>
#include "hardware_profile.h"
#include "debug_log.h"
#include "input_state.h"
#include "pcf_keypad.h"
#include "network_setup.h"
#include "web_test_state.h"
#include "server_sync.h"
#include "language_menu.h"
#include "../../common/server_terminal.h"
#include "../../common/server_discovery_arduino.h"

#ifndef ESP8266
#error "Choose NodeMCU 1.0 (ESP-12E Module), not an ESP32 board."
#endif

LiquidCrystal_PCF8574 lcd(TAMBOX_LCD_ADDRESS);
WiFiClient networkClient;
MqttClient mqtt(networkClient);
ServerTerminal terminal;
WiFiManager wifiManager;
KeyState keys;
InputLease lease;
LocalTrainEntry trainEntry;
bool enteringTrain = false;
WebTestSession webSession;
EnrollmentReadiness serverEnrollment;
ServerSync serverSync;
tmbox::LanguageMenu languageMenu;
JsonDocument deviceUi;
bool languageReady = false, idleScreen = false;
String meetingTime = "--:--";
String uiText(const char* key) { return deviceUi["messages"][key] | key; }

// The old connection settings occupied the beginning of EEPROM. Reserve a
// separate bounded cache at the end; never overwrite those settings.
void cacheDeviceUI(bool write) {
  constexpr int offset = 2048, capacity = 2032;
  EEPROM.begin(4096);
  if (write) {
    String body; serializeJson(deviceUi, body);
    if (body.length() < capacity) {
      uint32_t hash = 2166136261u;
      for (size_t i = 0; i < body.length(); ++i) hash = (hash ^ uint8_t(body[i])) * 16777619u;
      uint32_t oldHash; EEPROM.get(offset + 8, oldHash);
      if (hash != oldHash) {
        EEPROM.put(offset, uint32_t(0x544d5549)); EEPROM.put(offset + 4, uint32_t(body.length())); EEPROM.put(offset + 8, hash);
        for (size_t i = 0; i < body.length(); ++i) EEPROM.write(offset + 12 + i, body[i]);
        EEPROM.commit();
      }
    }
  } else {
    uint32_t magic, size, expected; EEPROM.get(offset, magic); EEPROM.get(offset + 4, size); EEPROM.get(offset + 8, expected);
    if (magic == 0x544d5549 && size > 0 && size < capacity) {
      String body; body.reserve(size); uint32_t hash = 2166136261u;
      for (uint32_t i = 0; i < size; ++i) { char c = EEPROM.read(offset + 12 + i); body += c; hash = (hash ^ uint8_t(c)) * 16777619u; }
      if (hash == expected && deserializeJson(deviceUi, body)) deviceUi.clear();
    }
  }
  EEPROM.end();
}
String deviceId, deviceCode, bootId, apName;
String gatewayHost, panelId, sessionId, allowedKeys, commandId;
String rememberedServerId, discoveredServerId;
WiFiManagerParameter forgetServer("forgetserver", "Byt TrainMeet Server (behall Wi-Fi)", "1", 1, "type=\"checkbox\"", WFM_LABEL_AFTER);
bool forgetServerRequested = false;
String shownLine1, shownLine2;
String serverLine1, serverLine2;
String stateToken, stateRequestId;
long revision = -1;
uint16_t gatewayPort = 1883;
bool lcdFound = false, keypadOK = false;
bool portalActive = false, saveRequested = false, mdnsStarted = false;
bool connectedBefore = false, wifiWasConnected = false, refreshRequested = false;
uint32_t nextConnection = 0, wifiLostAt = 0, lastKeyScan = 0;
uint32_t lastLcdCheck = 0;
uint32_t commandSequence = 0;
uint32_t stateSequence = 0;
unsigned connectionFailures = 0;

String inputLine1();
String inputLine2();

#ifndef TAMBOX_HARDWARE_CHECK
void stopWebTestServer();
#endif

bool due(uint32_t now, uint32_t when) { return int32_t(now - when) >= 0; }

String lcdLine(String value) {
  // The usual HD44780 ROM is not UTF-8. Keep cell alignment for Swedish text.
  value.replace("å", "a"); value.replace("ä", "a"); value.replace("ö", "o");
  value.replace("Å", "A"); value.replace("Ä", "A"); value.replace("Ö", "O");
  String line;
  for (size_t i = 0; i < value.length() && line.length() < 16; ++i) {
    const uint8_t c = value[i];
    if (c >= 32 && c < 127) line += char(c);
    else if (c >= 192) line += '?'; // One placeholder per other UTF-8 character.
  }
  while (line.length() < 16) line += ' ';
  return line;
}

void showFrame(const String& one, const String& two) {
  const String first = lcdLine(one), second = lcdLine(two);
  if (first == shownLine1 && second == shownLine2) return;
  shownLine1 = first; shownLine2 = second;
  TMBOX_LOG("LCD |%s|%s|\n", first.c_str(), second.c_str());
  if (lcdFound) {
    lcd.setCursor(0, 0); lcd.print(first);
    lcd.setCursor(0, 1); lcd.print(second);
  }
}

void invalidate() {
  trainEntry.clear(); enteringTrain = false;
  lease.clear(); panelId = ""; sessionId = ""; allowedKeys = ""; revision = -1;
  commandId = ""; keys.requireRelease();
  serverLine1 = ""; serverLine2 = "";
  stateToken = ""; stateRequestId = "";
}

void disconnectServer() {
  terminal.reset();
  languageReady = false; languageMenu.open = languageMenu.saving = false;
  invalidate(); mqtt.stop(); connectedBefore = false;
  serverEnrollment.clear();
  serverSync.reset();
  webSession.enabled = false;
}

void startPortal() {
  if (portalActive) return;
#ifndef TAMBOX_HARDWARE_CHECK
  stopWebTestServer(); // The setup portal and test page share port 80.
#endif
  disconnectServer();
  forgetServer.setValue("1", 1);
  wifiManager.startConfigPortal(apName.c_str());
  portalActive = wifiManager.getConfigPortalActive();
  if (portalActive) showFrame(uiText("INSTALLERA WIFI"), apName);
}

void saveServerBinding(const String& id) {
  EEPROM.begin(4096);
  EEPROM.put(1024, TrainMeetNetwork::bindingRecord(id.c_str()));
  EEPROM.commit(); EEPROM.end();
  rememberedServerId = id;
}

void savePortalSettings() {
  if (!saveRequested) return;
  saveRequested = false;
  disconnectServer(); gatewayHost = ""; nextConnection = millis(); wifiLostAt = millis();
  // The portal can switch networks between two loop iterations. Recreate
  // mDNS even when the main loop did not observe a disconnected Wi-Fi state.
  if (mdnsStarted) MDNS.close();
  mdnsStarted = false;
  portalActive = TrainMeetNetwork::finishSavedPortal(wifiManager, WiFi.status() == WL_CONNECTED);
}

bool publish(const String& topic, JsonDocument& document, bool retained = false) {
  if (!mqtt.beginMessage(topic, (unsigned long)measureJson(document), retained, 1)) {
    TMBOX_DEBUG("MQTT publish could not start\n"); return false;
  }
  serializeJson(document, mqtt);
  return mqtt.endMessage() == 1;
}

void hello() {
  JsonDocument message;
  message["protocol_version"] = 1;
  message["device_code"] = deviceCode;
  message["model"] = TAMBOX_MODEL;
  message["hardware_version"] = "nodemcu-pcf8574-16x2";
  message["display"]["rows"] = 2;
  message["display"]["cols"] = 16;
  message["display"]["charset"] = "ascii";
  message["firmware_version"] = TAMBOX_FIRMWARE_VERSION;
  message["wifi_rssi"] = WiFi.RSSI();
  // QoS1 publication can poll incoming replies before returning.
  serverSync.sent(ServerSync::Assignment, millis());
  publish("tambox/v1/device/" + deviceId + "/hello", message);
}

void requestState() {
  // Existing v1 servers answer presence with a snapshot. Updated servers only
  // acknowledge unchanged state, without re-sending assignment or display.
  JsonDocument message;
  stateRequestId = bootId + "-" + String(++stateSequence);
  message["status"] = "online";
  message["request_id"] = stateRequestId;
  message["panel_id"] = panelId;
  if (lease.allowed(millis()) && !refreshRequested) message["state_token"] = stateToken;
  serverSync.sent(ServerSync::State, millis());
  refreshRequested = false;
  publish("tambox/v1/client/" + deviceId + "/presence", message);
}

void receiveMessage(int size) {
  if (terminal.started) {
    if (size < 2 || size > 8192) { disconnectServer(); return; }
    const String topic = mqtt.messageTopic();
    const bool retained = mqtt.messageRetain();
    String body; body.reserve(size);
    while (mqtt.available()) body += char(mqtt.read());
    terminal.receive(topic, body, retained);
    if (lcdFound) terminal.draw(lcd);
    return;
  }
  const String topic = mqtt.messageTopic();
  const bool retained = mqtt.messageRetain();
  // Filter unused fields (routes/slots/ack snapshots) before allocating JSON.
  if (size < 2 || size > 8192) { TMBOX_DEBUG("MQTT message rejected: bytes=%d\n", size); invalidate(); serverSync.reset(); return; }
  JsonDocument filter, message;
  for (const char* key : {"status", "device_id", "protocol_version", "station_id", "panel_id", "traffic_session_id", "revision", "command_id", "reason", "state_token", "request_id"}) filter[key] = true;
  filter["assigned_panel_ids"][0] = true;
  filter["ui"] = true;
  filter["clock"]["time"] = true;
  filter["display"]["line1"] = true; filter["display"]["line2"] = true;
  filter["interaction"]["allowed_keys"][0] = true;
  for (const char* key : {"mode", "selected_slot", "owner_client_id", "train_number", "local_train_entry"})
    filter["interaction"][key] = true;
  if (deserializeJson(message, mqtt, DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(10))) {
    TMBOX_DEBUG("MQTT JSON rejected (payload not logged)\n");
    invalidate(); serverSync.reset(); return;
  }
  if (topic.endsWith("/preferences")) {
    if (retained) return;
    if (message["ui"]["version"] == 1 && message["ui"]["messages"].is<JsonObject>()) {
      deviceUi.set(message["ui"]); cacheDeviceUI(true);
      languageMenu.options.clear();
      for (JsonObject option : deviceUi["languages"].as<JsonArray>())
        languageMenu.options.push_back({option["code"] | "", option["name"] | ""});
      languageReady = true;
    }
    languageMenu.reply(message["request_id"] | "", String(message["status"] | "") == "accepted");
    keys.requireRelease();
    if (serverLine1.length()) showFrame(inputLine1(), inputLine2());
  } else if (topic.endsWith("/assignment")) {
    const String status = message["status"] | "";
    const String nextPanel = message["assigned_panel_ids"][0] | "";
    if (retained || message["protocol_version"] != 1 || deviceId != (message["device_id"] | "") ||
        (status != "assigned" && status != "waiting_for_assignment")) return;
    serverEnrollment.observe(retained, message["protocol_version"] | 0,
                             message["device_id"] | "", deviceId.c_str(), status.c_str());
    serverSync.assignmentReceived(millis());
    refreshRequested = false;
    TMBOX_DEBUG("Assignment: status=%.32s panel=%.80s\n", status.c_str(), nextPanel.c_str());
    if (status == "assigned" && !nextPanel.length() && String(message["station_id"] | "").length()) {
      invalidate(); showFrame(uiText("V1-PANEL SAKNAS"), uiText("KOLLA SERVERN")); return;
    }
    if (status != "assigned" || !nextPanel.length()) {
      invalidate(); showFrame(uiText("KOPPLA BOXEN"), deviceCode); return;
    }
    if (panelId != nextPanel) {
      invalidate(); panelId = nextPanel; refreshRequested = true;
      showFrame(uiText("BOX KOPPLAD"), uiText("HAMTAR PANEL..."));
    }
  } else if (topic.indexOf("/snapshot/") >= 0) {
    if (retained || !serverEnrollment.ready(mqtt.connected()) || !panelId.length() || panelId != (message["panel_id"] | "") ||
        !message["revision"].is<long>() || !message["display"]["line1"].is<const char*>() ||
        !message["display"]["line2"].is<const char*>() || !message["interaction"]["allowed_keys"].is<JsonArray>()) {
      TMBOX_DEBUG("Snapshot ignored: retained, wrong panel or invalid fields\n"); return;
    }
    const String nextSession = message["traffic_session_id"] | "";
    if (!nextSession.length()) return;
    const long nextRevision = message["revision"];
    if (nextRevision < 0 || (nextSession == sessionId && nextRevision < revision)) {
      TMBOX_DEBUG("Snapshot ignored: stale revision=%ld current=%ld\n", nextRevision, revision); return;
    }
    if (nextSession != sessionId || nextRevision != revision) {
      TMBOX_DEBUG("Snapshot accepted: revision=%ld\n", nextRevision);
    }
    sessionId = nextSession; revision = nextRevision;
    allowedKeys = "";
    for (JsonVariant key : message["interaction"]["allowed_keys"].as<JsonArray>()) {
      const String value = key.as<String>();
      if (value.length() == 1 && strchr(TAMBOX_KEYS, value[0])) allowedKeys += value;
    }
    lease.snapshot(millis(), false);
    serverSync.stateReceived(millis()); refreshRequested = false; stateRequestId = "";
    stateToken = message["state_token"] | "";
    serverLine1 = lcdLine(message["display"]["line1"].as<String>());
    serverLine2 = lcdLine(message["display"]["line2"].as<String>());
    enteringTrain = String(message["interaction"]["mode"] | "") == "enter_train";
    idleScreen = String(message["interaction"]["mode"] | "") == "idle";
    meetingTime = String(message["clock"]["time"] | "--:--").substring(0, 5);
    // An incoming traffic request takes priority over the presentation menu.
    if (!idleScreen) languageMenu.open = false;
    const String initial = message["interaction"]["train_number"] | "";
    const String owner = message["interaction"]["owner_client_id"] | "";
    const String context = sessionId + "|" + panelId + "|" +
      String(message["interaction"]["selected_slot"] | "") + "|" + owner + "|" + initial;
    trainEntry.sync(enteringTrain && message["interaction"]["local_train_entry"] == true && owner == deviceId,
                    context.c_str(), initial.c_str());
    if (keypadOK && lcdFound) showFrame(inputLine1(), inputLine2());
  } else if (topic.endsWith("/state")) {
    // A heartbeat is not an assignment, command ACK, or permission to revive
    // expired input. Only our latest request and exact snapshot may renew it.
    if (retained || !stateRequestId.length() || stateRequestId != (message["request_id"] | "") ||
        !serverEnrollment.ready(mqtt.connected())) return;
    const String status = message["status"] | "";
    if (status == "current" && stateToken.length() && stateToken == (message["state_token"] | "") &&
        panelId.length() && panelId == (message["panel_id"] | "")) {
      if (!lease.heartbeat(millis())) return;
    } else if (status != "waiting_for_assignment" || panelId.length()) return;
    stateRequestId = ""; serverSync.stateReceived(millis());
  } else if (topic.endsWith("/ack")) {
    if (!lease.waiting || commandId != (message["command_id"] | "")) return;
    TMBOX_DEBUG("Command acknowledgement: status=%.32s\n", message["status"] | "");
    // Do not resend unacknowledged input or enable keys until a new snapshot.
    lease.acknowledged(); commandId = ""; refreshRequested = true;
    if (String(message["status"] | "") == "rejected") showFrame(uiText("KOMMANDO NEKAT"), uiText("HAMTAR NYTT LAGE"));
  } else if (topic.startsWith("tambox/v1/gateway/") && topic.endsWith("/status")) {
    if (String(message["status"] | "") != "online") {
      serverEnrollment.clear();
      serverSync.reset();
      invalidate(); showFrame(uiText("SERVER BORTA"), uiText("FORSOKER IGEN"));
    } else if (!retained) {
      // A restarted gateway may have different grants/configuration.
      serverEnrollment.clear(); serverSync.reset(); invalidate();
    }
  }
}

bool resolveServer() {
  // Legacy EEPROM addresses are deliberately ignored. The local administrator
  // assigns this ID; operators never select hosts, ports or stations.
  if (!mdnsStarted) return false;
  const auto servers = TrainMeetNetwork::discoverServers();
  const auto selected = TrainMeetNetwork::selectServer(servers, rememberedServerId.c_str());
  if (selected.index < 0) {
    showFrame(selected.status == TrainMeetNetwork::DiscoveryStatus::Ambiguous ? "FLERA SERVRAR" : "SOKER SERVER",
              selected.status == TrainMeetNetwork::DiscoveryStatus::Ambiguous ? "BE ADMIN HJALPA" : deviceCode);
    return false;
  }
  const auto& server = servers[selected.index];
  gatewayHost = server.host.c_str(); gatewayPort = server.port; discoveredServerId = server.id.c_str();
  return true;
}

void connectServer() {
  disconnectServer();
  if (!resolveServer()) return;
  showFrame(uiText("ANSLUTER SERVER"), deviceCode);
  TMBOX_LOG("Connecting to TrainMeet Server %s:%u\n", gatewayHost.c_str(), gatewayPort);
  if (!mqtt.connect(gatewayHost.c_str(), gatewayPort)) {
    TMBOX_LOG("MQTT connection failed: %d\n", mqtt.connectError()); return;
  }
  TMBOX_LOG("TrainMeet Server connected; assignment is managed by the server administrator.\n");
  connectedBefore = true; connectionFailures = 0; keys.requireRelease();
  terminal.begin(mqtt, deviceId, deviceCode, "NodeMCU ESP8266 16x2", TAMBOX_FIRMWARE_VERSION,
                 bootId + "-" + String(++commandSequence));
  return; // Legacy v1 transport below is retained for source compatibility only.
  if (!mqtt.subscribe("tambox/v1/device/" + deviceId + "/assignment", 1) ||
      !mqtt.subscribe("tambox/v1/client/" + deviceId + "/snapshot/+", 1) ||
      !mqtt.subscribe("tambox/v1/client/" + deviceId + "/ack", 1) ||
      !mqtt.subscribe("tambox/v1/client/" + deviceId + "/state", 1) ||
      !mqtt.subscribe("tambox/v1/device/" + deviceId + "/preferences", 1) ||
      !mqtt.subscribe("tambox/v1/gateway/+/status", 1)) { disconnectServer(); return; }
  JsonDocument presence;
  presence["status"] = "online"; presence["device_code"] = deviceCode;
  publish("tambox/v1/client/" + deviceId + "/presence", presence, true);
  hello();
}

String inputLine1() {
  if (languageMenu.open) return lcdLine(uiText(languageMenu.saving ? "SAVING..." : languageMenu.failed ? "NOT SAVED #=TRY" : "C=NEXT *=BACK"));
  return enteringTrain && !trainEntry.active ? lcdLine(uiText("UPPDATERA SERVER")) : serverLine1;
}

String inputLine2() {
  if (languageMenu.open) {
    String name = String(languageMenu.name().c_str()).substring(0, 10);
    while (name.length() < 11) name += ' ';
    return lcdLine(name + meetingTime);
  }
  if (enteringTrain && !trainEntry.active) return lcdLine(uiText("LOKAL INMATNING"));
  if (!trainEntry.active) return serverLine2;
  String row = uiText("Tag: ") + String(trainEntry.value.c_str());
  if (trainEntry.value.size() < 5) row += '_';
  while (row.length() < 11) row += ' ';
  return lcdLine(row + uiText("*=Avb"));
}

bool sendKey(char key, bool virtualKey) {
  if (terminal.started) {
    if (!webSession.permits(virtualKey, keypadOK && lcdFound, millis())) return false;
    const bool accepted = terminal.press(key);
    if (lcdFound) terminal.draw(lcd);
    return accepted;
  }
  if (languageReady && mqtt.connected() && !lease.waiting &&
      webSession.permits(virtualKey, keypadOK && lcdFound, millis()) &&
      (languageMenu.open || (key == '#' && idleScreen && !trainEntry.active && lease.allowed(millis())))) {
    if (!languageMenu.open) languageMenu.begin(deviceUi["language"] | "sv");
    else {
      const std::string chosen = languageMenu.press(key);
      if (!chosen.empty()) {
        JsonDocument request;
        String id = bootId + "-language-" + String(++commandSequence);
        request["request_id"] = id; request["language"] = chosen.c_str();
        languageMenu.sent(id.c_str(), millis());
        if (!publish("tambox/v1/device/" + deviceId + "/preferences/set", request))
          languageMenu.reply(id.c_str(), false);
      }
    }
    keys.requireRelease(); showFrame(inputLine1(), inputLine2()); return true;
  }
  if (!mqtt.connected() || !lease.allowed(millis()) ||
      !webSession.permits(virtualKey, keypadOK && lcdFound, millis()) ||
      !panelId.length() || !sessionId.length() || allowedKeys.indexOf(key) < 0) {
    TMBOX_DEBUG("Key ignored: connection, lease, input mode or assignment not ready\n"); return false;
  }
  if (key >= '0' && key <= '9') {
    // Web and physical keys share this local buffer. Never send digit traffic,
    // including to older servers which do not advertise atomic submission.
    if (!trainEntry.digit(key)) return false;
    showFrame(inputLine1(), inputLine2());
    return true;
  }
  if (enteringTrain && key == '#' && !trainEntry.canSubmit()) return false;
  TMBOX_DEBUG("Key accepted: source=%s revision=%ld\n", virtualKey ? "web" : "physical", revision);
  commandId = deviceId + "-" + bootId + "-" + String(++commandSequence);
  JsonDocument command;
  command["protocol_version"] = 1;
  command["command_id"] = commandId;
  command["client_id"] = deviceId;
  command["traffic_session_id"] = sessionId;
  command["panel_id"] = panelId;
  command["expected_revision"] = revision;
  command["action"] = "key_press";
  command["key"] = String(key);
  if (enteringTrain && key == '#') command["train_number"] = trainEntry.value.c_str();
  command["device_uptime_ms"] = millis();
  lease.sent(millis());
  if (!publish("tambox/v1/client/" + deviceId + "/command", command)) {
    disconnectServer(); showFrame(uiText("INGET SERVER-SVAR"), uiText("KONTROLLERA LAGE"));
    return false;
  }
  if (key == '*') trainEntry.clear();
  return true;
}

#ifndef TAMBOX_HARDWARE_CHECK
#include "web_test.h"
#endif

void scanHardware() {
  TMBOX_LOG("I2C scan (7-bit addresses):\n");
  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) TMBOX_LOG("I2C device: 0x%02X\n", address);
    yield();
  }
  Wire.beginTransmission(TAMBOX_LCD_ADDRESS);
  lcdFound = Wire.endTransmission() == 0;
  if (lcdFound) { lcd.begin(16, 2, Wire); lcd.setBacklight(255); }
  keypadOK = keypadWrite(0xff);
  if (!lcdFound) TMBOX_LOG("LCD missing: traffic input disabled.\n");
  if (!keypadOK) TMBOX_LOG("Keypad missing: traffic input disabled.\n");
}

void setup() {
  Serial.begin(115200);
  TMBOX_LOG("TrainMeet TMBox %s (%s)\n", TAMBOX_FIRMWARE_VERSION, TAMBOX_MODEL);
  TMBOX_LOG("USB debug: %s\n", TAMBOX_DEBUG_ENABLED ? "on" : "off");
  cacheDeviceUI(false);
  { TrainMeetNetwork::ServerBindingRecord saved{}; EEPROM.begin(4096); EEPROM.get(1024, saved); EEPROM.end();
    rememberedServerId = TrainMeetNetwork::bindingId(saved).c_str(); }
  Wire.begin(TAMBOX_SDA, TAMBOX_SCL); Wire.setClock(100000);
  scanHardware();
  WiFi.mode(WIFI_STA);
  String mac = WiFi.macAddress(); mac.replace(":", ""); mac.toLowerCase();
  deviceId = "esp8266-" + mac;
  // Six hex digits preserve the chip's full 24-bit identifier (no truncated hash).
  char code[11]; snprintf(code, sizeof(code), "TBX-%06X", ESP.getChipId());
  deviceCode = code; apName = "TrainMeet-" + deviceCode.substring(4);
  bootId = String(ESP.random(), HEX) + String(ESP.random(), HEX);
  showFrame("TRAINMEET TMBOX", deviceCode);
#ifdef TAMBOX_HARDWARE_CHECK
  WiFi.mode(WIFI_OFF);
  showFrame("HARDVARUTEST", keypadOK ? "TRYCK ALLA 16" : "KNAPPSATS SAKNAS");
#else
  // Use our bounded USB diagnostics; the library can expose network settings.
  wifiManager.setDebugOutput(false);
  setupWebTest();
  wifiManager.setConfigPortalBlocking(false); wifiManager.setConnectTimeout(15);
  wifiManager.setSaveConfigCallback([]() { saveRequested = true; });
  wifiManager.addParameter(&forgetServer);
  wifiManager.setSaveParamsCallback([]() { forgetServerRequested = String(forgetServer.getValue()) == "1"; });
  mqtt.setId(deviceId); mqtt.setCleanSession(true); mqtt.setKeepAliveInterval(10000);
  mqtt.setConnectionTimeout(3000); mqtt.onMessage(receiveMessage);
  const String will = "{\"status\":\"offline\",\"device_code\":\"" + deviceCode + "\"}";
  mqtt.beginWill("tambox/v1/client/" + deviceId + "/presence", (unsigned short)will.length(), true, 1);
  mqtt.print(will); mqtt.endWill();
  WiFi.setAutoReconnect(true); WiFi.begin();
  wifiLostAt = millis();
  if (!WiFi.SSID().length()) startPortal();
#endif
}

void loop() {
  const uint32_t now = millis();
  if (uint32_t(now - lastLcdCheck) >= 1000) {
    lastLcdCheck = now;
    Wire.beginTransmission(TAMBOX_LCD_ADDRESS);
    const bool available = Wire.endTransmission() == 0;
    if (available != lcdFound) {
      lcdFound = available; lease.clear(); keys.requireRelease(); refreshRequested = true;
      terminal.digits = ""; terminal.dirty = true; terminal.guard = now + 500;
      shownLine1 = ""; shownLine2 = "";
      if (lcdFound) { lcd.begin(16, 2, Wire); lcd.setBacklight(255); }
      else TMBOX_LOG("LCD disconnected: traffic input disabled.\n");
    }
  }
  if (uint32_t(now - lastKeyScan) >= 10) {
    lastKeyScan = now;
    uint16_t mask = 0;
    const bool wasOK = keypadOK;
    keypadOK = keypadScan(mask);
    if (keypadOK != wasOK) {
      lease.clear(); keys.requireRelease(); refreshRequested = true;
      terminal.digits = ""; terminal.dirty = true; terminal.guard = now + 500;
    }
    const KeyEvent event = keys.update(mask, keypadOK, now);
#ifdef TAMBOX_HARDWARE_CHECK
    if (!keypadOK) showFrame(uiText("KNAPPSATS SAKNAS"), uiText("KONTROLLERA I2C"));
    if (event.pressed) showFrame("TANGENT", String(event.pressed));
#else
    if (event.pressed) sendKey(event.pressed, false);
    if (event.reset && !webSession.enabled) {
      // Enter setup without changing permanent identity or erasing good Wi-Fi.
      // Only Wi-Fi is configured here; server discovery is automatic.
      startPortal();
    }
#endif
  }
#ifndef TAMBOX_HARDWARE_CHECK
  if (portalActive) {
    wifiManager.process();
    if (forgetServerRequested) { forgetServerRequested = false; saveServerBinding(""); discoveredServerId = ""; gatewayHost = ""; }
    savePortalSettings();
    // Also handle the portal's explicit exit, even while Wi-Fi is offline.
    if (!wifiManager.getConfigPortalActive()) {
      portalActive = false; nextConnection = millis();
    }
  }
  const bool wifiConnected = WiFi.status() == WL_CONNECTED;
  if (!wifiConnected) {
    if (wifiWasConnected) {
      disconnectServer(); wifiLostAt = now; gatewayHost = "";
      if (mdnsStarted) MDNS.close();
      mdnsStarted = false;
    }
    if (!portalActive) showFrame(uiText("NAT SAKNAS"), uiText("FORSOKER IGEN"));
    if (!portalActive && uint32_t(now - wifiLostAt) >= 30000) startPortal();
  } else {
    if (!wifiWasConnected) TMBOX_LOG("Wi-Fi connected; box IP: %s\n", WiFi.localIP().toString().c_str());
    if (!mdnsStarted) mdnsStarted = MDNS.begin(deviceId.c_str());
    if (mdnsStarted) MDNS.update();
    if (!portalActive) {
      if (!mqtt.connected()) {
        if (connectedBefore) { disconnectServer(); showFrame(uiText("SERVER BORTA"), uiText("FORSOKER IGEN")); }
        if (due(now, nextConnection)) {
          connectServer();
          connectionFailures = min(connectionFailures + 1, 4u);
          nextConnection = millis() + 2000 * connectionFailures;
        }
      } else {
        mqtt.poll();
        // A callback may just have set lastSnapshot later than the loop's `now`.
        const uint32_t current = millis();
        if (terminal.started) {
          if (!terminal.tick()) { disconnectServer(); showFrame("SERVER SAKNAS", "FORSOKER IGEN"); nextConnection = current + 1000; }
          else {
            if (terminal.fresh && String(terminal.frame["station_code"] | "").length() &&
                rememberedServerId != discoveredServerId) saveServerBinding(discoveredServerId);
            if (lcdFound) terminal.draw(lcd);
          }
        } else if (lease.expired(current) || lease.timedOut(current)) {
          TMBOX_DEBUG("Server timeout: snapshot or acknowledgement missing\n");
          disconnectServer(); showFrame(uiText("INGET SERVER-SVAR"), uiText("KONTROLLERA LAGE")); nextConnection = now + 1000;
        } else {
          const auto request = serverSync.next(current, refreshRequested);
          if (request == ServerSync::Assignment) hello();
          else if (request == ServerSync::State) requestState();
        }
        // Do not let absent hardware overwrite Wi-Fi/setup/discovery status
        // every 10 ms, especially when testing a bare NodeMCU over USB.
        // Traffic input remains guarded by both hardware checks in sendKey().
        if (mqtt.connected() && lease.fresh && !webSession.enabled) {
          if (!lcdFound) showFrame(uiText("DISPLAY SAKNAS"), uiText("KONTROLLERA I2C"));
          else if (!keypadOK) showFrame(uiText("KNAPPSATS SAKNAS"), uiText("KONTROLLERA I2C"));
        }
      }
    }
  }
  wifiWasConnected = wifiConnected;
  languageMenu.tick(millis());
  if (languageMenu.open) showFrame(inputLine1(), inputLine2());
  tickWebTest();
#endif
  delay(2); // ESP8266 Wi-Fi + watchdog must get CPU time.
}
