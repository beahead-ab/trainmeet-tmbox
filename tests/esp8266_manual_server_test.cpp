#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

#include "../firmware/esp8266/TrainMeetTambox8266/manual_server.h"

namespace {

const uint16_t defaultHttpPort = 8000;

void accepts(const char* input, const char* host, uint16_t port,
             uint16_t fallback = defaultHttpPort) {
  TrainMeetManual::Address address;
  std::memset(&address, 0xa5, sizeof(address));
  assert(TrainMeetManual::parseAddress(input, fallback, address));
  assert(std::strcmp(address.host, host) == 0);
  assert(address.httpPort == port);
  // The entire fixed-size string buffer is initialized, not merely terminated.
  for (size_t index = std::strlen(host); index < sizeof(address.host); ++index) {
    assert(address.host[index] == '\0');
  }
}

void rejects(const char* input) {
  TrainMeetManual::Address address;
  std::memset(&address, 0xa5, sizeof(address));
  unsigned char before[sizeof(address)];
  std::memcpy(before, &address, sizeof(address));
  assert(!TrainMeetManual::parseAddress(input, defaultHttpPort, address));
  assert(std::memcmp(before, &address, sizeof(address)) == 0);
}

void testAutomaticAndFallback() {
  accepts("", "", defaultHttpPort);
  accepts(" \t\r\n\v\f ", "", 8787, 8787);
  accepts("trainmeet", "trainmeet", defaultHttpPort);
  accepts("TRAINMEET", "trainmeet", 8080, 8080);
  accepts("train-meet", "train-meet", defaultHttpPort);
  accepts("trainmeet.local", "trainmeet.local", defaultHttpPort);
  accepts("TRAINMEET.LOCAL", "trainmeet.local", defaultHttpPort);
  accepts("\t trainmeet.local \r\n", "trainmeet.local", defaultHttpPort);
  accepts("server.room.local", "server.room.local", defaultHttpPort);
  accepts("192.168.1.20", "192.168.1.20", defaultHttpPort);
  accepts("0.0.0.0", "0.0.0.0", defaultHttpPort);
  accepts("255.255.255.255", "255.255.255.255", defaultHttpPort);
  accepts("1", "1", defaultHttpPort);
  accepts("1234", "1234", defaultHttpPort);
  accepts("1.2.server", "1.2.server", defaultHttpPort);
}

void testExplicitHttpPort() {
  // 8787 is explicitly the HTTP port. A separate broker setting stays 1883.
  const uint16_t mqttPort = 1883;
  TrainMeetManual::Address address = {};
  assert(TrainMeetManual::parseAddress("trainmeet.local:8787", 8000, address));
  assert(address.httpPort == 8787 && address.httpPort != mqttPort);
  assert(mqttPort == 1883);

  accepts("192.168.1.20:8787", "192.168.1.20", 8787);
  accepts("TrainMeet.LOCAL:8787", "trainmeet.local", 8787);
  accepts("trainmeet.local:1", "trainmeet.local", 1);
  accepts("trainmeet.local:65535", "trainmeet.local", 65535);
  accepts("trainmeet.local:00008787", "trainmeet.local", 8787);
  accepts(" trainmeet.local:8787 ", "trainmeet.local", 8787);
  accepts("trainmeet.local:1883", "trainmeet.local", 1883); // Still an HTTP port.
}

void testHttpUrls() {
  accepts("http://trainmeet", "trainmeet", defaultHttpPort);
  accepts("http://trainmeet/", "trainmeet", defaultHttpPort);
  accepts("http://trainmeet.local:8787", "trainmeet.local", 8787);
  accepts("HTTP://TRAINMEET.LOCAL:8787/", "trainmeet.local", 8787);
  accepts("hTtP://TrainMeet.LoCaL/", "trainmeet.local", defaultHttpPort);
  accepts("hTtP://192.168.1.20:8787/", "192.168.1.20", 8787);
  accepts(" \t http://trainmeet.local:8787/ \r\n", "trainmeet.local", 8787);
  accepts("http://trainmeet.local/", "trainmeet.local", 8787, 8787);
}

void testRejectedAddresses() {
  const char* invalid[] = {
    NULL, "https://trainmeet.local", "HTTPS://trainmeet.local/",
    "ftp://trainmeet.local", "ws://trainmeet.local", "//trainmeet.local",
    "http:", "http:/trainmeet", "http:///trainmeet", "http://", "http:///",
    "http://:8787", "http://:8787/", ":8787", ":", "/", "trainmeet/",
    "trainmeet:8787/", "http://trainmeet//", "http://trainmeet/path",
    "http://trainmeet:8787/path", "trainmeet/path", "trainmeet.local?x=1",
    "trainmeet#fragment", "http://trainmeet/?x=1", "http://trainmeet/#fragment",
    "http://user@trainmeet", "http://user:secret@trainmeet:8787/",
    "user@trainmeet", "http://trainmeet:8787@other", "http://trainmeet%2flocal",
    "trainmeet:", "http://trainmeet:/", "trainmeet:0", "trainmeet:0000",
    "trainmeet:65536", "trainmeet:999999999999999999999999999999999999",
    "trainmeet:-1", "trainmeet:+8787", "trainmeet:87.87", "trainmeet:8e3",
    "trainmeet:port", "trainmeet:0x2253", "trainmeet:8787:1883",
    "trainmeet :8787", "trainmeet: 8787", "trainmeet:8787 /", "train meet",
    "http:// trainmeet/", "http://trainmeet /", "train\tmeet", "train\rmeet",
    "train\nmeet", "train_meet", "train~meet", "train,meet", "train;meet",
    "[::1]", "http://[::1]:8787/", "2001:db8::1", "trainmeet\\local",
    ".local", "trainmeet..local", "trainmeet.", "-trainmeet", "trainmeet-",
    "trainmeet.-local", "trainmeet-.local", "...", "256.1.2.3", "1.2.3.999",
    "1.2.3", "1.2.3.4.5", "1234.1.2.3", "1..2.3", "1.2.3.", "1.2.3.-1"
  };
  for (const char* input : invalid) rejects(input);

  // Every embedded control and non-ASCII byte is rejected, regardless of locale.
  for (unsigned value = 1; value <= 255; ++value) {
    const char byte = static_cast<char>(value);
    const bool validHostCharacter = (byte >= 'a' && byte <= 'z') ||
        (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') ||
        byte == '-' || byte == '.';
    if (!validHostCharacter) {
      const std::string input = std::string("train") + byte + "meet";
      rejects(input.c_str());
    }
    if (value < 32 && byte != '\t' && byte != '\n' && byte != '\v' &&
        byte != '\f' && byte != '\r') {
      rejects((std::string(1, byte) + "trainmeet").c_str());
      rejects((std::string("trainmeet") + byte).c_str());
    }
  }
}

void testLimitsAndNoTruncation() {
  const std::string maximumHost(63, 'a');
  accepts(maximumHost.c_str(), maximumHost.c_str(), defaultHttpPort);
  accepts(("http://" + maximumHost + ":8787/").c_str(), maximumHost.c_str(), 8787);
  rejects(std::string(64, 'a').c_str());
  rejects(("http://" + std::string(64, 'a') + ":8787/").c_str());
  rejects((maximumHost + ".local").c_str());
  accepts((std::string(192, ' ') + maximumHost).c_str(), maximumHost.c_str(),
          defaultHttpPort);
  rejects((std::string(193, ' ') + maximumHost).c_str());
  rejects(std::string(10000, ' ').c_str());
  char unterminated[256];
  std::memset(unterminated, 'a', sizeof(unterminated));
  rejects(unterminated);

  // A failure must not destroy a previously accepted manual server.
  TrainMeetManual::Address address = {};
  assert(TrainMeetManual::parseAddress("http://trainmeet.local:8787/", 8000, address));
  TrainMeetManual::Address before = address;
  assert(!TrainMeetManual::parseAddress("http://other.local:0/", 8000, address));
  assert(std::memcmp(&before, &address, sizeof(address)) == 0);
}

} // namespace

int main() {
  testAutomaticAndFallback();
  testExplicitHttpPort();
  testHttpUrls();
  testRejectedAddresses();
  testLimitsAndNoTruncation();
  std::cout << "ESP8266 manual server: host, HTTP port, validation and atomic output passed\n";
}
