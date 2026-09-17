#pragma once

#include <string>

namespace tmbox {

/// One complete operative command, ready for the caller to serialise.
///
/// The core holds no JSON: the box speaks ArduinoJson and the tests speak
/// plain C++, and neither should have to agree on a library to agree on what
/// a command means.
struct Command {
  std::string action;
  std::string movement_id;
  std::string track_id;
  std::string clearance_id;
  std::string message_id;   // the line message being acknowledged
  std::string connection_id;
  std::string train_number;  // the number being looked up
  bool approved = false;
  bool has_approved = false;  // whether `approved` belongs in the payload

  bool empty() const { return action.empty(); }
};

}  // namespace tmbox
