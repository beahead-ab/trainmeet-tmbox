#pragma once
// Only the Arduino primitives used by server_terminal.h. The production
// adapter and the pinned ArduinoJson parser are compiled without replacement.
#include <cstdint>
#include <string>
#include <type_traits>

inline uint32_t hostMillis = 1000;
inline uint32_t millis() { return hostMillis; }

class String : public std::string {
 public:
  using std::string::string;
  using std::string::operator=;
  String() = default;
  String(const std::string& value) : std::string(value) {}
  String(char value) : std::string(1, value) {}
  template <class T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, char>, int> = 0>
  String(T value) : std::string(std::to_string(value)) {}
  bool startsWith(const String& value) const { return rfind(value, 0) == 0; }
  bool endsWith(const String& value) const {
    return size() >= value.size() && compare(size() - value.size(), value.size(), value) == 0;
  }
  void remove(size_t index) { erase(index); }
  String& append(const char* value) { std::string::append(value); return *this; }
  String& append(const char* value, size_t length) { std::string::append(value, length); return *this; }
};
