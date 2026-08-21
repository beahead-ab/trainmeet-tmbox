#pragma once

#include <cstdint>
#include <string>

#include "command.h"
#include "model.h"
#include "renderer.h"

namespace tmbox {

/// What a key press did.
enum class Outcome {
  Ignored,   // the key means nothing here, or input is still locked
  Redraw,    // local state moved; nothing leaves the box
  Send,      // a complete command is ready to publish
};

struct KeyResult {
  Outcome outcome;
  Command command;

  // Explicit rather than default member initialisers: those would make this a
  // non-aggregate under C++11, which the ESP32 toolchain may still hand us.
  KeyResult() : outcome(Outcome::Ignored) {}
  explicit KeyResult(Outcome outcome_) : outcome(outcome_) {}
  KeyResult(Outcome outcome_, const Command& command_)
      : outcome(outcome_), command(command_) {}
};

/// Where the box is in its own cache, and what a key does about it.
///
/// Every decision here is local browsing. The only thing that ever leaves is a
/// complete command, and only for an action the server has listed as allowed —
/// the box never invents permission for itself.
class LocalNavigationState {
 public:
  /// `now_ms` is passed in rather than read, so the input lock can be tested
  /// without waiting half a second.
  KeyResult press(char key,
                  std::uint32_t now_ms,
                  const StationConfig& config,
                  const Snapshot& snapshot);

  /// A fresh snapshot replaces the cache wholesale, so a selection that no
  /// longer exists must not survive it.
  void reconcile(const StationConfig& config, const Snapshot& snapshot,
                 std::uint32_t now_ms);

  void show(Screen screen, std::uint32_t now_ms);

  const ViewState& view() const { return view_; }
  ViewState& view() { return view_; }

  /// §5: input is locked for about half a second after a screen change, so a
  /// press meant for the previous screen is not read against the new one.
  static constexpr std::uint32_t INPUT_LOCK_MS = 500;
  bool locked(std::uint32_t now_ms) const;

 private:
  ViewState view_;
  std::uint32_t screen_changed_at_ = 0;
  bool ever_shown_ = false;
};

}  // namespace tmbox
