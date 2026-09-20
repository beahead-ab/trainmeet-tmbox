/*
 * TrainMeet physical TMBox — local-first ESP32 firmware, protocol v2
 *
 * The Raspberry Pi / TrainMeet Server is the traffic authority. This box
 * holds its assigned station's config and live snapshot in RAM (retained
 * MQTT topics tmbox/v2/device/{id}/config and .../snapshot, each replaced
 * wholesale on every publish — no delta logic), browses that cache locally,
 * and only speaks on the wire to send a complete command. See
 * trainmeet-tmbox docs/tmbox.md §2.4/§2.5 for the design this implements.
 * Wi-Fi and MQTT are deliberately self-healing: a lost connection never
 * leaves the firmware in a dead loop.
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoMqttClient.h>
#include <ESPmDNS.h>
#include <Keypad.h>
#include <LiquidCrystal_I2C.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <Wire.h>

#include "hardware_profile.h"

// The screens and the state machine live in lib/tmbox_core, built and
// asserted in CI without a board. What this file does is talk to the network
// and the hardware; what a screen says and what a key means is decided there.
#include "model.h"
#include "attention.h"
#include "navigation.h"
#include "renderer.h"
#include "meet_scope.h"
#include "language_menu.h"

constexpr char FIRMWARE_VERSION[] = "0.5.0";
constexpr char DISCOVERY_SERVICE[] = "tmbox";
constexpr uint16_t DEFAULT_MQTT_PORT = 1883;
constexpr unsigned long SAVED_WIFI_WINDOW_MS = 15000;
constexpr unsigned long LOST_WIFI_PORTAL_DELAY_MS = 30000;
constexpr unsigned long WIFI_RETRY_MS = 5000;
constexpr unsigned long DISCOVERY_RETRY_MS = 4000;
constexpr unsigned long MQTT_RETRY_MIN_MS = 1000;
constexpr unsigned long MQTT_RETRY_MAX_MS = 8000;
constexpr unsigned long DEVICE_CODE_SCREEN_MS = 3000;
constexpr unsigned long ACK_MESSAGE_MS = 1500;
// A guard against a malformed payload rather than a working limit: a real
// station can have seventy movements in a day, and the cache is a vector now
// rather than the fixed array this number was sized for.
constexpr size_t MAX_CACHED_MOVEMENTS = 96;

const byte ROWS = 4;
const byte COLS = 4;
char keyMap[ROWS][COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'},
};
byte rowPins[ROWS] = {
  TMBOX_ROW_PINS[0], TMBOX_ROW_PINS[1], TMBOX_ROW_PINS[2], TMBOX_ROW_PINS[3]
};
byte colPins[COLS] = {
  TMBOX_COL_PINS[0], TMBOX_COL_PINS[1], TMBOX_COL_PINS[2], TMBOX_COL_PINS[3]
};

LiquidCrystal_I2C lcd(TMBOX_LCD_ADDRESS, TMBOX_LCD_COLUMNS, TMBOX_LCD_ROWS);
Keypad keypad = Keypad(makeKeymap(keyMap), rowPins, colPins, ROWS, COLS);
WiFiManager wifiManager;
WiFiClient networkClient;
MqttClient mqttClient(networkClient);
Preferences preferences;
tmbox::LanguageMenu languageMenu;
std::map<std::string, std::string> deviceMessages;
std::string deviceLanguage = "sv";
bool languageReady = false;
uint32_t languageSequence = 0;

bool loadDeviceUI(JsonVariantConst ui) {
  if (ui["version"] != 1 || !ui["language"].is<const char*>() || !ui["messages"].is<JsonObjectConst>()) return false;
  const JsonArrayConst options = ui["languages"].as<JsonArrayConst>();
  bool found = false;
  if (options.size() == 0 || options.size() > 5) return false;
  for (JsonObjectConst option : options) {
    const String code = option["code"] | "", name = option["name"] | "";
    if (code.length() != 2 || name.length() == 0 || name.length() > 12) return false;
    found = found || code == ui["language"].as<const char*>();
  }
  if (!found) return false;
  deviceLanguage = ui["language"].as<const char*>();
  deviceMessages.clear();
  for (JsonPairConst item : ui["messages"].as<JsonObjectConst>())
    if (item.value().is<const char*>()) deviceMessages[item.key().c_str()] = item.value().as<const char*>();
  languageMenu.options.clear();
  for (JsonObjectConst option : ui["languages"].as<JsonArrayConst>())
    languageMenu.options.push_back({option["code"] | "", option["name"] | ""});
  return true;
}

WiFiManagerParameter* gatewayParameter = nullptr;
String deviceId;
String deviceCode;
String accessPointName;
String configuredGatewayHost;
String gatewayHost;
uint16_t gatewayPort = DEFAULT_MQTT_PORT;

String helloTopic;
String assignmentTopic;
String configTopic;
String snapshotTopic;
String presenceTopic;
String commandTopic;
String ackTopic;

String assignedStationId;
String stationCode;
String stationName;
String configPublicationId;
bool hasConfig = false;
bool hasSnapshot = false;

tmbox::StationConfig stationConfig;
tmbox::Snapshot stationSnapshot;
tmbox::LocalNavigationState navigation;
tmbox::AttentionController attention;
tmbox::MeetScope meetScope;
bool scopeRefreshRequested = false;
unsigned long scopeRefreshAt = 0;

// What this box can physically show, announced in `hello` so the server knows
// what it is formatting for.
const tmbox::Geometry displayGeometry(TMBOX_LCD_ROWS, TMBOX_LCD_COLUMNS, false);

bool portalActive = false;
bool saveParametersRequested = false;
bool resetNetworkRequested = false;
bool mdnsStarted = false;
unsigned long bootAt = 0;
unsigned long wifiAttemptAt = 0;
unsigned long wifiLostAt = 0;
unsigned long nextWifiRetryAt = 0;
unsigned long nextDiscoveryAt = 0;
unsigned long nextMqttAttemptAt = 0;
unsigned long mqttRetryDelay = MQTT_RETRY_MIN_MS;
unsigned int failedMqttAttempts = 0;
unsigned long ackMessageUntil = 0;
// Which action is in flight, so its acknowledgement can be read correctly,
// and the screen to come back to when the flash is over.
String pendingAction;
String pendingMessageId;
tmbox::Screen flashReturnScreen = tmbox::Screen::StationOverview;
// What is on the glass right now, so an unchanged line is not rewritten.
static const uint8_t MAX_DISPLAY_ROWS = 4;
String drawnLines[MAX_DISPLAY_ROWS];

void drawScreen();
void showScreen(tmbox::Screen screen);
void showNetworkState();
void beginSavedWiFiAttempt();
void startSetupPortal();
void stopSetupPortal();
void processWiFi();
void processGateway();
void processSavedParameters();
void resetNetworkConfiguration();
bool discoverGateway();
bool connectMqtt();
void disconnectMqtt();
void publishHello();
void publishPresence(const char* status);
void sendCommand(const tmbox::Command& command);
void onMqttMessage(int messageSize);
void handleAssignment(const String& payload);
void handleConfig(const String& payload);
void handleSnapshot(const String& payload);
void signalAttention(const std::vector<tmbox::AttentionEvent>& events);
const char* attentionName(tmbox::Attention kind);
void handleAck(const String& payload);
void invalidateStationCache();
bool acceptScope(JsonDocument& document, tmbox::ScopePart part, const char* station);
void showStationWhenReady();
void keypadEvent(KeypadEvent key);
void buildIdentity();
String codeFromChipId(uint64_t chipId);

void setup() {
  Serial.begin(115200);
  Wire.begin(TMBOX_LCD_SDA, TMBOX_LCD_SCL);
  lcd.init();
  lcd.backlight();
  keypad.setHoldTime(5000);
  keypad.addEventListener(keypadEvent);

  preferences.begin("trainmeet", false);
  { JsonDocument cached; if (!deserializeJson(cached, preferences.getString("device-ui", ""))) loadDeviceUI(cached.as<JsonVariantConst>()); }
  configuredGatewayHost = preferences.getString("gateway", "");
  buildIdentity();
  bootAt = millis();
  showScreen(tmbox::Screen::Identity);

  static char gatewayBuffer[64];
  configuredGatewayHost.toCharArray(gatewayBuffer, sizeof(gatewayBuffer));
  static WiFiManagerParameter serverField(
    "server",
    "Raspberry Pi-adress (valfritt)",
    gatewayBuffer,
    sizeof(gatewayBuffer) - 1
  );
  gatewayParameter = &serverField;
  wifiManager.addParameter(gatewayParameter);
  wifiManager.setConfigPortalBlocking(false);
  wifiManager.setConnectTimeout(15);
  wifiManager.setSaveParamsCallback([]() { saveParametersRequested = true; });
  wifiManager.setAPCallback([](WiFiManager*) { portalActive = true; });

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  mqttClient.onMessage(onMqttMessage);
  mqttClient.setId(deviceId);
  mqttClient.setKeepAliveInterval(10 * 1000UL);
  mqttClient.setConnectionTimeout(4 * 1000UL);
  mqttClient.setCleanSession(true);
  beginSavedWiFiAttempt();
}

void loop() {
  processSavedParameters();
  if (resetNetworkRequested) {
    resetNetworkConfiguration();
  }

  processWiFi();
  processGateway();
  if (scopeRefreshRequested && mqttClient.connected() && millis() >= scopeRefreshAt) {
    scopeRefreshRequested = false;
    scopeRefreshAt = millis() + 2000;
    publishPresence("online");  // Ask for a complete fresh authoritative triplet.
  }

  if (ackMessageUntil != 0 && millis() >= ackMessageUntil) {
    ackMessageUntil = 0;
    navigation.show(flashReturnScreen, millis());
    drawScreen();
  }

  char key = keypad.getKey();
  languageMenu.tick(millis());
  if (key && ackMessageUntil == 0 && mqttClient.connected() && languageReady
      && (languageMenu.open || (key == 'D' &&
          (navigation.view().screen == tmbox::Screen::StationOverview ||
           navigation.view().screen == tmbox::Screen::AwaitingAssignment)))) {
    if (!languageMenu.open) languageMenu.begin(deviceLanguage);
    else {
      const std::string chosen = languageMenu.press(key);
      if (!chosen.empty()) {
        JsonDocument request;
        String id = deviceId + "-language-" + String(millis()) + "-" + String(++languageSequence);
        request["request_id"] = id; request["language"] = chosen.c_str();
        languageMenu.sent(id.c_str(), millis());
        String body; serializeJson(request, body);
        mqttClient.beginMessage(("tmbox/v2/device/" + deviceId + "/preferences/set").c_str(), body.length(), false, 1);
        mqttClient.print(body); mqttClient.endMessage();
      }
    }
    key = 0; drawScreen();
  }
  if (languageMenu.open) { key = 0; drawScreen(); }
  // No write is accepted until a fresh, authoritative config and snapshot have
  // arrived: acting on a stale cache is how a box sends a decision about a
  // train that is no longer there.
  if (key && ackMessageUntil == 0 && mqttClient.connected()
      && !assignedStationId.isEmpty() && hasConfig && hasSnapshot && meetScope.ready()) {
    const tmbox::KeyResult result =
        navigation.press(key, millis(), stationConfig, stationSnapshot);
    if (result.outcome == tmbox::Outcome::Redraw) {
      drawScreen();
    } else if (result.outcome == tmbox::Outcome::Send) {
      sendCommand(result.command);
    }
  }

  if (mqttClient.connected()) {
    mqttClient.poll();
  }
  delay(10);
}

void beginSavedWiFiAttempt() {
  stopSetupPortal();
  WiFi.mode(WIFI_STA);
  WiFi.begin();
  wifiAttemptAt = millis();
  wifiLostAt = 0;
  nextWifiRetryAt = 0;
}

void processWiFi() {
  const unsigned long now = millis();
  if (portalActive) {
    wifiManager.process();
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiLostAt = 0;
    if (portalActive) {
      stopSetupPortal();
    }
    return;
  }

  disconnectMqtt();
  if (wifiLostAt == 0) {
    wifiLostAt = now;
    showNetworkState();
  }

  if (!portalActive && now - wifiAttemptAt >= SAVED_WIFI_WINDOW_MS &&
      now - wifiLostAt >= LOST_WIFI_PORTAL_DELAY_MS) {
    startSetupPortal();
    return;
  }

  if (!portalActive && now >= nextWifiRetryAt) {
    WiFi.reconnect();
    nextWifiRetryAt = now + WIFI_RETRY_MS;
  }
}

void startSetupPortal() {
  if (portalActive) return;
  disconnectMqtt();
  WiFi.mode(WIFI_AP_STA);
  wifiManager.startConfigPortal(accessPointName.c_str());
  portalActive = true;
  showScreen(tmbox::Screen::SetupPortal);
}

void stopSetupPortal() {
  if (!portalActive) return;
  wifiManager.stopConfigPortal();
  portalActive = false;
}

void processGateway() {
  const unsigned long now = millis();
  if (WiFi.status() != WL_CONNECTED) return;

  if (mqttClient.connected()) {
    return;
  }

  if (gatewayHost.isEmpty()) {
    if (now < nextDiscoveryAt) return;
    if (!discoverGateway()) {
      showScreen(tmbox::Screen::SeekingServer);
      nextDiscoveryAt = now + DISCOVERY_RETRY_MS;
      return;
    }
  }

  if (now < nextMqttAttemptAt) return;
  if (connectMqtt()) {
    mqttRetryDelay = MQTT_RETRY_MIN_MS;
    failedMqttAttempts = 0;
    return;
  }

  signalAttention(attention.observe_link(false));
  failedMqttAttempts++;
  nextMqttAttemptAt = now + mqttRetryDelay;
  mqttRetryDelay = min(mqttRetryDelay * 2UL, MQTT_RETRY_MAX_MS);
  showScreen(tmbox::Screen::ServerGone);
  if (failedMqttAttempts >= 3 && configuredGatewayHost.isEmpty()) {
    gatewayHost = "";
    failedMqttAttempts = 0;
    nextDiscoveryAt = now + DISCOVERY_RETRY_MS;
  }
}

bool discoverGateway() {
  if (!configuredGatewayHost.isEmpty()) {
    gatewayHost = configuredGatewayHost;
    gatewayPort = DEFAULT_MQTT_PORT;
    return true;
  }

  if (!mdnsStarted) {
    mdnsStarted = MDNS.begin(deviceId.c_str());
  }
  if (!mdnsStarted) return false;

  const int count = MDNS.queryService(DISCOVERY_SERVICE, "tcp");
  if (count < 1) return false;
  IPAddress resolvedIp = MDNS.IP(0);
  gatewayHost = resolvedIp.toString();
  gatewayPort = MDNS.port(0);
  return gatewayHost != "0.0.0.0" && gatewayPort > 0;
}

bool connectMqtt() {
  showScreen(tmbox::Screen::SeekingServer);
  if (!mqttClient.connect(gatewayHost.c_str(), gatewayPort)) {
    return false;
  }

  // A dropped connection may already report disconnected before the Wi-Fi
  // handler sees it. Never carry that connection's selections into a new one.
  meetScope.reset();
  invalidateStationCache();

  mqttClient.subscribe(assignmentTopic, 1);
  mqttClient.subscribe(configTopic, 1);
  mqttClient.subscribe(snapshotTopic, 1);
  mqttClient.subscribe(ackTopic, 1);
  mqttClient.subscribe("tmbox/v2/device/" + deviceId + "/preferences", 1);
  publishHello();
  publishPresence("online");
  signalAttention(attention.observe_link(true));
  showScreen(tmbox::Screen::AwaitingAssignment);
  return true;
}

void disconnectMqtt() {
  languageReady = false;
  languageMenu.open = languageMenu.saving = false;
  if (!mqttClient.connected()) return;
  publishPresence("offline");
  mqttClient.stop();
  meetScope.reset();
  invalidateStationCache();
}

void invalidateStationCache() {
  assignedStationId = "";
  hasConfig = false;
  hasSnapshot = false;
  stationConfig = tmbox::StationConfig();
  stationSnapshot = tmbox::Snapshot();
  navigation = tmbox::LocalNavigationState();
  attention.forget();
  pendingAction = "";
  pendingMessageId = "";
  ackMessageUntil = 0;
  flashReturnScreen = tmbox::Screen::StationOverview;
}

bool acceptScope(JsonDocument& document, tmbox::ScopePart part, const char* station) {
  tmbox::WireScope incoming;
  incoming.present = !document["meet_generation"].isNull() || !document["publication_id"].isNull();
  if (incoming.present) {
    incoming.valid = document["meet_generation"].is<uint64_t>()
        && document["publication_id"].is<const char*>();
    incoming.generation = document["meet_generation"] | uint64_t(0);
    incoming.publication = document["publication_id"] | "";
  }
  const bool accepted = meetScope.accept(part, incoming, station ? station : "");
  if (meetScope.reset_seen()) {
    invalidateStationCache();
    showScreen(tmbox::Screen::LoadingStation);
  }
  if (!accepted) scopeRefreshRequested = true;
  return accepted;
}

void showStationWhenReady() {
  if (hasConfig && hasSnapshot && meetScope.ready()
      && (navigation.view().screen == tmbox::Screen::LoadingStation
          || navigation.view().screen == tmbox::Screen::AwaitingAssignment)) {
    navigation.show(tmbox::Screen::StationOverview, millis());
  }
  drawScreen();
}

void publishHello() {
  JsonDocument document;
  document["device_code"] = deviceCode;
  document["model"] = TMBOX_MODEL_NAME;
  document["firmware_version"] = FIRMWARE_VERSION;
  // §5: the box says what it can render so the server formats for it rather
  // than assuming the smallest display we support.
  JsonObject display = document["display"].to<JsonObject>();
  display["rows"] = displayGeometry.rows;
  display["cols"] = displayGeometry.cols;
  display["charset"] = displayGeometry.supports_swedish ? "cgram" : "ascii";
  String payload;
  serializeJson(document, payload);
  mqttClient.beginMessage(helloTopic.c_str(), payload.length(), false, 1);
  mqttClient.print(payload);
  mqttClient.endMessage();
}

void publishPresence(const char* status) {
  JsonDocument document;
  document["status"] = status;
  document["device_code"] = deviceCode;
  document["uptime_ms"] = millis();
  String payload;
  serializeJson(document, payload);
  mqttClient.beginMessage(presenceTopic.c_str(), payload.length(), false, 1);
  mqttClient.print(payload);
  mqttClient.endMessage();
}

void sendCommand(const tmbox::Command& command) {
  if (!mqttClient.connected() || !meetScope.ready() || !hasConfig || !hasSnapshot) {
    showScreen(tmbox::Screen::LoadingStation);
    return;
  }
  // One id per command, reused on replay, so a reconnect cannot turn one
  // decision into two.
  JsonDocument document;
  document["protocol_version"] = 2;
  pendingMessageId = deviceId + "-" + String((uint32_t)esp_random(), HEX);
  document["message_id"] = pendingMessageId;
  document["device_id"] = deviceId;
  document["station_id"] = assignedStationId;
  document["action"] = command.action.c_str();
  if (meetScope.value().present) {
    document["meet_generation"] = meetScope.value().generation;
    document["publication_id"] = meetScope.value().publication.c_str();
  }
  JsonObject payloadObject = document["payload"].to<JsonObject>();
  // Every id the state machine put on the command travels. A field it sets
  // and nobody packs is a command that will be refused on arrival.
  if (!command.movement_id.empty()) payloadObject["movement_id"] = command.movement_id.c_str();
  if (!command.track_id.empty()) payloadObject["track_id"] = command.track_id.c_str();
  if (!command.connection_id.empty()) payloadObject["connection_id"] = command.connection_id.c_str();
  if (!command.clearance_id.empty()) payloadObject["clearance_id"] = command.clearance_id.c_str();
  if (!command.message_id.empty()) payloadObject["message_id"] = command.message_id.c_str();
  if (!command.train_number.empty()) payloadObject["train_number"] = command.train_number.c_str();
  if (command.has_approved) payloadObject["approved"] = command.approved;

  String payload;
  serializeJson(document, payload);
  mqttClient.beginMessage(commandTopic.c_str(), payload.length(), false, 1);
  mqttClient.print(payload);
  mqttClient.endMessage();

  pendingAction = command.action.c_str();
  flashReturnScreen = navigation.view().screen;
  showScreen(tmbox::Screen::Sending);
}

void onMqttMessage(int messageSize) {
  const String topic = mqttClient.messageTopic();
  String payload;
  payload.reserve(messageSize);
  while (mqttClient.available()) {
    payload += (char)mqttClient.read();
  }

  if (topic.endsWith("/preferences")) {
    if (mqttClient.messageRetain()) return;
    JsonDocument document;
    if (deserializeJson(document, payload)) return;
    if (loadDeviceUI(document["ui"])) {
      languageReady = true;
      String cached; serializeJson(document["ui"], cached);
      if (preferences.getString("device-ui", "") != cached) preferences.putString("device-ui", cached);
    }
    languageMenu.reply(document["request_id"] | "", String(document["status"] | "") == "accepted");
    drawScreen();
  } else if (topic.endsWith("/assignment")) {
    handleAssignment(payload);
  } else if (topic.endsWith("/config")) {
    handleConfig(payload);
  } else if (topic.endsWith("/snapshot")) {
    handleSnapshot(payload);
  } else if (topic.endsWith("/ack")) {
    handleAck(payload);
  }
}

void handleAssignment(const String& payload) {
  JsonDocument document;
  if (deserializeJson(document, payload)) return;
  const String status = document["status"] | "";
  if (status != "assigned") {
    acceptScope(document, tmbox::ScopePart::Assignment, "");
    meetScope.invalidate();
    invalidateStationCache();
    scopeRefreshRequested = false;  // Waiting for the administrator, not a retry loop.
    showScreen(tmbox::Screen::AwaitingAssignment);
    return;
  }
  if (!acceptScope(document, tmbox::ScopePart::Assignment, document["station_id"] | "")) return;
  assignedStationId = document["station_id"] | "";
  if (!meetScope.ready()) showScreen(tmbox::Screen::LoadingStation);
  showStationWhenReady();
}

void handleConfig(const String& payload) {
  JsonDocument document;
  if (deserializeJson(document, payload)) return;
  if (!acceptScope(document, tmbox::ScopePart::Config, document["station"]["id"] | "")) return;
  configPublicationId = document["config_version"] | "";
  stationConfig = tmbox::StationConfig();
  stationConfig.station_id = document["station"]["id"] | "";
  stationConfig.code = document["station"]["code"] | "";
  stationConfig.name = document["station"]["name"] | "";
  // The pickers are built from these; without them a track change or a
  // clearance request has nothing to offer and nothing to name.
  for (JsonObject item : document["tracks"].as<JsonArray>()) {
    tmbox::Track track;
    track.id = item["id"] | "";
    track.display_label = item["display_label"] | "";
    stationConfig.tracks.push_back(track);
  }
  for (JsonObject item : document["connections"].as<JsonArray>()) {
    tmbox::Connection connection;
    connection.connection_id = item["connection_id"] | "";
    connection.other_station_code = item["other_station_code"] | "";
    connection.track_type = item["track_type"] | "";
    connection.display_side = item["display_side"] | "";
    stationConfig.connections.push_back(connection);
  }
  hasConfig = true;
  showStationWhenReady();
}

void handleSnapshot(const String& payload) {
  JsonDocument document;
  if (deserializeJson(document, payload)) return;
  if (!acceptScope(document, tmbox::ScopePart::Snapshot, document["station_id"] | "")) return;
  stationSnapshot = tmbox::Snapshot();
  stationSnapshot.station_id = document["station_id"] | "";
  stationSnapshot.clock.time = document["clock"]["time"] | "";
  stationSnapshot.clock.running = document["clock"]["running"] | false;
  for (JsonObject item : document["movements"].as<JsonArray>()) {
    if (stationSnapshot.movements.size() >= MAX_CACHED_MOVEMENTS) break;
    tmbox::Movement movement;
    movement.id = item["id"] | "";
    movement.train_number = item["train_number"] | "";
    movement.arrival_time = item["arrival_time"] | "";
    movement.departure_time = item["departure_time"] | "";
    movement.departure = item["departure"] | "none";
    movement.arrival = item["arrival"] | "none";
    movement.assigned_track_id = item["assignedTrackId"] | "";
    movement.crew_ready = item["crewReady"] | false;
    // What the box may offer is the server's answer, never the box's guess.
    for (JsonVariant action : item["allowed_actions"].as<JsonArray>()) {
      // A null in the array would hand std::string a null pointer, which is
      // undefined behaviour rather than an empty action.
      const char* name = action.as<const char*>();
      if (name != nullptr && name[0] != '\0') movement.allowed_actions.push_back(name);
    }
    stationSnapshot.movements.push_back(movement);
  }
  for (JsonObject item : document["active_clearances"].as<JsonArray>()) {
    tmbox::Clearance clearance;
    clearance.clearance_id = item["clearance_id"] | "";
    clearance.movement_id = item["movement_id"] | "";
    clearance.connection_id = item["connection_id"] | "";
    clearance.status = item["status"] | "";
    clearance.from_station_id = item["from_station_id"] | "";
    clearance.to_station_id = item["to_station_id"] | "";
    stationSnapshot.clearances.push_back(clearance);
  }
  for (JsonObject item : document["line_messages"].as<JsonArray>()) {
    tmbox::LineMessage message;
    message.message_id = item["message_id"] | "";
    message.connection_id = item["connection_id"] | "";
    message.status = item["status"] | "";
    message.from_station_id = item["from_station_id"] | "";
    stationSnapshot.line_messages.push_back(message);
  }
  hasSnapshot = true;
  signalAttention(attention.observe(stationSnapshot));
  // A snapshot replaces the cache whole, so a selection that no longer exists
  // must not survive it.
  navigation.reconcile(stationConfig, stationSnapshot, millis());
  showStationWhenReady();
}

void handleAck(const String& payload) {
  JsonDocument document;
  if (deserializeJson(document, payload)) return;
  if (pendingMessageId.isEmpty() || pendingMessageId != (document["message_id"] | "") || !meetScope.ready()) return;
  pendingMessageId = "";
  if (String(document["reason"] | "") == "stale_meet_context") {
    meetScope.invalidate();
    invalidateStationCache();
    scopeRefreshRequested = true;
    showScreen(tmbox::Screen::LoadingStation);
    return;
  }
  const String status = document["status"] | "";
  const bool refused = status != "accepted" && status != "duplicate";

  // A lookup answers rather than changes anything, so it lands on a screen
  // instead of flashing past the operator.
  if (!refused && pendingAction == "train.lookup") {
    std::vector<tmbox::LookupMatch> matches;
    for (JsonObject item : document["result"]["matches"].as<JsonArray>()) {
      tmbox::LookupMatch match;
      match.movement_id = item["movement_id"] | "";
      match.train_number = item["train_number"] | "";
      match.arrival_time = item["arrival_time"] | "";
      match.departure_time = item["departure_time"] | "";
      match.track_id = item["track_id"] | "";
      matches.push_back(match);
    }
    pendingAction = "";
    navigation.apply_lookup(stationSnapshot, matches, millis());
    drawScreen();
    return;
  }
  pendingAction = "";

  if (refused) {
    navigation.view().reason = String(document["reason"] | "").c_str();
    showScreen(tmbox::Screen::CommandRejected);
  } else {
    showScreen(tmbox::Screen::CommandAccepted);
  }
  // A fresh snapshot always follows an accepted command and redraws the
  // real view; this is only a brief flash so a rejection reason is visible
  // before that happens.
  ackMessageUntil = millis() + ACK_MESSAGE_MS;
}



void processSavedParameters() {
  if (!saveParametersRequested || gatewayParameter == nullptr) return;
  saveParametersRequested = false;
  configuredGatewayHost = String(gatewayParameter->getValue());
  configuredGatewayHost.trim();
  preferences.putString("gateway", configuredGatewayHost);
  gatewayHost = configuredGatewayHost;
  nextMqttAttemptAt = 0;
}

void keypadEvent(KeypadEvent key) {
  if (key == '*' && keypad.getState() == HOLD) {
    resetNetworkRequested = true;
  }
}

void resetNetworkConfiguration() {
  resetNetworkRequested = false;
  showScreen(tmbox::Screen::ResettingNetwork);
  disconnectMqtt();
  preferences.remove("gateway");
  configuredGatewayHost = "";
  gatewayHost = "";
  wifiManager.resetSettings();
  delay(800);
  ESP.restart();
}

void showNetworkState() {
  if (millis() - bootAt < DEVICE_CODE_SCREEN_MS) return;
  showScreen(tmbox::Screen::NoNetwork);
}

void drawScreen() {
  stationConfig.language = deviceLanguage;
  if (stationConfig.messages != deviceMessages) stationConfig.messages = deviceMessages;
  tmbox::Frame frame =
      tmbox::render(displayGeometry, navigation.view(), stationConfig, stationSnapshot);
  if (languageMenu.open) {
    const std::string key = languageMenu.saving ? "SAVING..." : languageMenu.failed ? "NOT SAVED #=TRY" : "C=NEXT *=BACK";
    const auto found = deviceMessages.find(key);
    const std::string name = languageMenu.name().substr(0, displayGeometry.cols - 6);
    const std::string clock = stationSnapshot.clock.time.empty() ? "--:--" : stationSnapshot.clock.time.substr(0, 5);
    const std::string choice = name + std::string(displayGeometry.cols - name.size() - clock.size(), ' ') + clock;
    frame = tmbox::frame_of(displayGeometry, {found == deviceMessages.end() ? key : found->second, choice, "LANGUAGE", stationSnapshot.clock.time});
  }
  for (uint8_t row = 0; row < displayGeometry.rows; ++row) {
    // Only write a line that actually changed. An I2C display is slow enough
    // that redrawing an unchanged frame is visible as a flicker.
    if (row < MAX_DISPLAY_ROWS && drawnLines[row] == frame[row].c_str()) continue;
    if (row < MAX_DISPLAY_ROWS) drawnLines[row] = frame[row].c_str();
    lcd.setCursor(0, row);
    lcd.print(frame[row].c_str());
  }
}

void showScreen(tmbox::Screen screen) {
  navigation.show(screen, millis());
  drawScreen();
}


void buildIdentity() {
  const uint64_t chipId = ESP.getEfuseMac();
  char idBuffer[25];
  snprintf(
    idBuffer,
    sizeof(idBuffer),
    "esp32-%04x%08x",
    (uint16_t)(chipId >> 32),
    (uint32_t)chipId
  );
  deviceId = String(idBuffer);
  deviceCode = codeFromChipId(chipId);
  // "TMBOX-" is 6 characters; the AP name carries just the 6-character code.
  accessPointName = "TrainMeet-" + deviceCode.substring(6);

  helloTopic = "tmbox/v2/device/" + deviceId + "/hello";
  assignmentTopic = "tmbox/v2/device/" + deviceId + "/assignment";
  configTopic = "tmbox/v2/device/" + deviceId + "/config";
  snapshotTopic = "tmbox/v2/device/" + deviceId + "/snapshot";
  presenceTopic = "tmbox/v2/device/" + deviceId + "/presence";
  commandTopic = "tmbox/v2/device/" + deviceId + "/command";
  ackTopic = "tmbox/v2/device/" + deviceId + "/ack";
}

String codeFromChipId(uint64_t chipId) {
  // TMBOX-XXXXXX: six symbols from a 32-character alphabet (5 bits each,
  // 30 bits total) — enough spread that two boxes on the same meeting
  // network collide only by extraordinary coincidence.
  constexpr char alphabet[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
  uint32_t value = ((uint32_t)chipId ^ (uint32_t)(chipId >> 24)) & 0x3FFFFFFF;
  char code[13] = "TMBOX-000000";
  for (int index = 11; index >= 6; --index) {
    code[index] = alphabet[value & 31];
    value >>= 5;
  }
  return String(code);
}

// The attention sink. The controller decides *whether*; this decides *how*,
// and with no buzzer defined the how is "log it and carry on" — the spec's
// graceful degradation. The serial line is not decoration: it is how a bench
// test sees that the box reached the right decision without any hardware to
// hear it with.
void signalAttention(const std::vector<tmbox::AttentionEvent>& events) {
  const tmbox::Attention loudest = tmbox::AttentionController::loudest(events);
  if (loudest == tmbox::Attention::None) return;

  Serial.print("[uppmarksamhet] ");
  Serial.println(attentionName(loudest));

  if (!TMBOX_HAS_BUZZER) return;

  // One buzzer, one sound. Losing the server gets the long note because it is
  // the one event that makes everything else on the display untrustworthy.
  unsigned int frequency = 2000;
  unsigned int duration = 120;
  switch (loudest) {
    case tmbox::Attention::ConnectionLost:     frequency = 700;  duration = 600; break;
    case tmbox::Attention::ConnectionRestored: frequency = 1400; duration = 120; break;
    case tmbox::Attention::IncomingRequest:    frequency = 2200; duration = 250; break;
    case tmbox::Attention::RequestDenied:      frequency = 900;  duration = 250; break;
    case tmbox::Attention::RequestApproved:    frequency = 2600; duration = 150; break;
    case tmbox::Attention::IncomingTrain:      frequency = 1800; duration = 150; break;
    case tmbox::Attention::None:               return;
  }
  tone(TMBOX_BUZZER_PIN, frequency, duration);
}

const char* attentionName(tmbox::Attention kind) {
  switch (kind) {
    case tmbox::Attention::None:               return "inget";
    case tmbox::Attention::ConnectionLost:     return "servern borta";
    case tmbox::Attention::IncomingRequest:    return "begaran hit";
    case tmbox::Attention::RequestDenied:      return "var begaran nekad";
    case tmbox::Attention::RequestApproved:    return "var begaran godkand";
    case tmbox::Attention::IncomingTrain:      return "linjen ledig mot oss";
    case tmbox::Attention::ConnectionRestored: return "servern tillbaka";
  }
  return "?";
}
