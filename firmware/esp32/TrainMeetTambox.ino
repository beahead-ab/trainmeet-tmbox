/*
 * TrainMeet physical Tambox — local-first ESP32 firmware
 *
 * The Raspberry Pi is the traffic authority. This box only sends key presses
 * and renders complete 16x2 snapshots. Wi-Fi and MQTT are deliberately
 * self-healing: a lost connection never leaves the firmware in a dead loop.
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

constexpr char FIRMWARE_VERSION[] = "0.2.0";
constexpr char DISCOVERY_SERVICE[] = "tambox";
constexpr uint16_t DEFAULT_MQTT_PORT = 1883;
constexpr unsigned long SAVED_WIFI_WINDOW_MS = 15000;
constexpr unsigned long LOST_WIFI_PORTAL_DELAY_MS = 30000;
constexpr unsigned long WIFI_RETRY_MS = 5000;
constexpr unsigned long DISCOVERY_RETRY_MS = 4000;
constexpr unsigned long MQTT_RETRY_MIN_MS = 1000;
constexpr unsigned long MQTT_RETRY_MAX_MS = 8000;
constexpr unsigned long DEVICE_CODE_SCREEN_MS = 3000;

const byte ROWS = 4;
const byte COLS = 4;
char keyMap[ROWS][COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'},
};
byte rowPins[ROWS] = {
  TAMBOX_ROW_PINS[0], TAMBOX_ROW_PINS[1], TAMBOX_ROW_PINS[2], TAMBOX_ROW_PINS[3]
};
byte colPins[COLS] = {
  TAMBOX_COL_PINS[0], TAMBOX_COL_PINS[1], TAMBOX_COL_PINS[2], TAMBOX_COL_PINS[3]
};

LiquidCrystal_I2C lcd(TAMBOX_LCD_ADDRESS, TAMBOX_LCD_COLUMNS, TAMBOX_LCD_ROWS);
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
String assignedPanelId;
String trafficSessionId;
String allowedKeys;
String authoritativeLine1;
String authoritativeLine2;
long serverRevision = -1;

bool portalActive = false;
bool saveParametersRequested = false;
bool resetNetworkRequested = false;
bool mdnsStarted = false;
bool hasAuthoritativeFrame = false;
unsigned long bootAt = 0;
unsigned long wifiAttemptAt = 0;
unsigned long wifiLostAt = 0;
unsigned long nextWifiRetryAt = 0;
unsigned long nextDiscoveryAt = 0;
unsigned long nextMqttAttemptAt = 0;
unsigned long mqttRetryDelay = MQTT_RETRY_MIN_MS;
unsigned int failedMqttAttempts = 0;
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
void publishKey(char key);
void onMqttMessage(int messageSize);
void handleAssignment(const String& payload);
void handleSnapshot(const String& payload);
void handleAcknowledgement(const String& payload);
void keypadEvent(KeypadEvent key);
void buildIdentity();
String codeFromChipId(uint64_t chipId);
String normalizedLine(const String& value);

void setup() {
  Serial.begin(115200);
  Wire.begin(TAMBOX_LCD_SDA, TAMBOX_LCD_SCL);
  lcd.init();
  lcd.backlight();
  keypad.setHoldTime(5000);
  keypad.addEventListener(keypadEvent);

  preferences.begin("trainmeet", false);
  configuredGatewayHost = preferences.getString("gateway", "");
  buildIdentity();
  bootAt = millis();
  showFrame("TRAINMEET TAMBOX", deviceCode);

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

  char key = keypad.getKey();
  if (key && mqttClient.connected() && !assignedPanelId.isEmpty() && allowedKeys.indexOf(key) >= 0) {
    publishKey(key);
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
  gatewayHost = MDNS.hostname(0);
  gatewayPort = MDNS.port(0);
  return !gatewayHost.isEmpty() && gatewayPort > 0;
}

bool connectMqtt() {
  showFrame("HITTAT SERVER", "ANSLUTER...");
  if (!mqttClient.connect(gatewayHost.c_str(), gatewayPort)) {
    return false;
  }

  const String assignmentTopic = "tambox/v1/device/" + deviceId + "/assignment";
  const String snapshotTopic = "tambox/v1/client/" + deviceId + "/snapshot/+";
  const String acknowledgementTopic = "tambox/v1/client/" + deviceId + "/ack";
  mqttClient.subscribe(assignmentTopic, 1);
  mqttClient.subscribe(snapshotTopic, 1);
  mqttClient.subscribe(acknowledgementTopic, 1);
  publishPresence("online");
  publishHello();
  showFrame("SERVER ANSLUTEN", "HAMTAR PANEL...");
  return true;
}

void disconnectMqtt() {
  if (!mqttClient.connected()) return;
  publishPresence("offline");
  mqttClient.stop();
  assignedPanelId = "";
  allowedKeys = "";
  hasAuthoritativeFrame = false;
}

void publishHello() {
  JsonDocument document;
  document["protocol_version"] = 1;
  document["device_code"] = deviceCode;
  document["model"] = TAMBOX_MODEL_NAME;
  document["firmware_version"] = FIRMWARE_VERSION;
  document["wifi_rssi"] = WiFi.RSSI();
  String payload;
  serializeJson(document, payload);
  const String topic = "tambox/v1/device/" + deviceId + "/hello";
  mqttClient.beginMessage(topic.c_str(), payload.length(), false, 1);
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
  const String topic = "tambox/v1/client/" + deviceId + "/presence";
  mqttClient.beginMessage(topic.c_str(), payload.length(), true, 1);
  mqttClient.print(payload);
  mqttClient.endMessage();
}

void publishKey(char key) {
  JsonDocument document;
  document["protocol_version"] = 1;
  document["command_id"] = deviceId + "-" + String((uint32_t)esp_random(), HEX);
  document["client_id"] = deviceId;
  document["traffic_session_id"] = trafficSessionId;
  document["panel_id"] = assignedPanelId;
  document["expected_revision"] = serverRevision;
  document["action"] = "key_press";
  document["key"] = String(key);
  document["device_uptime_ms"] = millis();
  String payload;
  serializeJson(document, payload);
  const String topic = "tambox/v1/client/" + deviceId + "/command";
  mqttClient.beginMessage(topic.c_str(), payload.length(), false, 1);
  mqttClient.print(payload);
  mqttClient.endMessage();
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
  } else if (topic.indexOf("/snapshot/") >= 0) {
    handleSnapshot(payload);
  } else if (topic.endsWith("/ack")) {
    handleAcknowledgement(payload);
  }
}

void handleAssignment(const String& payload) {
  JsonDocument document;
  if (deserializeJson(document, payload)) return;
  const String status = document["status"] | "";
  if (status != "assigned") {
    assignedPanelId = "";
    allowedKeys = "";
    hasAuthoritativeFrame = false;
    showFrame("KOPPLA BOXEN", deviceCode);
    return;
  }

  JsonArray panelIds = document["assigned_panel_ids"].as<JsonArray>();
  if (panelIds.isNull() || panelIds.size() == 0) return;
  assignedPanelId = panelIds[0].as<String>();
  showFrame("BOX KOPPLAD", "HAMTAR PANEL...");
  publishPresence("online");
}

void handleSnapshot(const String& payload) {
  JsonDocument document;
  if (deserializeJson(document, payload)) return;
  const String panelId = document["panel_id"] | "";
  if (panelId.isEmpty() || panelId != assignedPanelId) return;

  trafficSessionId = document["traffic_session_id"] | "";
  serverRevision = document["revision"] | -1;
  authoritativeLine1 = document["display"]["line1"] | "";
  authoritativeLine2 = document["display"]["line2"] | "";
  allowedKeys = "";
  for (JsonVariant key : document["interaction"]["allowed_keys"].as<JsonArray>()) {
    allowedKeys += key.as<String>();
  }
  hasAuthoritativeFrame = true;
  showFrame(authoritativeLine1, authoritativeLine2);
}

void handleAcknowledgement(const String& payload) {
  JsonDocument document;
  if (deserializeJson(document, payload)) return;
  const String status = document["status"] | "";
  if (status != "rejected") return;
  const String reason = document["reason"] | "";
  if (reason == "stale_revision") {
    showFrame("LAGET ANDRADES", "FORSOK IGEN");
  } else if (reason == "connection_busy") {
    showFrame("STRACKA UPPTAGEN", "");
  } else {
    showFrame("KOMMANDO NEKAT", "FORSOK IGEN");
  }
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
  String result = value.substring(0, TAMBOX_LCD_COLUMNS);
  while (result.length() < TAMBOX_LCD_COLUMNS) result += ' ';
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
  accessPointName = "TrainMeet-" + deviceCode.substring(4);
}

String codeFromChipId(uint64_t chipId) {
  constexpr char alphabet[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
  uint32_t value = ((uint32_t)chipId ^ (uint32_t)(chipId >> 24)) & 0xFFFFF;
  char code[9] = "TBX-0000";
  for (int index = 7; index >= 4; --index) {
    code[index] = alphabet[value & 31];
    value >>= 5;
  }
  return String(code);
}
