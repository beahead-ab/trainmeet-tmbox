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
#include "../../common/lcd_text.h"
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
WebTestSession webSession;
// Local status texts in the box's language, cached by firmware before 0.7.0.
// The catalog is keyed on the Swedish folded to ASCII (FORSOKER IGEN), and a
// message equal to its key is that same Swedish: the box keeps its own
// spelling with the dots (FÖRSÖKER IGEN).
JsonDocument deviceUi;
String uiText(const char* text) {
  char key[48]; TrainMeetLcd::foldText(text, key, sizeof key);
  const char* message = deviceUi["messages"][(const char*)key].as<const char*>();
  return message && strcmp(message, key) ? String(message) : String(text);
}

// The old connection settings occupied the beginning of EEPROM. Reserve a
// separate bounded cache at the end; never overwrite those settings.
void loadCachedDeviceUI() {
  constexpr int offset = 2048, capacity = 2032;
  EEPROM.begin(4096);
  uint32_t magic, size, expected; EEPROM.get(offset, magic); EEPROM.get(offset + 4, size); EEPROM.get(offset + 8, expected);
  if (magic == 0x544d5549 && size > 0 && size < capacity) {
    String body; body.reserve(size); uint32_t hash = 2166136261u;
    for (uint32_t i = 0; i < size; ++i) { char c = EEPROM.read(offset + 12 + i); body += c; hash = (hash ^ uint8_t(c)) * 16777619u; }
    if (hash == expected && deserializeJson(deviceUi, body)) deviceUi.clear();
  }
  EEPROM.end();
}
String deviceId, deviceCode, bootId, apName;
String gatewayHost;
String rememberedServerId, discoveredServerId;
WiFiManagerParameter forgetServer("forgetserver", "Byt TrainMeet Server (behall Wi-Fi)", "1", 1, "type=\"checkbox\"", WFM_LABEL_AFTER);
bool forgetServerRequested = false;
String shownLine1, shownLine2;
uint16_t gatewayPort = 1883;
bool lcdFound = false, keypadOK = false;
bool portalActive = false, saveRequested = false, mdnsStarted = false;
bool connectedBefore = false, wifiWasConnected = false;
uint32_t nextConnection = 0, wifiLostAt = 0, lastKeyScan = 0;
uint32_t lastLcdCheck = 0;
uint32_t commandSequence = 0;
unsigned connectionFailures = 0;

#ifndef TAMBOX_HARDWARE_CHECK
void stopWebTestServer();
#endif

bool due(uint32_t now, uint32_t when) { return int32_t(now - when) >= 0; }

// The box's own two lines. Å, Ä and Ö are drawn in CGRAM with the server's
// glyphs (lcd_text.h); until 0.7.4 they were folded to A and O here.
void showFrame(const String& one, const String& two) {
  char first[64], second[64];
  TrainMeetLcd::fitText(one.c_str(), 16, first, sizeof first);
  TrainMeetLcd::fitText(two.c_str(), 16, second, sizeof second);
  if (shownLine1 == first && shownLine2 == second) return;
  shownLine1 = first; shownLine2 = second;
  TMBOX_LOG("LCD |%s|%s|\n", first, second);
  if (lcdFound) {
    TrainMeetLcd::Screen screen(2, 16);
    screen.line(0, first); screen.line(1, second);
    screen.draw(lcd);
  }
}

void disconnectServer() {
  // The display showed the server's frame and its glyphs: the next local
  // text is written even if it is the one shown before the session.
  shownLine1 = ""; shownLine2 = "";
  terminal.reset(); keys.requireRelease();
  mqtt.stop(); connectedBefore = false;
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

void receiveMessage(int size) {
  if (size < 2 || size > 8192) { disconnectServer(); return; }
  const String topic = mqtt.messageTopic();
  const bool retained = mqtt.messageRetain();
  String body; body.reserve(size);
  while (mqtt.available()) body += char(mqtt.read());
  if (!terminal.started) return;
  terminal.receive(topic, body, retained);
  if (lcdFound) terminal.draw(lcd);
}

bool resolveServer() {
  // Legacy EEPROM addresses are deliberately ignored. The local administrator
  // assigns this ID; operators never select hosts, ports or stations.
  if (!mdnsStarted) return false;
  const auto servers = TrainMeetNetwork::discoverServers();
  const auto selected = TrainMeetNetwork::selectServer(servers, rememberedServerId.c_str());
  if (selected.index < 0) {
    showFrame(selected.status == TrainMeetNetwork::DiscoveryStatus::Ambiguous ? "FLERA SERVRAR" : "SÖKER SERVER",
              selected.status == TrainMeetNetwork::DiscoveryStatus::Ambiguous ? "BE ADMIN HJÄLPA" : deviceCode);
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
  terminal.waitingText = uiText("VÄNTAR PÅ SVAR"); terminal.unansweredText = uiText("INGET SVAR");
  terminal.begin(mqtt, deviceId, deviceCode, "NodeMCU ESP8266 16x2", TAMBOX_FIRMWARE_VERSION,
                 bootId + "-" + String(++commandSequence));
}

bool sendKey(char key, bool virtualKey) {
  if (!terminal.started || !webSession.permits(virtualKey, keypadOK && lcdFound, millis())) return false;
  const bool accepted = terminal.press(key);
  if (lcdFound) terminal.draw(lcd);
  return accepted;
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
  loadCachedDeviceUI();
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
  showFrame("HÅRDVARUTEST", keypadOK ? "TRYCK ALLA 16" : "KNAPPSATS SAKNAS");
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
      lcdFound = available; keys.requireRelease();
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
      keys.requireRelease();
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
    if (!portalActive) showFrame(uiText("NÄT SAKNAS"), uiText("FÖRSÖKER IGEN"));
    if (!portalActive && uint32_t(now - wifiLostAt) >= 30000) startPortal();
  } else {
    if (!wifiWasConnected) TMBOX_LOG("Wi-Fi connected; box IP: %s\n", WiFi.localIP().toString().c_str());
    if (!mdnsStarted) mdnsStarted = MDNS.begin(deviceId.c_str());
    if (mdnsStarted) MDNS.update();
    if (!portalActive) {
      if (!mqtt.connected()) {
        if (connectedBefore) { disconnectServer(); showFrame(uiText("SERVER BORTA"), uiText("FÖRSÖKER IGEN")); }
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
          if (!terminal.tick()) { disconnectServer(); showFrame("SERVER SAKNAS", "FÖRSÖKER IGEN"); nextConnection = current + 1000; }
          else {
            if (terminal.fresh && String(terminal.frame["station_code"] | "").length() &&
                rememberedServerId != discoveredServerId) saveServerBinding(discoveredServerId);
            if (lcdFound) terminal.draw(lcd);
          }
        } else {
          // Connected without a terminal session: there is nothing else to
          // speak since 0.7.4 (Server 2.0.0 has no v1 protocol). Start over.
          disconnectServer(); nextConnection = current + 1000;
        }
      }
    }
  }
  wifiWasConnected = wifiConnected;
  tickWebTest();
#endif
  delay(2); // ESP8266 Wi-Fi + watchdog must get CPU time.
}
