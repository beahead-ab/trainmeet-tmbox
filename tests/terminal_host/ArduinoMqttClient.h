#pragma once
#include "Arduino.h"
#include <vector>

class MqttClient {
 public:
  struct Message { String topic, body; bool retained; int qos; };
  bool online = true, beginOK = true, endOK = true;
  std::vector<String> subscriptions;
  std::vector<Message> messages;
  Message writing{};
  bool connected() const { return online; }
  void subscribe(const String& topic, int) { subscriptions.push_back(topic); }
  int beginMessage(const char* topic, size_t, bool retained, int qos) {
    writing = {topic, "", retained, qos}; return beginOK;
  }
  void print(const String& value) { writing.body += value; }
  int endMessage() {
    if (endOK) messages.push_back(writing);
    return endOK;
  }
};
