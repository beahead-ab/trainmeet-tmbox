#include "text.h"

namespace tmbox {
namespace {

/// UTF-8 two-byte sequences for the Swedish vowels, and what they fold to.
struct Fold {
  unsigned char lead;
  unsigned char trail;
  char ascii;
};

constexpr Fold FOLDS[] = {
    {0xC3, 0x85, 'A'},  // Å
    {0xC3, 0xA5, 'A'},  // å
    {0xC3, 0x84, 'A'},  // Ä
    {0xC3, 0xA4, 'A'},  // ä
    {0xC3, 0x96, 'O'},  // Ö
    {0xC3, 0xB6, 'O'},  // ö
};

}  // namespace

std::string transliterate(const std::string& value) {
  std::string result;
  result.reserve(value.size());
  for (std::size_t index = 0; index < value.size(); ++index) {
    const auto lead = static_cast<unsigned char>(value[index]);
    bool folded = false;
    if (index + 1 < value.size()) {
      const auto trail = static_cast<unsigned char>(value[index + 1]);
      for (const Fold& fold : FOLDS) {
        if (lead == fold.lead && trail == fold.trail) {
          result.push_back(fold.ascii);
          ++index;
          folded = true;
          break;
        }
      }
    }
    if (folded) continue;
    // Anything else outside plain ASCII would render as noise on a character
    // display, so it becomes a space rather than a stray glyph.
    result.push_back(lead < 0x80 ? static_cast<char>(lead) : ' ');
  }
  return result;
}

std::string fit(const std::string& value, std::uint8_t width) {
  std::string result = value.substr(0, width);
  result.append(width - result.size(), ' ');
  return result;
}

std::string departure_mark(const std::string& train_number, const std::string& track) {
  return track.empty() ? train_number + ">" : train_number + ">[" + track + "]";
}

std::string arrival_mark(const std::string& train_number, const std::string& track) {
  return track.empty() ? "<" + train_number : "[" + track + "]<" + train_number;
}

Frame frame_of(const Geometry& geometry, const std::vector<std::string>& lines) {
  Frame frame;
  frame.reserve(geometry.rows);
  for (std::uint8_t row = 0; row < geometry.rows; ++row) {
    const std::string source = row < lines.size() ? lines[row] : std::string();
    frame.push_back(fit(geometry.supports_swedish ? source : transliterate(source),
                        geometry.cols));
  }
  return frame;
}

}  // namespace tmbox
