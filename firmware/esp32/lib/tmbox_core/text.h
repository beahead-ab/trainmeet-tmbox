#pragma once

#include <string>
#include <vector>

#include "geometry.h"

namespace tmbox {

/// Fold Å, Ä and Ö down to ASCII for displays without them in CGRAM.
/// Input is UTF-8; output is one byte per character.
std::string transliterate(const std::string& value);

/// Cut or pad to exactly `width` characters, so a frame never inherits
/// leftovers from the frame before it.
std::string fit(const std::string& value, std::uint8_t width);

/// `421>[1A]` for a departure, `[2A]<428` for an arrival. The arrow points
/// the way the train travels, and the brackets hold the track it uses.
std::string departure_mark(const std::string& train_number, const std::string& track);
std::string arrival_mark(const std::string& train_number, const std::string& track);

/// A frame is exactly `rows` lines of exactly `cols` characters.
using Frame = std::vector<std::string>;
Frame frame_of(const Geometry& geometry, const std::vector<std::string>& lines);

}  // namespace tmbox
