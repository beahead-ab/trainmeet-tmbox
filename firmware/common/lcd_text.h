#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Text the box writes itself - before and between server sessions, and the
// waiting overlay - on an HD44780 display. Shared by ESP8266 and ESP32.
//
// One cell per character. Å, Ä, Ö and the other letters the server uses are
// drawn in CGRAM from the same 5x8 bitmaps as the server's own frames
// (trainmeet-server terminal16_glyphs.py), so a box looks the same whether
// the server or the box wrote the line. The ROM's own codes above 127 differ
// between A00 and A02 displays and are never used.
//
// Until 0.7.4 the box folded these letters to A and O before writing, and
// the waiting overlay wrote its UTF-8 bytes straight to the display.
namespace TrainMeetLcd {

struct Glyph {
  uint32_t character;
  uint8_t rows[8];
};

// Same characters and bitmaps as GLYPHS in terminal16_glyphs.py.
constexpr Glyph GLYPHS[] = {
    {0x005C, {16, 8, 4, 2, 1, 0, 0, 0}},          // backslash (A02 ROM differs)
    {0x007E, {0, 0, 8, 21, 2, 0, 0, 0}},          // tilde (A02 ROM differs)
    {0x00C5, {4, 10, 4, 14, 17, 31, 17, 17}},     // Å
    {0x00E5, {4, 10, 4, 14, 1, 15, 17, 15}},      // å
    {0x00C4, {10, 0, 14, 17, 31, 17, 17, 0}},     // Ä
    {0x00E4, {10, 0, 14, 1, 15, 17, 15, 0}},      // ä
    {0x00D6, {10, 0, 14, 17, 17, 17, 14, 0}},     // Ö
    {0x00F6, {10, 0, 0, 14, 17, 17, 14, 0}},      // ö
    {0x00C6, {7, 12, 20, 23, 28, 20, 23, 0}},     // Æ
    {0x00E6, {0, 0, 26, 5, 15, 20, 15, 0}},       // æ
    {0x00D8, {15, 19, 21, 21, 21, 25, 30, 0}},    // Ø
    {0x00F8, {0, 0, 15, 19, 21, 25, 30, 0}},      // ø
    {0x00DC, {10, 0, 17, 17, 17, 17, 14, 0}},     // Ü
    {0x00FC, {10, 0, 0, 17, 17, 19, 13, 0}},      // ü
    {0x00E9, {2, 4, 14, 17, 31, 16, 14, 0}},      // é
    {0x00DF, {6, 9, 9, 14, 9, 9, 22, 0}},         // ß
    {0x1E9E, {14, 17, 18, 20, 18, 17, 22, 0}},    // ẞ
    {0x25C0, {1, 3, 7, 15, 7, 3, 1, 0}},          // ◀
    {0x25B6, {16, 24, 28, 30, 28, 24, 16, 0}},    // ▶
};
constexpr uint8_t SLOTS = 8;
constexpr uint8_t MAX_ROWS = 4;
constexpr uint8_t MAX_COLS = 20;

// The next character of UTF-8 text, advancing `text`. A malformed byte is
// one U+FFFD, so it still takes exactly one cell.
inline uint32_t next(const char*& text) {
  const uint8_t lead = uint8_t(*text++);
  if (lead < 0x80) return lead;
  int more = lead >= 0xF0 ? 3 : lead >= 0xE0 ? 2 : lead >= 0xC0 ? 1 : -1;
  if (more < 0) return 0xFFFD;
  uint32_t value = lead & (0x3F >> more);
  for (; more > 0; --more) {
    const uint8_t trail = uint8_t(*text);
    if ((trail & 0xC0) != 0x80) return 0xFFFD;
    value = (value << 6) | (trail & 0x3F);
    ++text;
  }
  return value;
}

// A combining accent belongs to the character before it, never a cell of its own.
inline bool combining(uint32_t character) { return character >= 0x300 && character <= 0x36F; }

inline const Glyph* glyph(uint32_t character) {
  for (const Glyph& candidate : GLYPHS) if (candidate.character == character) return &candidate;
  return nullptr;
}

// What a character becomes when it cannot be drawn.
inline char fold(uint32_t character) {
  if (character >= 32 && character <= 126) return char(character);
  switch (character) {
    case 0x00C5: case 0x00C4: case 0x00C6: return 'A';
    case 0x00E5: case 0x00E4: case 0x00E6: return 'a';
    case 0x00D6: case 0x00D8: return 'O';
    case 0x00F6: case 0x00F8: return 'o';
    case 0x00DC: return 'U';
    case 0x00FC: return 'u';
    case 0x00E9: return 'e';
    case 0x00DF: case 0x1E9E: return 's';
    case 0x25C0: return '<';
    case 0x25B6: return '>';
    default: return '?';
  }
}

// `text` with every character folded, e.g. "FÖRSÖKER IGEN" -> "FORSOKER IGEN":
// the server's message catalog is keyed on the folded Swedish.
inline void foldText(const char* text, char* out, size_t size) {
  size_t length = 0;
  while (*text && length + 1 < size) {
    const uint32_t character = next(text);
    if (!combining(character)) out[length++] = fold(character);
  }
  if (size) out[length] = '\0';
}

// At most `cols` characters of `text`, padded with spaces: the line as it
// reads on the display, in UTF-8 (for the web status page).
inline void fitText(const char* text, uint8_t cols, char* out, size_t size) {
  size_t length = 0;
  uint8_t cells = 0;
  while (*text && cells < cols) {
    const char* start = text;
    const uint32_t character = next(text);
    const size_t bytes = size_t(text - start);
    if (length + bytes + 1 > size) break;
    memcpy(out + length, start, bytes); length += bytes;
    if (!combining(character)) ++cells;
  }
  while (cells < cols && length + 1 < size) { out[length++] = ' '; ++cells; }
  if (size) out[length] = '\0';
}

// Cells and CGRAM for up to four rows. Slots are taken in order of first use,
// so an unchanged first row keeps its slots whatever the rows below it do.
class Screen {
 public:
  uint8_t rows, cols;
  uint8_t cells[MAX_ROWS][MAX_COLS];
  uint32_t slot[SLOTS] = {};     // the character in each slot this screen defines; 0 = none
  bool reserved[SLOTS] = {};     // defined by someone else (a server frame): never redefined
  uint8_t reservedRows[SLOTS][8] = {};

  explicit Screen(uint8_t rows_ = 2, uint8_t cols_ = 16)
      : rows(rows_ > MAX_ROWS ? MAX_ROWS : rows_), cols(cols_ > MAX_COLS ? MAX_COLS : cols_) {
    memset(cells, ' ', sizeof cells);
  }

  // A slot the display already shows something in. Its bitmap may be shared
  // by an identical glyph, but the slot is never redefined.
  void reserve(uint8_t index, const uint8_t bitmap[8]) {
    if (index >= SLOTS) return;
    reserved[index] = true;
    memcpy(reservedRows[index], bitmap, 8);
  }

  void line(uint8_t row, const char* text) {
    if (row >= rows) return;
    uint8_t column = 0;
    while (*text && column < cols) {
      const uint32_t character = next(text);
      if (!combining(character)) cells[row][column++] = cell(character);
    }
    while (column < cols) cells[row][column++] = ' ';
  }

  bool operator==(const Screen& other) const {
    return rows == other.rows && cols == other.cols && !memcmp(cells, other.cells, sizeof cells) &&
           !memcmp(slot, other.slot, sizeof slot);
  }
  bool operator!=(const Screen& other) const { return !(*this == other); }

  // Defines this screen's own glyphs, then writes every row.
  template <class LCD> void draw(LCD& lcd) const {
    defineGlyphs(lcd);
    for (uint8_t row = 0; row < rows; ++row) {
      lcd.setCursor(0, row);
      for (uint8_t column = 0; column < cols; ++column) lcd.write(cells[row][column]);
    }
  }

  template <class LCD> void defineGlyphs(LCD& lcd) const {
    for (uint8_t index = 0; index < SLOTS; ++index) {
      if (!slot[index]) continue;
      uint8_t bitmap[8];
      memcpy(bitmap, glyph(slot[index])->rows, 8);
      lcd.createChar(index, bitmap);
    }
  }

 private:
  uint8_t cell(uint32_t character) {
    const Glyph* shape = glyph(character);
    if (!shape) return uint8_t(fold(character));
    for (uint8_t index = 0; index < SLOTS; ++index) {
      if (slot[index] == character) return index;
      if (reserved[index] && !memcmp(reservedRows[index], shape->rows, 8)) return index;
    }
    for (uint8_t index = 0; index < SLOTS; ++index) {
      if (!slot[index] && !reserved[index]) { slot[index] = character; return index; }
    }
    // Eight different special characters already: fold rather than redefine
    // a slot something on the display still uses.
    return uint8_t(fold(character));
  }
};

}  // namespace TrainMeetLcd
