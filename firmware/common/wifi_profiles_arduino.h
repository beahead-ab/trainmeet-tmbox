#pragma once
#include <WiFiManager.h>
#include "wifi_profiles.h"
#ifdef ESP8266
#include <EEPROM.h>
#else
#include <Preferences.h>
#endif

namespace TrainMeetWiFi {
// EEPROM 512..1011: unused between legacy settings and the server binding
// at 1024. The display at 1536 and cached language at 2048 stay intact.
constexpr int EEPROM_OFFSET = 512;
static_assert(EEPROM_OFFSET + sizeof(Record) <= 1024, "Wi-Fi overlaps server identity");

class SavedNetworks {
 public:
  explicit SavedNetworks(WiFiManager& manager) : manager(manager) {}

  void begin() {
#ifdef ESP8266
    EEPROM.begin(4096); EEPROM.get(EEPROM_OFFSET, saved); EEPROM.end();
#else
    Preferences storage;
    storage.begin("tmbox-wifi", true);
    if (storage.getBytesLength("networks") == sizeof saved) storage.getBytes("networks", &saved, sizeof saved);
    storage.end();
#endif
    if (!valid(saved)) {
      saved = Record{};
      Record migrated{};
      const String ssid = manager.getWiFiSSID(), password = manager.getWiFiPass();
      prefer(migrated, ssid.c_str(), password.c_str()); // migrate the existing SDK credential once
      persist(migrated);
    }
    // Our sequence owns reconnection. Driver retries must not race the next slot
    // or wear flash by saving every fallback network as the primary network.
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(); // the stored list, rather than SDK autoconnect, decides the first choice
    manager.setWiFiAutoReconnect(false);
    manager.setRestorePersistent(false);
    manager.setShowInfoErase(false); // list removal is explicit on our page
    manager.setCustomMenuHTML("<form action='/networks' method='get'><button>Sparade wifi-nät</button></form>");
    const char* menu[] = {"wifi", "custom", "info", "exit", "sep", "update"};
    manager.setMenu(menu, sizeof menu / sizeof menu[0]);
    manager.setWebServerCallback([this]() {
      manager.server->on("/networks", HTTP_GET, [this]() { show(); });
      manager.server->on("/networks", HTTP_POST, [this]() { saveForm(); });
    });
  }

  // Called only after WiFiManager successfully saves its ordinary Wi-Fi form.
  void rememberCurrent() {
    Record candidate = saved;
    const String ssid = manager.getWiFiSSID(), password = manager.getWiFiPass();
    if (prefer(candidate, ssid.c_str(), password.c_str())) persist(candidate);
    sequence.restart();
  }
  void restart() { sequence.restart(); }
  bool takeSaved() { const bool result = formSaved; formSaved = false; return result; }
  bool process(bool portalActive) {
    if (portalActive) return false;
    const int action = sequence.tick(saved, WiFi.status() == WL_CONNECTED, millis());
    if (action == Sequence::Portal) return true;
    if (action >= 0) {
      WiFi.persistent(false);
      WiFi.setAutoReconnect(false);
      WiFi.disconnect();
      WiFi.mode(WIFI_STA);
      const Network& n = saved.networks[action];
      WiFi.begin(n.ssid, n.password);
    }
    return false;
  }

 private:
  WiFiManager& manager;
  Record saved{};
  Sequence sequence;
  bool formSaved = false;

  bool persist(Record candidate) {
    seal(candidate);
    if (!std::memcmp(&candidate, &saved, sizeof saved) && valid(saved)) return true;
    bool ok;
#ifdef ESP8266
    EEPROM.begin(4096); EEPROM.put(EEPROM_OFFSET, candidate); ok = EEPROM.commit(); EEPROM.end();
#else
    Preferences storage;
    if (!storage.begin("tmbox-wifi", false)) return false;
    ok = storage.putBytes("networks", &candidate, sizeof candidate) == sizeof candidate;
    storage.end();
#endif
    if (ok) saved = candidate;
    return ok;
  }
  String escape(const char* text) { return manager.htmlEntities(String(text)); }
  void show() {
    String page = F("<!doctype html><html lang='sv'><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>Sparade wifi-nät</title><style>body{font:18px system-ui;max-width:36em;margin:2em auto;padding:0 1em}"
      "input:not([type=checkbox]),button{box-sizing:border-box;width:100%;padding:.6em;margin:.3em 0 1em}fieldset{margin:1em 0}</style>"
      "<h1>Sparade wifi-nät</h1><p>Boxen provar nät 1–5 i ordning, högst 15 sekunder per nät. Tomma rader hoppas över. "
      "Om inget fungerar öppnas installationsnätet.</p><p>Tomt lösenord behåller det sparade lösenordet för samma nät, även när nätet flyttas. "
      "Markera öppet nät för att ta bort lösenordet. Töm nätets namn för att ta bort nätet.</p>"
      "<p>Vanliga Configure WiFi sparar nätet som förstaval och flyttar övriga nedåt. Är alla fem platser upptagna ersätts det femte nätet.</p>"
      "<form method='post' action='/networks'>");
    for (size_t i = 0; i < MAX_NETWORKS; ++i) {
      const String index(i);
      page += "<fieldset><legend>Nät " + String(i + 1) + "</legend><label>Namn (SSID)<input name='s" + index +
        "' maxlength='32' value='" + escape(saved.networks[i].ssid) + "'></label><label>Lösenord<input type='password' name='p" +
        index + "' maxlength='64' autocomplete='new-password'></label><label><input type='checkbox' name='o" + index +
        "'> Öppet nät (utan lösenord)</label></fieldset>";
    }
    page += F("<button>Spara och anslut</button></form><p><a href='/'>Tillbaka utan att spara</a></p></html>");
    manager.server->sendHeader("Cache-Control", "no-store");
    manager.server->send(200, "text/html; charset=utf-8", page);
  }
  void saveForm() {
    Record candidate{};
    for (size_t i = 0; i < MAX_NETWORKS; ++i) {
      const String index(i), ssid = manager.server->arg("s" + index), password = manager.server->arg("p" + index);
      if (!manager.server->hasArg("s" + index) || !manager.server->hasArg("p" + index) ||
          !edit(candidate.networks[i], saved, ssid.c_str(), password.c_str(), manager.server->hasArg("o" + index))) {
        manager.server->send(400, "text/plain; charset=utf-8", "Ogiltiga nätuppgifter. Inget sparades."); return;
      }
      for (size_t j = 0; j < i; ++j) {
        if (ssid.length() && ssid == candidate.networks[j].ssid) {
          manager.server->send(400, "text/plain; charset=utf-8", "Samma nät får bara finnas en gång. Inget sparades."); return;
        }
      }
    }
    if (!persist(candidate)) {
      manager.server->send(500, "text/plain; charset=utf-8", "Kunde inte spara. Försök igen."); return;
    }
    manager.server->sendHeader("Cache-Control", "no-store");
    manager.server->send(200, "text/html; charset=utf-8",
      "<!doctype html><meta charset='utf-8'><p>Näten är sparade. Boxen ansluter nu. Om inget nät fungerar öppnas installationsnätet igen.</p>");
    // Close the portal only after its HTTP handler has returned.
    formSaved = true;
  }
};
} // namespace TrainMeetWiFi
