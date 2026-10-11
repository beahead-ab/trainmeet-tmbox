#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Which display the box has, chosen in the setup portal and kept in EEPROM.
// An HD44780 behind an I2C backpack cannot tell how big it is, so the
// operator says so once. No Arduino dependency: the host tests compile this
// header as it is.
//
// Layout of the 4 KB EEPROM: 0 the old connection settings (ignored), 1024 the
// server binding, 1536 this record, 2048 the cached texts. Never overlap.
namespace TrainMeetDisplay {

struct Size {
  uint8_t rows, cols;
  bool operator==(const Size& other) const { return rows == other.rows && cols == other.cols; }
  bool operator!=(const Size& other) const { return !(*this == other); }
};
// 16x2 is what every box has had so far, and what an unreadable record means.
constexpr Size SMALL{2, 16};
constexpr Size LARGE{4, 20};

constexpr int EEPROM_OFFSET = 1536;
constexpr uint32_t MAGIC = 0x54444953;  // "TDIS"

struct Record {
  uint32_t magic;
  uint8_t rows, cols, reserved[2];
  uint32_t checksum;
};

inline uint32_t checksum(const Record& record) {
  uint32_t hash = 2166136261u;
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&record);
  for (size_t i = 0; i < offsetof(Record, checksum); ++i) hash = (hash ^ bytes[i]) * 16777619u;
  return hash;
}

inline Record record(Size size) {
  Record result{};
  result.magic = MAGIC; result.rows = size.rows; result.cols = size.cols;
  result.checksum = checksum(result);
  return result;
}

// What the record says; anything unreadable or unknown is 16x2.
inline Size sizeOf(const Record& saved) {
  if (saved.magic != MAGIC || saved.checksum != checksum(saved)) return SMALL;
  const Size size{saved.rows, saved.cols};
  return size == LARGE ? LARGE : SMALL;
}

inline const char* name(Size size) { return size == LARGE ? "20x4" : "16x2"; }

// What an operator typed in the portal: "20x4", "4x20", "16 x 2", "20×4",
// in either case. False for anything else, and `size` is then left alone.
inline bool parse(const char* text, Size& size) {
  char squeezed[8]; size_t length = 0;
  for (; text && *text; ++text) {
    if (*text == ' ') continue;
    char c = *text;
    if (uint8_t(text[0]) == 0xC3 && uint8_t(text[1]) == 0x97) { ++text; c = 'x'; }  // the times sign
    else if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    if (length + 1 >= sizeof squeezed) return false;
    squeezed[length++] = c;
  }
  squeezed[length] = '\0';
  if (!strcmp(squeezed, "16x2") || !strcmp(squeezed, "2x16")) { size = SMALL; return true; }
  if (!strcmp(squeezed, "20x4") || !strcmp(squeezed, "4x20")) { size = LARGE; return true; }
  return false;
}

}  // namespace TrainMeetDisplay
