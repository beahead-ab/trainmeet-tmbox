#pragma once

#include <cstdint>

namespace tmbox {

/// What a box can physically show. Announced in `hello`, so the server knows
/// how much context fits before it formats anything.
struct Geometry {
  std::uint8_t rows;
  std::uint8_t cols;
  /// True when the display carries ÅÄÖ in CGRAM. False means the renderer
  /// transliterates: SPAR, BEGAR, FORARE.
  bool supports_swedish;

  // An explicit constructor rather than default member initialisers: those
  // would make this a non-aggregate under C++11, and the ESP32 Arduino
  // toolchain does not always give us a newer standard.
  constexpr Geometry(std::uint8_t rows_ = 2, std::uint8_t cols_ = 16,
                     bool supports_swedish_ = false)
      : rows(rows_), cols(cols_), supports_swedish(supports_swedish_) {}

  constexpr bool wide() const { return cols >= 20; }
  constexpr bool tall() const { return rows >= 4; }
};

constexpr Geometry GEOMETRY_16X2{2, 16, false};
constexpr Geometry GEOMETRY_20X2{2, 20, false};
constexpr Geometry GEOMETRY_16X4{4, 16, false};
constexpr Geometry GEOMETRY_20X4{4, 20, false};

}  // namespace tmbox
