// The display setting, compiled as it is shipped. No Arduino, no EEPROM:
// a record is just bytes.
#include <cassert>
#include <cstring>
#include <iostream>
#include "../firmware/esp8266/TrainMeetTMBox8266/display_setting.h"
#include "../firmware/esp8266/TrainMeetTMBox8266/network_setup.h"

using namespace TrainMeetDisplay;

// 4096 bytes of EEPROM: the old settings from 0, the server binding at 1024,
// this record at 1536, the cached texts from 2048 (capacity 2032).
static_assert(EEPROM_OFFSET >= 1024 + int(sizeof(TrainMeetNetwork::ServerBindingRecord)), "overlaps the server binding");
static_assert(EEPROM_OFFSET + int(sizeof(Record)) <= 2048, "overlaps the cached texts");

void a_saved_size_comes_back() {
  for (const Size size : {SMALL, LARGE}) {
    const Record saved = record(size);
    assert(sizeOf(saved) == size);
  }
  assert(SMALL.rows == 2 && SMALL.cols == 16 && LARGE.rows == 4 && LARGE.cols == 20);
}

void anything_unreadable_is_16x2() {
  Record erased; memset(&erased, 0xFF, sizeof erased);          // flash that was never written
  assert(sizeOf(erased) == SMALL);
  Record zeros{}; assert(sizeOf(zeros) == SMALL);
  Record damaged = record(LARGE); damaged.cols = 16; assert(sizeOf(damaged) == SMALL);  // checksum no longer fits
  Record flipped = record(LARGE); flipped.checksum ^= 1; assert(sizeOf(flipped) == SMALL);
  Record foreign = record(Size{3, 17});                          // a valid record of a size nobody supports
  assert(sizeOf(foreign) == SMALL);
  Record other = record(LARGE); other.magic = 0x544d5349; other.checksum = checksum(other);  // the server binding's magic
  assert(sizeOf(other) == SMALL);
}

void the_portal_field_is_read_forgivingly() {
  const struct { const char* text; Size size; } accepted[] = {
    {"16x2", SMALL}, {"2x16", SMALL}, {"20x4", LARGE}, {"4x20", LARGE}, {" 20x4 ", LARGE}, {"20X4", LARGE},
    {"20 x 4", LARGE}, {"16\xC3\x97" "2", SMALL}, {"20\xC3\x97" "4", LARGE}};
  for (const auto& item : accepted) {
    Size size = item.size == LARGE ? SMALL : LARGE;               // start from the other one
    assert(parse(item.text, size)); assert(size == item.size);
  }
  for (const char* refused : {"", "16", "20x5", "2x20", "4x16", "sixteen", "20x4x", "0x0", "20x4 and more", "\xC3", "\xC3\x97"}) {
    Size size = LARGE; assert(!parse(refused, size)); assert(size == LARGE);   // left alone
  }
  Size size = SMALL; assert(!parse(nullptr, size)); assert(size == SMALL);
  assert(!strcmp(name(SMALL), "16x2") && !strcmp(name(LARGE), "20x4"));
  Size roundTrip = SMALL; assert(parse(name(LARGE), roundTrip) && roundTrip == LARGE);
}

int main() {
  a_saved_size_comes_back(); anything_unreadable_is_16x2(); the_portal_field_is_read_forgivingly();
  std::cout << "ESP8266 display setting: 3 scenarios passed\n";
}
