#pragma once
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include "web_test_page.h"
#include "manual_server.h"

// Included after the firmware's shared command path: HTTP keys must use
// exactly the same MQTT command, lease, revision and acknowledgement rules.
ESP8266WebServer testWeb(80);
PairingThrottle pairingThrottle;
String webPin, webToken;
bool testWebRunning = false;

void stopVirtualInput() {
  webSession.enabled = false;
  trainEntry.clear();
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
  data["serverHost"] = gatewayHost; data["serverPort"] = gatewayPort;
  data["configuredServer"] = settings.host; data["configuredPort"] = settings.port;
  data["httpPort"] = settings.reserved ? settings.reserved : 8787;
  data["connected"] = mqtt.connected(); data["panel"] = panelId;
  data["enrollmentReady"] = serverEnrollment.ready(mqtt.connected());
  data["session"] = sessionId; data["revision"] = revision;
  data["lcd"] = lcdFound; data["keypad"] = keypadOK;
  data["webTest"] = webSession.enabled && webSession.valid(millis());
  data["waiting"] = lease.waiting;
  data["fresh"] = lease.fresh && !lease.expired(millis());
  data["canStart"] = webSnapshotReady();
  data["ready"] = webSnapshotReady() && webSession.enabled;
  String webKeys = allowedKeys;
  if (enteringTrain && !trainEntry.active) webKeys = "*";
  if (enteringTrain && !trainEntry.canSubmit()) webKeys.replace("#", "");
  if (trainEntry.active && trainEntry.value.size() >= 5)
    for (char digit = '0'; digit <= '9'; ++digit) webKeys.replace(String(digit), "");
  data["allowedKeys"] = webKeys;
  data["localEntry"] = trainEntry.active;
  data["entryNeedsUpdate"] = enteringTrain && !trainEntry.active;
  data["line1"] = serverLine1.length() ? inputLine1() : shownLine1;
  data["line2"] = serverLine2.length() ? inputLine2() : shownLine2;
  webJson(200, data);
}

void setupWebTest() {
  char pin[7]; snprintf(pin, sizeof(pin), "%06u", unsigned(ESP.random() % 1000000));
  webPin = pin;
  // Printed only at boot, over the physical USB connection, never in the API.
  TMBOX_LOG("Webbtestkod: %s (galler till nasta omstart)\n", webPin.c_str());
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
    TMBOX_DEBUG("Phone web session paired (credentials not logged)\n");
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
      TMBOX_DEBUG("Web test enabled\n");
      // A fresh snapshot is required after changing input source.
      trainEntry.clear(); lease.clear(); keys.requireRelease(); refreshRequested = true;
    } else { stopVirtualInput(); webSession.touch(millis()); TMBOX_DEBUG("Web test disabled\n"); }
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
    const unsigned port = data["port"].as<unsigned>();
    const unsigned httpPort = data["httpPort"].as<unsigned>();
    TrainMeetManual::Address address{};
    if (!port || port > 65535 || !httpPort || httpPort > 65535 ||
        !TrainMeetManual::parseAddress(data["host"].as<const char*>(), uint16_t(httpPort), address)) {
      webError(400, "Ange IP/namn eller http://IP:8787 utan sökväg. Portarna måste vara 1–65535. HTTPS stöds inte av boxen."); return;
    }
    Settings next{}; memcpy(next.host, address.host, sizeof(next.host)); next.port = uint16_t(port);
    next.reserved = address.httpPort;
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
    if (!serverEnrollment.ready(true)) {
      webError(409, "Servern har inte bekräftat boxens registrering ännu. Vänta på anslutningen och försök igen."); return;
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
    TMBOX_DEBUG("Local server enrollment: HTTP status=%d (code not logged)\n", status);
    code = ""; body = ""; request.clear();
    if (status <= 0) {
      http.end(); webError(502, "Kunde inte nå serverns webbport. Kontrollera IP-adressen och webbporten (normalt 8787, inte MQTT-port 1883)."); return;
    }
    if (status == 404) { http.end(); webError(409, "Uppdatera TrainMeet Server. Den saknar stöd för TMBox-anslutningskod."); return; }
    if (status == 429) { http.end(); webError(429, "För många kodförsök mot servern. Vänta en minut."); return; }
    if (status >= 400 && status < 500) {
      // Preserve the server's explanation (expired code, undiscovered or
      // disabled box). Never turn a server's 401 into a phone-session logout.
      JsonDocument failure;
      bool parsed = http.getSize() >= 0 && http.getSize() <= 1536 &&
                    !deserializeJson(failure, http.getString(), DeserializationOption::NestingLimit(3));
      http.end();
      const String detail = parsed ? String(failure["message"] | "") : String();
      if (detail.length() && detail.length() <= 300) webError(400, detail.c_str());
      else webError(400, "Servern nekade koden eller boxen. Kontrollera att du använder den lokala serverns anslutningskod och att boxen inte är spärrad.");
      return;
    }
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
    TMBOX_LOG("TMBox webbtest: http://%s/ (anvand webbtestkoden fran USB)\n", WiFi.localIP().toString().c_str());
  }
  testWeb.handleClient();
}
