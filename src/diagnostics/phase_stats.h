#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace playden::diagnostics {

// Bounded CPU wall-duration samples supplied by the platform loop. This helper
// owns no clock and does not infer GPU execution or presentation outcomes.
class PhaseStats {
 public:
  static constexpr std::size_t kCapacity = 1024;

  void Record(std::uint64_t update_ns, std::uint64_t render_ns,
              std::uint64_t end_ns, std::uint64_t swap_ns);

  // Produces one locale-independent JSON object, then clears all samples and
  // the overflow count for the next reporting window.
  std::string FormatAndReset();

 private:
  std::array<std::uint64_t, kCapacity> update_ns_{};
  std::array<std::uint64_t, kCapacity> render_ns_{};
  std::array<std::uint64_t, kCapacity> end_ns_{};
  std::array<std::uint64_t, kCapacity> swap_ns_{};
  std::size_t sample_count_ = 0;
  std::uint64_t dropped_sample_count_ = 0;
};

}  // namespace playden::diagnostics
