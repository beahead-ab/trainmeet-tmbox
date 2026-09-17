#pragma once

#include <iostream>
#include <string>
#include <vector>

namespace check {

inline int failures = 0;
inline int checks = 0;

inline void equal(const std::string& expected, const std::string& actual,
                  const std::string& what) {
  ++checks;
  if (expected == actual) return;
  ++failures;
  std::cout << "  FEL " << what << "\n"
            << "    vantat: [" << expected << "]\n"
            << "    faktisk:[" << actual << "]\n";
}

inline void truthy(bool value, const std::string& what) {
  ++checks;
  if (value) return;
  ++failures;
  std::cout << "  FEL " << what << "\n";
}

inline int report() {
  std::cout << (failures ? "MISSLYCKADES: " : "OK: ") << checks << " kontroller, "
            << failures << " fel\n";
  return failures == 0 ? 0 : 1;
}

}  // namespace check
