#pragma once
#include <cstring>
#include <vector>
struct Preferences {
  inline static std::vector<unsigned char> bytes;
  inline static unsigned writes = 0;
  inline static bool failWrites = false;
  bool begin(const char*, bool) { return true; }
  void end() {}
  size_t getBytesLength(const char*) { return bytes.size(); }
  size_t getBytes(const char*, void* output, size_t length) { std::memcpy(output, bytes.data(), length); return length; }
  size_t putBytes(const char*, const void* input, size_t length) {
    if (failWrites) return 0;
    const auto* start = static_cast<const unsigned char*>(input);
    bytes.assign(start, start + length); ++writes; return length;
  }
};
