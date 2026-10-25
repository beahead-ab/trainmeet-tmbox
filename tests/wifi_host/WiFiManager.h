#pragma once
#include "../terminal_host/Arduino.h"
#include <functional>
#include <map>
#include <memory>
#include <vector>
#define F(text) text
constexpr int HTTP_GET = 0, HTTP_POST = 1, WIFI_STA = 1, WL_CONNECTED = 3;
struct HostWiFi {
  bool autoReconnect = true, persistentValue = true;
  int statusValue = 0;
  String ssid, password;
  unsigned attempts = 0;
  void persistent(bool value) { persistentValue = value; }
  void setAutoReconnect(bool value) { autoReconnect = value; }
  void disconnect() { statusValue = 0; }
  void mode(int) {}
  int status() { return statusValue; }
  void begin(const char* name, const char* pass) { ssid = name; password = pass; ++attempts; }
};
inline HostWiFi WiFi;
struct HostServer {
  std::map<std::pair<std::string, int>, std::function<void()>> handlers;
  std::map<std::string, String> args;
  int code = 0;
  String body;
  void on(const char* path, int method, std::function<void()> callback) { handlers[{path, method}] = callback; }
  bool hasArg(const String& name) { return args.count(name); }
  String arg(const String& name) { const auto found = args.find(name); return found == args.end() ? String() : found->second; }
  void sendHeader(const char*, const char*) {}
  void send(int status, const char*, const String& text) { code = status; body = text; }
  void request(int method) { handlers.at({"/networks", method})(); }
};
class WiFiManager {
 public:
  std::unique_ptr<HostServer> server{new HostServer};
  String legacySsid = "Home", legacyPassword = "home-password";
  std::function<void()> serverCallback;
  std::vector<String> menu;
  String getWiFiSSID() { return legacySsid; }
  String getWiFiPass() { return legacyPassword; }
  void setWiFiAutoReconnect(bool) {}
  void setRestorePersistent(bool) {}
  void setShowInfoErase(bool) {}
  void setCustomMenuHTML(const char*) {}
  void setMenu(const char* items[], uint8_t count) { for (uint8_t i = 0; i < count; ++i) menu.push_back(items[i]); }
  void setWebServerCallback(std::function<void()> callback) { serverCallback = callback; }
  String htmlEntities(String text) {
    String out;
    for (char c : text) {
      if (c == '&') out += "&amp;";
      else if (c == '\'') out += "&#39;";
      else if (c == '<') out += "&lt;";
      else if (c == '>') out += "&gt;";
      else out += c;
    }
    return out;
  }
};
