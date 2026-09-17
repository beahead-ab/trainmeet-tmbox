#include "model.h"

#include <algorithm>

namespace tmbox {

bool Movement::allows(const std::string& action) const {
  return std::find(allowed_actions.begin(), allowed_actions.end(), action)
         != allowed_actions.end();
}

const Track* StationConfig::track_by_id(const std::string& id) const {
  for (const Track& track : tracks) {
    if (track.id == id) return &track;
  }
  return nullptr;
}

}  // namespace tmbox
