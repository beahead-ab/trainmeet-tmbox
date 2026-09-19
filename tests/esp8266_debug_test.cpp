#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>

namespace {
uint32_t fakeNow = 0;
unsigned clockReads = 0;

struct FakeSerial {
  std::string output;
  unsigned writes = 0;

  int printf(const char* format, ...) {
    char buffer[1024];
    va_list arguments;
    va_start(arguments, format);
    const int length = std::vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    assert(length >= 0 && static_cast<unsigned>(length) < sizeof(buffer));
    output.append(buffer, static_cast<unsigned>(length));
    ++writes;
    return length;
  }

  void clear() { output.clear(); writes = 0; }
};

FakeSerial Serial;
[[maybe_unused]] unsigned long millis() { ++clockReads; return fakeNow; }
}

#include "../firmware/esp8266/TrainMeetTambox8266/debug_log.h"

#ifndef EXPECTED_DEBUG
#error "The test runner must declare the expected debug mode"
#endif
static_assert(TAMBOX_DEBUG_ENABLED == EXPECTED_DEBUG, "Unexpected debug default/override");

std::string expectedLine(const char* function, unsigned line, const char* message) {
#if TAMBOX_DEBUG_ENABLED
  char prefix[160];
  std::snprintf(prefix, sizeof(prefix), "[%10lu ms] %-22s : (%u) ",
                static_cast<unsigned long>(fakeNow), function, line);
  return std::string(prefix) + message;
#else
  (void)function;
  (void)line;
  return message;
#endif
}

void testNormalLogging() {
  fakeNow = 12345;
  clockReads = 0;
  Serial.clear();
  int effects = 0;
  const unsigned callLine = __LINE__ + 1;
  TMBOX_LOG("normal %d %s\n", ++effects, "ready");
  assert(effects == 1);
  assert(Serial.output == expectedLine(__func__, callLine, "normal 1 ready\n"));
  assert(clockReads == (TAMBOX_DEBUG_ENABLED ? 1u : 0u));

  Serial.clear();
  const unsigned noArgumentsLine = __LINE__ + 1;
  TMBOX_LOG("no arguments\n");
  assert(Serial.output == expectedLine(__func__, noArgumentsLine, "no arguments\n"));

  // A macro must remain one statement in an unbraced if/else.
  if (false)
    TMBOX_LOG("must not print\n");
  else
    ++effects;
  assert(effects == 2);
  assert(Serial.output.find("must not print") == std::string::npos);
}

void testVerboseLogging() {
  fakeNow = 20000;
  clockReads = 0;
  Serial.clear();
  int effects = 0;
  const unsigned callLine = __LINE__ + 1;
  TMBOX_DEBUG("verbose %d\n", ++effects);
#if TAMBOX_DEBUG_ENABLED
  assert(effects == 1);
  assert(Serial.output == expectedLine(__func__, callLine, "verbose 1\n"));
  assert(clockReads > 0);
#else
  (void)callLine;
  assert(effects == 0);
  assert(Serial.output.empty() && Serial.writes == 0 && clockReads == 0);
  // Proves real compile-out, not merely a runtime `if (false)`.
  TMBOX_DEBUG("removed %s\n", symbol_that_must_not_be_compiled);
#endif

  Serial.clear();
  const unsigned noArgumentsLine = __LINE__ + 1;
  TMBOX_DEBUG("verbose without arguments\n");
#if TAMBOX_DEBUG_ENABLED
  assert(Serial.output == expectedLine(__func__, noArgumentsLine, "verbose without arguments\n"));
#else
  (void)noArgumentsLine;
  assert(Serial.output.empty());
#endif
  if (false)
    TMBOX_DEBUG("must not print\n");
  else
    ++effects;
  assert(effects == (TAMBOX_DEBUG_ENABLED ? 2 : 1));
}

#if TAMBOX_DEBUG_ENABLED
void testVerboseRateLimit() {
  fakeNow = 30000;
  Serial.clear();
  int effects = 0;
  for (unsigned attempt = 0; attempt < 21; ++attempt) {
    TMBOX_DEBUG("limited %d\n", ++effects);
  }
  assert(effects == 20); // Dropped calls do not evaluate message arguments.
  assert(Serial.writes == 40); // Prefix plus message for each accepted call.
  assert(Serial.output.find("limited 20\n") != std::string::npos);
  assert(Serial.output.find("limited 21\n") == std::string::npos);

  const std::string fullWindow = Serial.output;
  fakeNow = 30999;
  TMBOX_DEBUG("still limited %d\n", ++effects);
  assert(effects == 20 && Serial.output == fullWindow);

  // Normal status/error output is never swallowed by the verbose budget.
  const unsigned normalLine = __LINE__ + 1;
  TMBOX_LOG("normal remains available\n");
  assert(Serial.output == fullWindow + expectedLine(__func__, normalLine, "normal remains available\n"));

  fakeNow = 31000;
  Serial.clear();
  const unsigned reopenedLine = __LINE__ + 1;
  TMBOX_DEBUG("new window %d\n", ++effects);
  assert(effects == 21);
  assert(Serial.output == expectedLine(__func__, reopenedLine, "new window 21\n"));
}

void testVerboseMillisWrap() {
  const uint32_t start = 0xffffff00u;
  for (unsigned count = 0; count < 20; ++count) {
    assert(TrainMeetDebug::allowVerbose(start));
  }
  assert(!TrainMeetDebug::allowVerbose(start));
  assert(!TrainMeetDebug::allowVerbose(uint32_t(start + 999u)));
  assert(TrainMeetDebug::allowVerbose(uint32_t(start + 1000u)));
  for (unsigned count = 1; count < 20; ++count) {
    assert(TrainMeetDebug::allowVerbose(uint32_t(start + 1000u)));
  }
  assert(!TrainMeetDebug::allowVerbose(uint32_t(start + 1000u)));
}
#endif

int main() {
  testNormalLogging();
  testVerboseLogging();
#if TAMBOX_DEBUG_ENABLED
  testVerboseRateLimit();
  testVerboseMillisWrap();
#endif
  std::cout << "ESP8266 debug=" << TAMBOX_DEBUG_ENABLED
            << ": source location, formatting, side effects and rate limits passed\n";
}
