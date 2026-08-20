/*
 * TrainMeet physical TMBox — local-first ESP32 firmware, protocol v2
 *
 * The Raspberry Pi / TrainMeet Server is the traffic authority. This box
 * holds its assigned station's config and live snapshot in RAM (retained
 * MQTT topics tmbox/v2/device/{id}/config and .../snapshot, each replaced
 * wholesale on every publish — no delta logic), browses that cache locally,
 * and only speaks on the wire to send a complete command. See
 * trainmeet-tmbox docs/underlag/tmbox-monsterprompt-v2.md §3.4/§3.4a for the
 * design this implements.
 *
 * This is the connectivity-and-proof-of-protocol slice: identity, discovery,
 * station assignment, config/snapshot caching, a minimal movement browser,
 * and one real write command (uppställt). The full local command-page
 * interaction (§6 rendering rules, train lookup, spårväljare, klarering,
 * linjen-ledig) is a separate, larger pass — see §22 step 3 in the same
 * document. Wi-Fi and MQTT are deliberately self-healing regardless: a lost
 * connection never leaves the firmware in a dead loop.
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

constexpr char FIRMWARE_VERSION[] = "0.3.0";
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
constexpr uint8_t MAX_CACHED_MOVEMENTS = 8;

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

struct MovementCache {
  String id;
  String trainNumber;
  String departure;
  String arrival;
  bool crewReady;
};
MovementCache movements[MAX_CACHED_MOVEMENTS];
uint8_t movementCount = 0;
int8_t selectedMovement = -1;  // -1 = station overview

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
String currentLine1;
String currentLine2;

void showFrame(const String& line1, const String& line2);
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
void sendPositionCommand(const MovementCache& movement);
void onMqttMessage(int messageSize);
void handleAssignment(const String& payload);
void handleConfig(const String& payload);
void handleSnapshot(const String& payload);
void handleAck(const String& payload);
void cycleMovement();
void renderCurrentView();
void keypadEvent(KeypadEvent key);
void buildIdentity();
String codeFromChipId(uint64_t chipId);
String normalizedLine(const String& value);

void setup() {
  Serial.begin(115200);
  Wire.begin(TMBOX_LCD_SDA, TMBOX_LCD_SCL);
  lcd.init();
  lcd.backlight();
  keypad.setHoldTime(5000);
  keypad.addEventListener(keypadEvent);

  preferences.begin("trainmeet", false);
  configuredGatewayHost = preferences.getString("gateway", "");
  buildIdentity();
  bootAt = millis();
  showFrame("TRAINMEET TMBOX", deviceCode);

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

  if (ackMessageUntil != 0 && millis() >= ackMessageUntil) {
    ackMessageUntil = 0;
    renderCurrentView();
  }

  char key = keypad.getKey();
  if (key && mqttClient.connected() && !assignedStationId.isEmpty() && hasConfig && hasSnapshot) {
    if (key == 'C') {
      cycleMovement();
    } else if (key == '*') {
      selectedMovement = -1;
      renderCurrentView();
    } else if (key == 'A' && selectedMovement >= 0 && selectedMovement < movementCount) {
      sendPositionCommand(movements[selectedMovement]);
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
  showFrame("INSTALLERA WIFI", accessPointName);
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
      showFrame("SOKER SERVER", deviceCode);
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

  failedMqttAttempts++;
  nextMqttAttemptAt = now + mqttRetryDelay;
  mqttRetryDelay = min(mqttRetryDelay * 2UL, MQTT_RETRY_MAX_MS);
  showFrame("SERVER BORTA", "FORSOKER IGEN");
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
  showFrame("HITTAT SERVER", "ANSLUTER...");
  if (!mqttClient.connect(gatewayHost.c_str(), gatewayPort)) {
    return false;
  }

  mqttClient.subscribe(assignmentTopic, 1);
  mqttClient.subscribe(configTopic, 1);
  mqttClient.subscribe(snapshotTopic, 1);
  mqttClient.subscribe(ackTopic, 1);
  publishHello();
  publishPresence("online");
  showFrame("SERVER ANSLUTEN", "VANTAR TILLDELN.");
  return true;
}

void disconnectMqtt() {
  if (!mqttClient.connected()) return;
  publishPresence("offline");
  mqttClient.stop();
  assignedStationId = "";
  hasConfig = false;
  hasSnapshot = false;
  movementCount = 0;
  selectedMovement = -1;
}

void publishHello() {
  JsonDocument document;
  document["device_code"] = deviceCode;
  document["model"] = TMBOX_MODEL_NAME;
  document["firmware_version"] = FIRMWARE_VERSION;
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

void sendPositionCommand(const MovementCache& movement) {
  // The single proof-of-protocol write command for this slice: a complete,
  // idempotent train.position.set. The full command-page flow (uppställt ->
  // förare -> begär -> ...) is the next pass (§22 step 3), not this one.
  JsonDocument document;
  document["protocol_version"] = 2;
  document["message_id"] = deviceId + "-" + String((uint32_t)esp_random(), HEX);
  document["device_id"] = deviceId;
  document["station_id"] = assignedStationId;
  document["action"] = "train.position.set";
  JsonObject payloadObject = document["payload"].to<JsonObject>();
  payloadObject["movement_id"] = movement.id;
  String payload;
  serializeJson(document, payload);
  mqttClient.beginMessage(commandTopic.c_str(), payload.length(), false, 1);
  mqttClient.print(payload);
  mqttClient.endMessage();
  showFrame("SKICKAR...", "TAG " + movement.trainNumber);
}

void onMqttMessage(int messageSize) {
  const String topic = mqttClient.messageTopic();
  String payload;
  payload.reserve(messageSize);
  while (mqttClient.available()) {
    payload += (char)mqttClient.read();
  }

  if (topic.endsWith("/assignment")) {
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
    assignedStationId = "";
    hasConfig = false;
    hasSnapshot = false;
    showFrame("KOPPLA BOXEN", deviceCode);
    return;
  }
  assignedStationId = document["station_id"] | "";
  showFrame("STATION KOPPLAD", "HAMTAR DATA...");
}

void handleConfig(const String& payload) {
  JsonDocument document;
  if (deserializeJson(document, payload)) return;
  configPublicationId = document["config_version"] | "";
  stationCode = document["station"]["code"] | "";
  stationName = document["station"]["name"] | "";
  hasConfig = true;
  renderCurrentView();
}

void handleSnapshot(const String& payload) {
  JsonDocument document;
  if (deserializeJson(document, payload)) return;
  movementCount = 0;
  for (JsonObject item : document["movements"].as<JsonArray>()) {
    if (movementCount >= MAX_CACHED_MOVEMENTS) break;
    MovementCache& slot = movements[movementCount];
    slot.id = item["id"] | "";
    slot.trainNumber = item["train_number"] | "";
    slot.departure = item["departure"] | "none";
    slot.arrival = item["arrival"] | "none";
    slot.crewReady = item["crewReady"] | false;
    movementCount++;
  }
  hasSnapshot = true;
  if (selectedMovement >= movementCount) {
    selectedMovement = -1;
  }
  renderCurrentView();
}

void handleAck(const String& payload) {
  JsonDocument document;
  if (deserializeJson(document, payload)) return;
  const String status = document["status"] | "";
  if (status == "accepted") {
    showFrame("KOMMANDO OK", "");
  } else {
    const String reason = document["reason"] | "";
    showFrame("KOMMANDO NEKAT", reason);
  }
  // A fresh snapshot always follows an accepted command and redraws the
  // real view; this is only a brief flash so a rejection reason is visible
  // before that happens.
  ackMessageUntil = millis() + ACK_MESSAGE_MS;
}

void cycleMovement() {
  if (movementCount == 0) return;
  selectedMovement = (selectedMovement + 1) % movementCount;
  renderCurrentView();
}

void renderCurrentView() {
  if (ackMessageUntil != 0) return;  // let the ack flash finish first
  if (!hasConfig || !hasSnapshot) return;
  if (selectedMovement < 0 || selectedMovement >= movementCount) {
    const String label = stationCode.isEmpty() ? "TMBOX" : stationCode;
    const String status = movementCount == 0
      ? String("INGA TAG IDAG")
      : String(movementCount) + " TAG  C=BLADDRA";
    showFrame(label, status);
    return;
  }
  const MovementCache& movement = movements[selectedMovement];
  showFrame("TAG " + movement.trainNumber, movement.departure + "  A=UPP");
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
  showFrame("NATVERK RADERAS", deviceCode);
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
  showFrame("NAT SAKNAS", "FORSOKER IGEN");
}

void showFrame(const String& line1, const String& line2) {
  const String nextLine1 = normalizedLine(line1);
  const String nextLine2 = normalizedLine(line2);
  if (nextLine1 == currentLine1 && nextLine2 == currentLine2) return;
  currentLine1 = nextLine1;
  currentLine2 = nextLine2;
  lcd.setCursor(0, 0);
  lcd.print(currentLine1);
  lcd.setCursor(0, 1);
  lcd.print(currentLine2);
}

String normalizedLine(const String& value) {
  String result = value.substring(0, TMBOX_LCD_COLUMNS);
  while (result.length() < TMBOX_LCD_COLUMNS) result += ' ';
  return result;
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
