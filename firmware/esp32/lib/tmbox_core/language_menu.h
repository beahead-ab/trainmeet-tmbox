#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace tmbox {
struct LanguageOption { std::string code, name; };

// Local presentation only: no station, route or traffic-command state.
struct LanguageMenu {
  bool open = false, saving = false, failed = false;
  size_t selected = 0;
  uint32_t sent_at = 0;
  std::string request_id;
  std::vector<LanguageOption> options;

  void begin(const std::string& current) {
    if (options.empty()) return;
    selected = 0;
    for (size_t i = 0; i < options.size(); ++i) if (options[i].code == current) selected = i;
    open = true; saving = failed = false; request_id.clear();
  }
  // Empty = browse/cancel, nonempty = explicitly save this language.
  std::string press(char key) {
    if (!open || saving || options.empty()) return "";
    if (key == '*') { open = false; failed = false; return ""; }
    if (key == 'C') { selected = (selected + 1) % options.size(); failed = false; }
    if (key == '#' && selected < options.size()) return options[selected].code;
    return "";
  }
  void sent(const std::string& id, uint32_t now) {
    request_id = id; sent_at = now; saving = true; failed = false;
  }
  bool reply(const std::string& id, bool accepted) {
    if (!saving || id != request_id) return false;
    saving = false; failed = !accepted; open = !accepted; return true;
  }
  void tick(uint32_t now) {
    if (saving && uint32_t(now - sent_at) >= 5000) { saving = false; failed = true; }
  }
  std::string name() const { return selected < options.size() ? options[selected].name : ""; }
};
}  // namespace tmbox
