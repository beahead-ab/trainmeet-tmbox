#include "../firmware/common/wifi_profiles_arduino.h"
#include <cassert>
#include <algorithm>
#include <iostream>
using namespace TrainMeetWiFi;

void form(WiFiManager& manager) {
  manager.server->args.clear();
  for (unsigned i = 0; i < MAX_NETWORKS; ++i) {
    manager.server->args["s" + std::to_string(i)] = "";
    manager.server->args["p" + std::to_string(i)] = "";
  }
}
int main() {
#ifdef ESP8266
  HostEEPROM::committed.fill(0x5a);
#endif
  WiFiManager manager;
  SavedNetworks box(manager); box.begin();
  assert(Preferences::writes == 1); // upgrade is actually persisted
  assert(!WiFi.autoReconnect && !WiFi.persistentValue);
  assert(!box.process(false) && WiFi.ssid == "Home" && WiFi.password == "home-password");
  manager.serverCallback();
  assert(std::find(manager.menu.begin(), manager.menu.end(), "update") != manager.menu.end());
  manager.server->request(HTTP_GET);
  assert(manager.server->code == 200 && manager.server->body.find("Home") != std::string::npos);
  assert(manager.server->body.find("home-password") == std::string::npos); // never render stored secrets
  assert(!box.takeSaved());
  unsigned before = WiFi.attempts;
  hostMillis += ATTEMPT_MS;
  assert(!box.process(true) && WiFi.attempts == before); // portal pauses sequence

  form(manager);
  manager.server->args["s0"] = "Meeting"; manager.server->args["p0"] = "meeting-password";
  manager.server->args["s2"] = "Home"; // blank keeps its password by SSID, across rows
  manager.server->request(HTTP_POST);
  assert(manager.server->code == 200 && box.takeSaved() && !box.takeSaved());
  box.restart(); assert(!box.process(false) && WiFi.ssid == "Meeting");
  hostMillis += ATTEMPT_MS;
  assert(!box.process(false) && WiFi.ssid == "Home" && WiFi.password == "home-password");

  // Only a confirmed connection records success; repeated online polls do not wear flash.
  WiFi.statusValue = WL_CONNECTED;
  before = WiFi.attempts;
  assert(!box.process(false) && Preferences::writes == 3);
  assert(!box.process(false) && Preferences::writes == 3 && WiFi.attempts == before);
  WiFi.statusValue = 0;
  assert(!box.process(false) && WiFi.ssid == "Home");

  WiFiManager rebootManager; SavedNetworks reboot(rebootManager); reboot.begin();
  assert(Preferences::writes == 3); // persisted last success survives a power cycle
  assert(!reboot.process(false) && WiFi.ssid == "Home");
  assert(WiFi.password == "home-password");
  rebootManager.serverCallback();
  form(rebootManager); rebootManager.server->args["s0"] = "Home"; rebootManager.server->args["o0"] = "on";
  rebootManager.server->request(HTTP_POST);
  assert(rebootManager.server->code == 200 && reboot.takeSaved());
  reboot.restart(); assert(!reboot.process(false) && WiFi.ssid == "Home" && WiFi.password.empty());

  form(rebootManager); rebootManager.server->args["s0"] = "Duplicate"; rebootManager.server->args["s1"] = "Duplicate";
  before = Preferences::writes; rebootManager.server->request(HTTP_POST);
  assert(rebootManager.server->code == 400 && Preferences::writes == before);
  form(rebootManager); rebootManager.server->args["s0"] = std::string(33, 'x');
  rebootManager.server->request(HTTP_POST); assert(rebootManager.server->code == 400 && Preferences::writes == before);
  form(rebootManager); rebootManager.server->args.erase("s0");
  rebootManager.server->request(HTTP_POST); assert(rebootManager.server->code == 400 && Preferences::writes == before);
  form(rebootManager); Preferences::failWrites = true;
  rebootManager.server->request(HTTP_POST); assert(rebootManager.server->code == 500 && !reboot.takeSaved());
  Preferences::failWrites = false; rebootManager.server->request(HTTP_POST);
  assert(rebootManager.server->code == 200 && reboot.takeSaved());
  WiFiManager emptyManager; SavedNetworks empty(emptyManager); empty.begin();
  assert(empty.process(false)); // deleting all networks does not resurrect the SDK credential on reboot
#ifdef ESP8266
  for (size_t i = 0; i < HostEEPROM::committed.size(); ++i)
    if (i < size_t(EEPROM_OFFSET) || i >= EEPROM_OFFSET + sizeof(Record)) assert(HostEEPROM::committed[i] == 0x5a);
#endif
  std::cout << "Wi-Fi Arduino adapter: migration, persistence, portal saves, privacy and deletion passed\n";
}
