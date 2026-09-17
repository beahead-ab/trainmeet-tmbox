/* TrainMeet TMBox for NodeMCU ESP8266 + PCF8574 keypad + 16x2 I2C LCD.
 * Arduino IDE: open this entire folder, not only the .ino file.
 * The local TrainMeet Server owns every traffic decision. No Cloud runtime.
 */
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoMqttClient.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <LiquidCrystal_I2C.h>
#include <WiFiManager.h>
#include <stddef.h>
#include "hardware_profile.h"
#include "device_settings.h"
#include "input_state.h"
#include "pcf_keypad.h"

#ifndef ESP8266
#error "Choose NodeMCU 1.0 (ESP-12E Module), not an ESP32 board."
#endif

LiquidCrystal_I2C lcd(TAMBOX_LCD_ADDRESS, 16, 2);
WiFiClient networkClient;
MqttClient mqtt(networkClient);
WiFiManager wifiManager;
KeyState keys;
InputLease lease;
String deviceId, deviceCode, bootId, apName;
String gatewayHost, panelId, sessionId, allowedKeys, commandId;
String shownLine1, shownLine2;
long revision = -1;
uint16_t gatewayPort = 1883;
bool lcdFound = false, keypadOK = false;
bool portalActive = false, saveRequested = false, mdnsStarted = false;
bool connectedBefore = false, wifiWasConnected = false, refreshRequested = false;
uint32_t nextConnection = 0, lastHello = 0, wifiLostAt = 0, lastKeyScan = 0;
uint32_t lastLcdCheck = 0;
uint32_t commandSequence = 0;
unsigned connectionFailures = 0;
WiFiManagerParameter* hostParameter = nullptr;
WiFiManagerParameter* portParameter = nullptr;

Settings settings{};

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
  Serial.printf("LCD |%s|%s|\n", first.c_str(), second.c_str());
  if (lcdFound) {
    lcd.setCursor(0, 0); lcd.print(first);
    lcd.setCursor(0, 1); lcd.print(second);
  }
}

void invalidate() {
  lease.clear(); panelId = ""; sessionId = ""; allowedKeys = ""; revision = -1;
  commandId = ""; keys.requireRelease();
}

void disconnectServer() {
  invalidate(); mqtt.stop(); connectedBefore = false;
}

void loadSettings() {
  EEPROM.begin(128);
  EEPROM.get(0, settings);
  if (!validSettings(settings)) {
    memset(&settings, 0, sizeof(settings)); settings.port = 1883;
  }
}

bool storeSettings(Settings next) {
  next.magic = 0x544d3836;
  next.checksum = settingsChecksum(next);
  if (memcmp(&next, &settings, sizeof(next)) == 0) return true;
  EEPROM.put(0, next);
  if (!EEPROM.commit()) return false;
  settings = next;
  return true;
}

void startPortal() {
  if (portalActive) return;
  disconnectServer();
  wifiManager.startConfigPortal(apName.c_str());
  portalActive = true;
  showFrame("INSTALLERA WIFI", apName);
}

void savePortalSettings() {
  if (!saveRequested) return;
  saveRequested = false;
  String host = hostParameter->getValue(); host.trim();
  String port = portParameter->getValue(); port.trim();
  bool validPort = port.length() > 0 && port.length() <= 5;
  for (size_t i = 0; i < port.length(); ++i) validPort &= isDigit(port[i]);
  const long number = port.toInt();
  if (!validPort || number < 1 || number > 65535 || host.indexOf('/') >= 0 || host.indexOf(':') >= 0 || host.indexOf(' ') >= 0) {
    showFrame("FEL SERVERADRESS", "IP + MQTT-PORT"); return;
  }
  Settings next{};
  host.toCharArray(next.host, sizeof(next.host)); next.port = uint16_t(number);
  if (!storeSettings(next)) { showFrame("KAN INTE SPARA", "FORSOK IGEN"); return; }
  disconnectServer(); gatewayHost = ""; nextConnection = millis(); wifiLostAt = millis();
  wifiManager.stopConfigPortal(); portalActive = false;
}

bool publish(const String& topic, JsonDocument& document, bool retained = false) {
  if (!mqtt.beginMessage(topic, (unsigned long)measureJson(document), retained, 1)) return false;
  serializeJson(document, mqtt);
  return mqtt.endMessage() == 1;
}

void hello() {
  JsonDocument message;
  message["protocol_version"] = 1;
  message["device_code"] = deviceCode;
  message["model"] = TAMBOX_MODEL;
  message["firmware_version"] = TAMBOX_FIRMWARE_VERSION;
  message["wifi_rssi"] = WiFi.RSSI();
  publish("tambox/v1/device/" + deviceId + "/hello", message);
  lastHello = millis(); refreshRequested = false;
}

void receiveMessage(int size) {
  const String topic = mqtt.messageTopic();
  const bool retained = mqtt.messageRetain();
  // Filter unused fields (routes/slots/ack snapshots) before allocating JSON.
  if (size < 2 || size > 8192) { invalidate(); return; }
  JsonDocument filter, message;
  for (const char* key : {"status", "panel_id", "traffic_session_id", "revision", "command_id", "reason"}) filter[key] = true;
  filter["assigned_panel_ids"][0] = true;
  filter["display"]["line1"] = true; filter["display"]["line2"] = true;
  filter["interaction"]["allowed_keys"][0] = true;
  if (deserializeJson(message, mqtt, DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(10))) {
    invalidate(); refreshRequested = true; return;
  }
  if (topic.endsWith("/assignment")) {
    const String status = message["status"] | "";
    const String nextPanel = message["assigned_panel_ids"][0] | "";
    if (status != "assigned" || !nextPanel.length()) {
      invalidate(); showFrame("KOPPLA BOXEN", deviceCode); return;
    }
    if (panelId != nextPanel) {
      invalidate(); panelId = nextPanel; refreshRequested = true;
      showFrame("BOX KOPPLAD", "HAMTAR PANEL...");
    }
  } else if (topic.indexOf("/snapshot/") >= 0) {
    if (retained || !panelId.length() || panelId != (message["panel_id"] | "") ||
        !message["revision"].is<long>() || !message["display"]["line1"].is<const char*>() ||
        !message["display"]["line2"].is<const char*>() || !message["interaction"]["allowed_keys"].is<JsonArray>()) return;
    const String nextSession = message["traffic_session_id"] | "";
    if (!nextSession.length()) return;
    const long nextRevision = message["revision"];
    if (nextRevision < 0 || (nextSession == sessionId && nextRevision < revision)) return;
    sessionId = nextSession; revision = nextRevision;
    allowedKeys = "";
    for (JsonVariant key : message["interaction"]["allowed_keys"].as<JsonArray>()) {
      const String value = key.as<String>();
      if (value.length() == 1 && strchr(TAMBOX_KEYS, value[0])) allowedKeys += value;
    }
    lease.snapshot(millis(), false);
    if (keypadOK && lcdFound) showFrame(message["display"]["line1"].as<String>(), message["display"]["line2"].as<String>());
  } else if (topic.endsWith("/ack")) {
    if (!lease.waiting || commandId != (message["command_id"] | "")) return;
    // Do not resend unacknowledged input or enable keys until a new snapshot.
    lease.acknowledged(); commandId = ""; refreshRequested = true;
    if (String(message["status"] | "") == "rejected") showFrame("KOMMANDO NEKAT", "HAMTAR NYTT LAGE");
  } else if (topic.startsWith("tambox/v1/gateway/") && topic.endsWith("/status")) {
    if (String(message["status"] | "") != "online") {
      invalidate(); showFrame("SERVER BORTA", "FORSOKER IGEN");
    } else refreshRequested = true;
  }
}

bool resolveServer() {
  if (settings.host[0]) {
    gatewayHost = settings.host; gatewayPort = settings.port;
    // ESP8266's regular DNS is not guaranteed to resolve a .local name.
    if (gatewayHost.endsWith(".local")) {
      if (!mdnsStarted) return false;
      const int count = MDNS.queryService("tambox", "tcp", 1000);
      for (int i = 0; i < count; ++i) {
        String host = MDNS.hostname(i); if (host.endsWith(".")) host.remove(host.length() - 1);
        if (!host.endsWith(".local")) host += ".local";
        if (host.equalsIgnoreCase(gatewayHost)) { gatewayHost = MDNS.IP(i).toString(); return true; }
      }
      return false;
    }
    return true;
  }
  if (!mdnsStarted) return false;
  const int count = MDNS.queryService("tambox", "tcp");
  // Never pick an arbitrary runtime server when several advertise themselves.
  if (count != 1) {
    showFrame(count > 1 ? "FLERA SERVRAR" : "SOKER SERVER", count > 1 ? "HALL * FOR VAL" : deviceCode);
    return false;
  }
  gatewayHost = MDNS.IP(0).toString(); gatewayPort = MDNS.port(0);
  return gatewayHost != "0.0.0.0" && gatewayPort > 0;
}

void connectServer() {
  disconnectServer();
  if (!resolveServer()) return;
  showFrame("ANSLUTER SERVER", deviceCode);
  if (!mqtt.connect(gatewayHost.c_str(), gatewayPort)) return;
  connectedBefore = true; connectionFailures = 0; keys.requireRelease();
  if (!mqtt.subscribe("tambox/v1/device/" + deviceId + "/assignment", 1) ||
      !mqtt.subscribe("tambox/v1/client/" + deviceId + "/snapshot/+", 1) ||
      !mqtt.subscribe("tambox/v1/client/" + deviceId + "/ack", 1) ||
      !mqtt.subscribe("tambox/v1/gateway/+/status", 1)) { disconnectServer(); return; }
  JsonDocument presence;
  presence["status"] = "online"; presence["device_code"] = deviceCode;
  publish("tambox/v1/client/" + deviceId + "/presence", presence, true);
  hello();
}

void sendKey(char key) {
  if (!mqtt.connected() || !lease.allowed(millis()) || !keypadOK || !lcdFound ||
      !panelId.length() || !sessionId.length() || allowedKeys.indexOf(key) < 0) return;
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
  command["device_uptime_ms"] = millis();
  lease.sent(millis());
  if (!publish("tambox/v1/client/" + deviceId + "/command", command)) {
    disconnectServer(); showFrame("INGET SERVER-SVAR", "KONTROLLERA LAGE");
  }
}

void scanHardware() {
  Serial.println("I2C scan (7-bit addresses):");
  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) Serial.printf("  0x%02X\n", address);
    yield();
  }
  Wire.beginTransmission(TAMBOX_LCD_ADDRESS);
  lcdFound = Wire.endTransmission() == 0;
  if (lcdFound) { lcd.init(); lcd.backlight(); }
  keypadOK = keypadWrite(0xff);
  if (!lcdFound) Serial.println("LCD missing: traffic input disabled.");
  if (!keypadOK) Serial.println("Keypad missing: traffic input disabled.");
}

void setup() {
  Serial.begin(115200);
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
  loadSettings();
  static char portText[6]; snprintf(portText, sizeof(portText), "%u", settings.port);
  static WiFiManagerParameter hostField("server", "TrainMeet Server: IP/namn (tomt = auto)", settings.host, 63);
  static WiFiManagerParameter portField("mqttport", "MQTT-port (inte webbport)", portText, 5);
  hostParameter = &hostField; portParameter = &portField;
  wifiManager.addParameter(hostParameter); wifiManager.addParameter(portParameter);
  wifiManager.setConfigPortalBlocking(false); wifiManager.setConnectTimeout(15);
  wifiManager.setSaveParamsCallback([]() { saveRequested = true; });
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
      shownLine1 = ""; shownLine2 = "";
      if (lcdFound) { lcd.init(); lcd.backlight(); }
      else Serial.println("LCD disconnected: traffic input disabled.");
    }
  }
  if (uint32_t(now - lastKeyScan) >= 10) {
    lastKeyScan = now;
    uint16_t mask = 0;
    const bool wasOK = keypadOK;
    keypadOK = keypadScan(mask);
    if (keypadOK != wasOK) {
      lease.clear(); keys.requireRelease(); refreshRequested = true;
    }
    const KeyEvent event = keys.update(mask, keypadOK, now);
    if (!keypadOK) showFrame("KNAPPSATS SAKNAS", "KONTROLLERA I2C");
#ifdef TAMBOX_HARDWARE_CHECK
    if (event.pressed) showFrame("TANGENT", String(event.pressed));
#else
    if (event.pressed) sendKey(event.pressed);
    if (event.reset) {
      // Enter setup without changing permanent identity or erasing good Wi-Fi.
      // Wi-Fi and server can be changed in the portal, including while online.
      startPortal();
    }
#endif
  }
#ifndef TAMBOX_HARDWARE_CHECK
  if (portalActive) { wifiManager.process(); savePortalSettings(); }
  const bool wifiConnected = WiFi.status() == WL_CONNECTED;
  if (!wifiConnected) {
    if (wifiWasConnected) {
      disconnectServer(); wifiLostAt = now; gatewayHost = "";
      if (mdnsStarted) MDNS.close(); mdnsStarted = false;
    }
    if (!portalActive) showFrame("NAT SAKNAS", "FORSOKER IGEN");
    if (!portalActive && uint32_t(now - wifiLostAt) >= 30000) startPortal();
  } else {
    if (!mdnsStarted) mdnsStarted = MDNS.begin(deviceId.c_str());
    if (mdnsStarted) MDNS.update();
    // Keep a manually opened portal open until its save or explicit exit.
    if (portalActive && !wifiManager.getConfigPortalActive()) {
      portalActive = false; nextConnection = now;
    }
    if (!portalActive) {
      if (!mqtt.connected()) {
        if (connectedBefore) { disconnectServer(); showFrame("SERVER BORTA", "FORSOKER IGEN"); }
        if (due(now, nextConnection)) {
          connectServer();
          connectionFailures = min(connectionFailures + 1, 4u);
          nextConnection = millis() + 2000 * connectionFailures;
        }
      } else {
        mqtt.poll();
        // A callback may just have set lastSnapshot later than the loop's `now`.
        const uint32_t current = millis();
        if (lease.expired(current) || lease.timedOut(current)) {
          disconnectServer(); showFrame("INGET SERVER-SVAR", "KONTROLLERA LAGE"); nextConnection = now + 1000;
        } else if (refreshRequested || uint32_t(current - lastHello) >= 10000) hello();
      }
    }
  }
  wifiWasConnected = wifiConnected;
#endif
  delay(2); // ESP8266 Wi-Fi + watchdog must get CPU time.
}
