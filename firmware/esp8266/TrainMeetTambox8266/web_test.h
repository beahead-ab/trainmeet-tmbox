#pragma once
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include "web_test_page.h"

// Included after the firmware's shared command path: HTTP keys must use
// exactly the same MQTT command, lease, revision and acknowledgement rules.
ESP8266WebServer testWeb(80);
PairingThrottle pairingThrottle;
String webPin, webToken;
bool testWebRunning = false;

void stopVirtualInput() {
  webSession.enabled = false;
  lease.clear(); keys.requireRelease(); refreshRequested = true;
}

void stopWebTestServer() {
  if (testWebRunning) testWeb.stop();
  testWebRunning = false;
  stopVirtualInput(); webSession.clear(); webToken = "";
}

void webHeaders() {
  testWeb.sendHeader("Cache-Control", "no-store");
  testWeb.sendHeader("X-Content-Type-Options", "nosniff");
  testWeb.sendHeader("X-Frame-Options", "DENY");
  testWeb.sendHeader("Referrer-Policy", "no-referrer");
  testWeb.sendHeader("Content-Security-Policy", "default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; frame-ancestors 'none'; form-action 'self'");
}

void webJson(int status, JsonDocument& data) {
  String body;
  serializeJson(data, body);
  webHeaders(); testWeb.send(status, "application/json; charset=utf-8", body);
}

void webError(int status, const char* message) {
  JsonDocument data; data["error"] = message; webJson(status, data);
}

bool webRequestAllowed(bool mutation) {
  // Reject DNS rebinding and cross-origin form/JS requests. No wildcard CORS.
  String host = testWeb.hostHeader(); host.toLowerCase();
  if (host.endsWith(":80")) host.remove(host.length() - 3);
  if (host != WiFi.localIP().toString() && host != deviceId + ".local") {
    webError(403, "Använd boxens lokala IP-adress."); return false;
  }
  if (mutation) {
    String origin = testWeb.header("Origin"); origin.toLowerCase();
    if (origin.endsWith(":80")) origin.remove(origin.length() - 3);
    if (origin != "http://" + host ||
        !testWeb.header("Content-Type").startsWith("application/json")) {
      webError(403, "Använd boxens egen testsida."); return false;
    }
  }
  return true;
}

bool ownsWebSession() {
  return webSession.valid(millis()) &&
         hasWebTestCookie(testWeb.header("Cookie").c_str(), webToken.c_str());
}

bool webAuthorize(bool mutation) {
  if (!webRequestAllowed(mutation)) return false;
  if (!ownsWebSession()) {
    webError(401, "Parkoppla telefonen med webbtestkoden."); return false;
  }
  return true;
}

bool readWebBody(JsonDocument& data) {
  const String body = testWeb.arg("plain");
  if (body.length() > 256 || deserializeJson(data, body, DeserializationOption::NestingLimit(3)) || !data.is<JsonObject>()) {
    webError(400, "Ogiltigt kommando."); return false;
  }
  return true;
}

bool webSnapshotReady() {
  return WiFi.status() == WL_CONNECTED && mqtt.connected() && panelId.length() &&
         sessionId.length() && lease.allowed(millis());
}

void webStatus() {
  JsonDocument data;
  data["deviceCode"] = deviceCode; data["deviceId"] = deviceId;
  data["firmware"] = TAMBOX_FIRMWARE_VERSION;
  data["ip"] = WiFi.localIP().toString();
  data["server"] = gatewayHost.length() ? gatewayHost + ":" + gatewayPort : "";
  data["configuredServer"] = settings.host; data["configuredPort"] = settings.port;
  data["httpPort"] = settings.reserved ? settings.reserved : 8787;
  data["connected"] = mqtt.connected(); data["panel"] = panelId;
  data["session"] = sessionId; data["revision"] = revision;
  data["lcd"] = lcdFound; data["keypad"] = keypadOK;
  data["webTest"] = webSession.enabled && webSession.valid(millis());
  data["waiting"] = lease.waiting;
  data["fresh"] = lease.fresh && !lease.expired(millis());
  data["canStart"] = webSnapshotReady();
  data["ready"] = webSnapshotReady() && webSession.enabled;
  data["allowedKeys"] = allowedKeys;
  data["line1"] = serverLine1.length() ? serverLine1 : shownLine1;
  data["line2"] = serverLine2.length() ? serverLine2 : shownLine2;
  webJson(200, data);
}

void setupWebTest() {
  char pin[7]; snprintf(pin, sizeof(pin), "%06u", unsigned(ESP.random() % 1000000));
  webPin = pin;
  // Printed only at boot, over the physical USB connection, never in the API.
  Serial.printf("Webbtestkod: %s (galler till nasta omstart)\n", webPin.c_str());
  testWeb.collectHeaders("Cookie", "Origin", "Content-Type");
  testWeb.on("/", HTTP_GET, []() {
    if (!webRequestAllowed(false)) return;
    webHeaders(); testWeb.send_P(200, "text/html; charset=utf-8", WEB_TEST_PAGE);
  });
  testWeb.on("/api/login", HTTP_POST, []() {
    if (!webRequestAllowed(true)) return;
    if (pairingThrottle.blocked(millis())) { webError(429, "För många försök. Vänta en minut."); return; }
    JsonDocument data;
    if (!readWebBody(data)) return;
    if (!data["pin"].is<const char*>() || webPin != data["pin"].as<String>()) {
      pairingThrottle.failed(millis()); webError(403, "Fel webbtestkod."); return;
    }
    if (webSession.valid(millis()) && !ownsWebSession()) {
      webError(409, "En annan telefon är parkopplad. Koppla från den eller vänta tio minuter."); return;
    }
    stopVirtualInput();
    char token[33];
    snprintf(token, sizeof(token), "%08lx%08lx%08lx%08lx", (unsigned long)ESP.random(),
             (unsigned long)ESP.random(), (unsigned long)ESP.random(), (unsigned long)ESP.random());
    webToken = token; webSession.pair(millis());
    testWeb.sendHeader("Set-Cookie", "tm_test=" + webToken + "; Path=/; HttpOnly; SameSite=Strict");
    webStatus();
  });
  testWeb.on("/api/status", HTTP_GET, []() {
    if (webAuthorize(false)) webStatus(); // Polling does not renew idle timeout.
  });
  testWeb.on("/api/test", HTTP_POST, []() {
    if (!webAuthorize(true)) return;
    JsonDocument data; if (!readWebBody(data)) return;
    if (!data["enabled"].is<bool>()) { webError(400, "Ange på eller av."); return; }
    if (data["enabled"].as<bool>()) {
      if (!webSnapshotReady()) { webError(409, "Invänta stationstilldelning och aktuell skärmbild från servern."); return; }
      webSession.enable(millis());
      // A fresh snapshot is required after changing input source.
      lease.clear(); keys.requireRelease(); refreshRequested = true;
    } else { stopVirtualInput(); webSession.touch(millis()); }
    webStatus();
  });
  testWeb.on("/api/key", HTTP_POST, []() {
    if (!webAuthorize(true)) return;
    JsonDocument data; if (!readWebBody(data)) return;
    const String key = data["key"] | "";
    if (key.length() != 1 || !strchr(TAMBOX_KEYS, key[0]) ||
        !data["session"].is<const char*>() || sessionId != data["session"].as<String>() ||
        !data["revision"].is<long>() || data["revision"].as<long>() != revision) {
      webError(409, "Skärmbilden har ändrats. Läs aktuellt läge och försök igen."); return;
    }
    if (!sendKey(key[0], true)) {
      webError(409, "Tangenten är spärrad eller ett kommando väntar på serversvar."); return;
    }
    webSession.touch(millis()); webStatus();
  });
  testWeb.on("/api/server", HTTP_POST, []() {
    if (!webAuthorize(true)) return;
    JsonDocument data; if (!readWebBody(data)) return;
    if (!data["host"].is<const char*>() || !data["port"].is<unsigned>() || !data["httpPort"].is<unsigned>()) {
      webError(400, "Ange serveradress och MQTT-port."); return;
    }
    String host = data["host"].as<String>(); host.trim();
    const unsigned port = data["port"].as<unsigned>();
    const unsigned httpPort = data["httpPort"].as<unsigned>();
    bool valid = host.length() <= 63 && port > 0 && port <= 65535 && httpPort > 0 && httpPort <= 65535;
    for (unsigned i = 0; i < host.length(); ++i) {
      const char c = host[i];
      valid &= (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '.' || c == '-';
    }
    if (!valid) { webError(400, "Skriv IP/namn utan http, sökväg eller port. MQTT-port: 1–65535."); return; }
    Settings next{}; host.toCharArray(next.host, sizeof(next.host)); next.port = uint16_t(port);
    next.reserved = uint16_t(httpPort);
    if (!storeSettings(next)) { webError(500, "Inställningarna kunde inte sparas."); return; }
    // Keep WiFiManager's form consistent if setup is opened later.
    hostParameter->setValue(settings.host, 63);
    char portText[6]; snprintf(portText, sizeof(portText), "%u", settings.port);
    portParameter->setValue(portText, 5);
    disconnectServer(); gatewayHost = ""; nextConnection = millis();
    webSession.touch(millis()); webStatus();
  });
  testWeb.on("/api/enroll", HTTP_POST, []() {
    if (!webAuthorize(true)) return;
    JsonDocument data; if (!readWebBody(data)) return;
    String code = data["code"] | "";
    code.replace("-", ""); code.replace(" ", ""); code.toUpperCase();
    bool valid = code.length() == 6;
    for (unsigned i = 0; i < code.length(); ++i) valid &= isalnum(static_cast<unsigned char>(code[i]));
    if (!valid) { webError(400, "Ange den lokala serverns sexsiffriga anslutningskod."); return; }
    if (!mqtt.connected() || !gatewayHost.length()) {
      webError(409, "Invänta lokal serveranslutning först."); return;
    }
    // A dedicated endpoint never assigns all panels (as the older /v1/pair
    // workflow can). Older servers fail closed with an update instruction.
    stopVirtualInput();
    WiFiClient pairingClient;
    HTTPClient http;
    const unsigned port = settings.reserved ? settings.reserved : 8787;
    if (!http.begin(pairingClient, "http://" + gatewayHost + ":" + port + "/v1/tmbox/enroll")) {
      webError(502, "Kunde inte nå serverns webbanslutning."); return;
    }
    http.setTimeout(3000);
    http.addHeader("Content-Type", "application/json");
    JsonDocument request; request["client_id"] = deviceId; request["pairing_code"] = code;
    String body; serializeJson(request, body);
    const int status = http.POST(body);
    code = ""; body = ""; request.clear();
    if (status == 404) { http.end(); webError(409, "Uppdatera TrainMeet Server. Den saknar stöd för TMBox-anslutningskod."); return; }
    if (status == 401) { http.end(); webError(400, "Servern nekade koden eller boxen. Kontrollera kodens giltighet och enhetens behörighet."); return; }
    if (status == 429) { http.end(); webError(429, "För många kodförsök mot servern. Vänta en minut."); return; }
    if (status != 201 || http.getSize() < 0 || http.getSize() > 1536) {
      http.end(); webError(502, "Ingen giltig bekräftelse från servern. Kontrollera dess webbport."); return;
    }
    JsonDocument response;
    const bool parsed = !deserializeJson(response, http.getString(), DeserializationOption::NestingLimit(3));
    http.end();
    if (!parsed || response["accepted"] != true || String(response["client_id"] | "") != deviceId) {
      webError(502, "Servern bekräftade inte boxens ID."); return;
    }
    webSession.touch(millis()); refreshRequested = true;
    JsonDocument result; result["accepted"] = true;
    result["awaitingStation"] = response["awaiting_station_assignment"] | true;
    webJson(200, result);
  });
  testWeb.on("/api/logout", HTTP_POST, []() {
    if (!webAuthorize(true)) return;
    stopVirtualInput(); webSession.clear(); webToken = "";
    testWeb.sendHeader("Set-Cookie", "tm_test=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
    JsonDocument data; data["ok"] = true; webJson(200, data);
  });
  testWeb.onNotFound([]() { webError(404, "Sidan finns inte."); });
}

void tickWebTest() {
  if (portalActive || WiFi.status() != WL_CONNECTED) {
    if (testWebRunning) stopWebTestServer();
    return;
  }
  if (webSession.paired && !webSession.valid(millis())) {
    stopVirtualInput(); webSession.clear(); webToken = "";
  }
  if (!testWebRunning) {
    testWeb.begin(); testWebRunning = true;
    if (mdnsStarted) MDNS.addService("http", "tcp", 80);
    Serial.printf("TMBox webbtest: http://%s/ (anvand webbtestkoden fran USB)\n", WiFi.localIP().toString().c_str());
  }
  testWeb.handleClient();
}
