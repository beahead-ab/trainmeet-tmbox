#pragma once
#include <ESP8266WebServer.h>
#include "web_test_page.h"

// Included after the firmware's shared command path: HTTP keys must use
// exactly the same MQTT command, lease, revision and acknowledgement rules.
ESP8266WebServer testWeb(80);
String webToken;
bool testWebRunning = false;

void stopVirtualInput() {
  terminal.digits = ""; terminal.dirty = true;
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
    webError(401, "Öppna webbpanelen igen för att ansluta."); return false;
  }
  return true;
}

bool readWebBody(JsonDocument& data) {
  const String body = testWeb.arg("plain");
  if (body.length() > 512 || deserializeJson(data, body, DeserializationOption::NestingLimit(3)) || !data.is<JsonObject>()) {
    webError(400, "Ogiltigt kommando."); return false;
  }
  return true;
}

bool webSnapshotReady() {
  if (terminal.started) return terminal.ready() && terminal.hasEntry();
  return WiFi.status() == WL_CONNECTED && mqtt.connected() && panelId.length() &&
         sessionId.length() && lease.allowed(millis());
}

void webStatus() {
  JsonDocument data;
  if (terminal.started) {
    data["serverDriven"] = true;
    data["deviceCode"] = deviceCode; data["deviceId"] = deviceId;
    data["firmware"] = TAMBOX_FIRMWARE_VERSION; data["ip"] = WiFi.localIP().toString();
    data["connected"] = mqtt.connected(); data["panel"] = terminal.frame["station_code"] | "";
    data["session"] = terminal.token(); data["revision"] = terminal.frame["revision"] | 0;
    data["lcd"] = lcdFound; data["keypad"] = keypadOK;
    data["webTest"] = webSession.enabled && webSession.valid(millis());
    data["waiting"] = terminal.pending.length() > 0; data["fresh"] = terminal.fresh;
    data["canStart"] = webSnapshotReady(); data["ready"] = webSnapshotReady() && webSession.enabled;
    data["allowedKeys"] = terminal.allowed(); data["languageAvailable"] = false;
    data["localEntry"] = terminal.hasEntry(); data["entryContext"] = terminal.context();
    data["entryValue"] = terminal.digits; data["entryLines"] = terminal.frame["entry"]["lines"];
    data["line1"] = terminal.line(0); data["line2"] = terminal.line(1);
    webJson(200, data); return;
  }
  data["deviceCode"] = deviceCode; data["deviceId"] = deviceId;
  data["firmware"] = TAMBOX_FIRMWARE_VERSION;
  data["ip"] = WiFi.localIP().toString();
  data["server"] = gatewayHost.length() ? gatewayHost + ":" + gatewayPort : "";
  data["serverHost"] = gatewayHost; data["serverPort"] = gatewayPort;
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
  if (languageReady && idleScreen && !trainEntry.active) webKeys += '#';
  if (languageMenu.open) webKeys = languageMenu.saving ? "" : "C#*";
  if (enteringTrain && !trainEntry.active) webKeys = "*";
  data["allowedKeys"] = webKeys;
  data["language"] = deviceUi["language"] | "sv";
  data["languageMenu"] = languageMenu.open;
  data["languageAvailable"] = languageReady && idleScreen && !trainEntry.active;
  data["entryLabel"] = uiText("Tag: ");
  data["localEntry"] = trainEntry.active;
  data["entryContext"] = trainEntry.entryContext().c_str();
  data["entryValue"] = trainEntry.value.c_str();
  data["entryNeedsUpdate"] = enteringTrain && !trainEntry.active;
  data["line1"] = serverLine1.length() ? inputLine1() : shownLine1;
  data["line2"] = serverLine2.length() ? inputLine2() : shownLine2;
  webJson(200, data);
}

void setupWebTest() {
  testWeb.collectHeaders("Cookie", "Origin", "Content-Type");
  testWeb.on("/", HTTP_GET, []() {
    if (!webRequestAllowed(false)) return;
    webHeaders(); testWeb.send_P(200, "text/html; charset=utf-8", WEB_TEST_PAGE);
  });
  testWeb.on("/api/session", HTTP_POST, []() {
    if (!webRequestAllowed(true)) return;
    JsonDocument data;
    if (!readWebBody(data)) return;
    if (ownsWebSession()) { webStatus(); return; }
    if (webSession.valid(millis()) && !ownsWebSession()) {
      webError(409, "En annan telefon använder boxen. Välj Koppla från telefonen där eller vänta tio minuter."); return;
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
      terminal.digits = ""; terminal.dirty = true; terminal.guard = millis() + 500;
    } else { stopVirtualInput(); webSession.touch(millis()); TMBOX_DEBUG("Web test disabled\n"); }
    webStatus();
  });
  testWeb.on("/api/key", HTTP_POST, []() {
    if (!webAuthorize(true)) return;
    JsonDocument data; if (!readWebBody(data)) return;
    const String key = data["key"] | "";
    if (terminal.started) {
      if (key.length() != 1 || !strchr(TAMBOX_KEYS, key[0]) ||
          String(data["session"] | "") != terminal.token() ||
          !webSession.permits(true, false, millis())) {
        webError(409, "Läs den aktuella displayen och försök igen."); return;
      }
      if (key[0] >= '0' && key[0] <= '9') { webError(400, "Siffror stannar i telefonen tills #."); return; }
      if (data.containsKey("train_number") && (key[0] != '#' ||
          !terminal.replaceDigits(data["entryContext"] | "", data["train_number"] | ""))) {
        webError(409, "Inmatningen är inte aktuell."); return;
      }
      if (!sendKey(key[0], true)) { webError(409, "Invänta aktuell skärmbild."); return; }
      webSession.touch(millis()); webStatus(); return;
    }
    if (key.length() != 1 || !strchr(TAMBOX_KEYS, key[0]) ||
        !data["session"].is<const char*>() || sessionId != data["session"].as<String>() ||
        !data["revision"].is<long>() || data["revision"].as<long>() != revision) {
      webError(409, "Skärmbilden har ändrats. Läs aktuellt läge och försök igen."); return;
    }
    if (trainEntry.active && key[0] >= '0' && key[0] <= '9') {
      webError(400, "Siffror skrivs lokalt. Bekräfta hela tågnumret med #."); return;
    }
    if (trainEntry.active && key[0] == '#') {
      if (!webSnapshotReady() || !webSession.permits(true, false, millis()) ||
          !data["entryContext"].is<const char*>() || !data["train_number"].is<const char*>() ||
          !trainEntry.replace(data["entryContext"].as<const char*>(), data["train_number"].as<const char*>())) {
        webError(409, "Inmatningen har ändrats. Läs aktuellt läge och ange tåget igen."); return;
      }
    } else if (data.containsKey("train_number")) {
      webError(409, "Tåginmatningen är inte längre aktiv."); return;
    }
    if (!sendKey(key[0], true)) {
      webError(409, "Tangenten är spärrad eller ett kommando väntar på serversvar."); return;
    }
    webSession.touch(millis()); webStatus();
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
    TMBOX_LOG("TMBox webbtest: http://%s/ (oppna sidan; station tilldelas av admin)\n", WiFi.localIP().toString().c_str());
  }
  testWeb.handleClient();
}
