#pragma once

#include <stdexcept>

namespace dynamicleverage {

// The previous native executor changed account leverage. That cannot implement
// a policy that fixes leverage separately for each newly opened volume segment.
// Keep native startup and every legacy write entry point explicitly unavailable
// until a verified opening-volume margin backend replaces that executor.
class NativeExecutionUnavailable final : public std::logic_error {
public:
  NativeExecutionUnavailable()
    : std::logic_error("Dynamic Leverage native execution is unavailable: the opening-volume margin backend has not been verified; account-wide leverage updates are disabled to preserve existing positions") {}
};

[[noreturn]] inline void require_native_execution() {
  throw NativeExecutionUnavailable{};
}

} // namespace dynamicleverage
