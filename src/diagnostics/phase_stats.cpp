#include "diagnostics/phase_stats.h"

#include <algorithm>
#include <cstdio>

namespace playden::diagnostics {
namespace {

void AppendUnsigned(std::string& output, std::uint64_t value) {
  char buffer[32];
  const int length = std::snprintf(buffer, sizeof(buffer), "%llu",
                                   static_cast<unsigned long long>(value));
  if (length > 0) output.append(buffer, static_cast<std::size_t>(length));
}

std::uint64_t Percentile(const std::array<std::uint64_t, PhaseStats::kCapacity>& sorted,
                         std::size_t count, std::size_t numerator) {
  const std::size_t rank = (count * numerator + 99) / 100;
  return sorted[rank - 1];
}

void AppendStage(std::string& output, const char* name, const char* measurement,
                 const std::array<std::uint64_t, PhaseStats::kCapacity>& samples,
                 std::size_t count) {
  std::array<std::uint64_t, PhaseStats::kCapacity> sorted{};
  std::copy_n(samples.begin(), count, sorted.begin());
  std::sort(sorted.begin(), sorted.begin() + count);

  output.append(",\"");
  output.append(name);
  output.append("\":{\"measurement\":\"");
  output.append(measurement);
  output.append("\",\"count\":");
  AppendUnsigned(output, count);
  const auto append_value = [&output, count](const char* field,
                                             std::uint64_t value) {
    output.append(",\"");
    output.append(field);
    output.append("\":");
    if (count == 0) {
      output.append("null");
    } else {
      AppendUnsigned(output, value);
    }
  };
  append_value("p50_ns", count == 0 ? 0 : Percentile(sorted, count, 50));
  append_value("p95_ns", count == 0 ? 0 : Percentile(sorted, count, 95));
  append_value("p99_ns", count == 0 ? 0 : Percentile(sorted, count, 99));
  append_value("max_ns", count == 0 ? 0 : sorted[count - 1]);
  output.push_back('}');
}

}  // namespace

void PhaseStats::Record(std::uint64_t update_ns, std::uint64_t render_ns,
                        std::uint64_t end_ns, std::uint64_t swap_ns) {
  if (sample_count_ == kCapacity) {
    ++dropped_sample_count_;
    return;
  }
  update_ns_[sample_count_] = update_ns;
  render_ns_[sample_count_] = render_ns;
  end_ns_[sample_count_] = end_ns;
  swap_ns_[sample_count_] = swap_ns;
  ++sample_count_;
}

std::string PhaseStats::FormatAndReset() {
  std::string output;
  output.reserve(768);
  output.append("{\"diagnostic\":\"cpu_phase_stats\",\"dropped_sample_count\":");
  AppendUnsigned(output, dropped_sample_count_);
  AppendStage(output, "input_tick_update_cpu", "cpu_elapsed_ns", update_ns_, sample_count_);
  AppendStage(output, "clear_begin_render_submission_cpu",
              "cpu_submission_elapsed_ns_not_gpu_time", render_ns_, sample_count_);
  AppendStage(output, "end_submission_cpu",
              "cpu_submission_elapsed_ns_not_gpu_time", end_ns_, sample_count_);
  AppendStage(output, "swap_combined_wait",
              "cpu_elapsed_ns_combined_wait_not_gpu_time", swap_ns_,
              sample_count_);
  output.push_back('}');

  sample_count_ = 0;
  dropped_sample_count_ = 0;
  return output;
}

}  // namespace playden::diagnostics
