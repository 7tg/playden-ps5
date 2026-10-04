#include "diagnostics/phase_stats.h"

#include <cassert>
#include <cstdint>
#include <string>

namespace {

using playden::diagnostics::PhaseStats;

void TestReportsNearestRankStageDistributions() {
  PhaseStats stats;
  for (std::uint64_t value = 1; value <= 100; ++value) {
    stats.Record(value, value * 2, value * 3, value * 4);
  }

  const std::string json = stats.FormatAndReset();
  assert(json.find("\"dropped_sample_count\":0") != std::string::npos);
  assert(json.find(
             "\"input_tick_update_cpu\":{\"measurement\":\"cpu_elapsed_ns\","
             "\"count\":100,\"p50_ns\":50,\"p95_ns\":95,"
             "\"p99_ns\":99,\"max_ns\":100}") != std::string::npos);
  assert(json.find(
             "\"clear_begin_render_submission_cpu\":{\"measurement\":"
             "\"cpu_submission_elapsed_ns_not_gpu_time\",\"count\":100,"
             "\"p50_ns\":100,\"p95_ns\":190,\"p99_ns\":198,"
             "\"max_ns\":200}") != std::string::npos);
  assert(json.find(
             "\"end_submission_cpu\":{\"measurement\":"
             "\"cpu_submission_elapsed_ns_not_gpu_time\",\"count\":100,"
             "\"p50_ns\":150,\"p95_ns\":285,\"p99_ns\":297,"
             "\"max_ns\":300}") != std::string::npos);
  assert(json.find(
             "\"swap_combined_wait\":{\"measurement\":"
             "\"cpu_elapsed_ns_combined_wait_not_gpu_time\","
             "\"count\":100,\"p50_ns\":200,\"p95_ns\":380,"
             "\"p99_ns\":396,\"max_ns\":400}") != std::string::npos);
}

void TestFormatResetsSamplesAndDrops() {
  PhaseStats stats;
  stats.Record(1, 2, 3, 4);
  const std::string first = stats.FormatAndReset();
  assert(first.find("\"count\":1") != std::string::npos);

  const std::string empty = stats.FormatAndReset();
  assert(empty.find("\"dropped_sample_count\":0") != std::string::npos);
  assert(empty.find("\"count\":0,\"p50_ns\":null,\"p95_ns\":null,"
                    "\"p99_ns\":null,\"max_ns\":null") !=
         std::string::npos);
}

void TestCapacityDropsWholeSamplesWithoutUnboundingStorage() {
  PhaseStats stats;
  for (std::size_t index = 0; index < PhaseStats::kCapacity; ++index) {
    const auto value = static_cast<std::uint64_t>(index + 1);
    stats.Record(value, value, value, value);
  }
  stats.Record(999999, 999999, 999999, 999999);
  stats.Record(999999, 999999, 999999, 999999);

  const std::string json = stats.FormatAndReset();
  assert(json.find("\"dropped_sample_count\":2") != std::string::npos);
  assert(json.find("\"count\":1024") != std::string::npos);
  assert(json.find("\"max_ns\":1024") != std::string::npos);
  assert(json.find("999999") == std::string::npos);
}

}  // namespace

int main() {
  TestReportsNearestRankStageDistributions();
  TestFormatResetsSamplesAndDrops();
  TestCapacityDropsWholeSamplesWithoutUnboundingStorage();
  return 0;
}
