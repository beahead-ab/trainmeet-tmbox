#pragma once

#include <stddef.h>
#include <stdint.h>

namespace TrainMeetManual {

struct Address {
  char host[64];
  uint16_t httpPort;
};

namespace detail {

inline bool isSpace(char value) {
  return value == ' ' || value == '\t' || value == '\r' || value == '\n' ||
         value == '\v' || value == '\f';
}

inline bool isDigit(char value) {
  return value >= '0' && value <= '9';
}

inline bool isAlpha(char value) {
  return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
}

inline char lowerAscii(char value) {
  return value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value;
}

inline bool hasHttpScheme(const char* begin, size_t length) {
  const char scheme[] = "http://";
  if (length < sizeof(scheme) - 1) return false;
  for (size_t index = 0; index < sizeof(scheme) - 1; ++index) {
    if (lowerAscii(begin[index]) != scheme[index]) return false;
  }
  return true;
}

inline bool isIpv4(const char* begin, const char* end) {
  unsigned octets = 0;
  unsigned value = 0;
  unsigned digits = 0;
  for (const char* cursor = begin; cursor != end; ++cursor) {
    if (*cursor == '.') {
      if (digits == 0 || ++octets > 3) return false;
      value = 0;
      digits = 0;
    } else {
      value = value * 10 + static_cast<unsigned>(*cursor - '0');
      if (++digits > 3 || value > 255) return false;
    }
  }
  return octets == 3 && digits != 0;
}

inline bool validHost(const char* begin, const char* end) {
  if (begin == end || end - begin > 63) return false;
  bool numeric = true;
  bool dotted = false;
  bool labelStart = true;
  char previous = '\0';
  for (const char* cursor = begin; cursor != end; ++cursor) {
    const char value = *cursor;
    if (value == '.') {
      if (labelStart || previous == '-') return false;
      dotted = true;
      labelStart = true;
    } else {
      if (!isAlpha(value) && !isDigit(value) && value != '-') return false;
      if (labelStart && value == '-') return false;
      if (!isDigit(value)) numeric = false;
      labelStart = false;
    }
    previous = value;
  }
  if (labelStart || previous == '-') return false;
  return !numeric || !dotted || isIpv4(begin, end);
}

} // namespace detail

// Accept a host, host:port or http://host[:port][/]. A blank host selects
// automatic discovery. The parsed port is the HTTP registration port, never
// the MQTT broker port. Failure leaves every byte of the caller's output alone.
inline bool parseAddress(const char* raw, uint16_t fallbackHttpPort, Address& out) {
  if (raw == NULL) return false;

  // Bound work even for malformed, unterminated form input. The caller supplies
  // a NUL-terminated string or at least 256 readable bytes; longer input fails.
  const size_t maxRawLength = 255;
  size_t length = 0;
  while (length <= maxRawLength && raw[length] != '\0') ++length;
  if (length > maxRawLength) return false;

  const char* begin = raw;
  const char* end = raw + length;
  while (begin != end && detail::isSpace(*begin)) ++begin;
  while (begin != end && detail::isSpace(end[-1])) --end;

  Address parsed = {};
  parsed.httpPort = fallbackHttpPort;
  if (begin == end) {
    out = parsed;
    return true;
  }

  const bool hasScheme = detail::hasHttpScheme(begin, static_cast<size_t>(end - begin));
  if (hasScheme) {
    begin += 7;
    if (begin != end && end[-1] == '/') --end;
  }

  const char* hostEnd = begin;
  while (hostEnd != end && *hostEnd != ':') ++hostEnd;
  if (!detail::validHost(begin, hostEnd)) return false;

  if (hostEnd != end) {
    const char* cursor = hostEnd + 1;
    if (cursor == end) return false;
    uint32_t port = 0;
    for (; cursor != end; ++cursor) {
      if (!detail::isDigit(*cursor)) return false;
      port = port * 10 + static_cast<uint32_t>(*cursor - '0');
      if (port > 65535) return false;
    }
    if (port == 0) return false;
    parsed.httpPort = static_cast<uint16_t>(port);
  }

  const size_t hostLength = static_cast<size_t>(hostEnd - begin);
  // DNS names are case-insensitive; normalize .LOCAL for the mDNS resolver.
  for (size_t index = 0; index < hostLength; ++index) {
    parsed.host[index] = detail::lowerAscii(begin[index]);
  }
  out = parsed;
  return true;
}

} // namespace TrainMeetManual
